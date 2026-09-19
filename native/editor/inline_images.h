#pragma once
#include "ScintillaEditBase.h"

#include <QByteArray>
#include <QCache>
#include <QColor>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QRect>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace star {

// A pasted image always owns a whole line so the space it needs is a whole number of rows.
enum class InlineImageFlavor { Token, Markdown, Html };

struct InlineImageLine {
    InlineImageFlavor flavor = InlineImageFlavor::Token;
    QString reference;
    QString alt;
    int width = 0;
    int height = 0;
};

inline InlineImageFlavor inlineImageFlavorFor(const QString& documentPath) {
    const auto suffix = QFileInfo(documentPath).suffix().toLower();
    if (suffix == "md" || suffix == "markdown" || suffix == "mdown" || suffix == "mkd") return InlineImageFlavor::Markdown;
    if (suffix == "htm" || suffix == "html" || suffix == "xhtml") return InlineImageFlavor::Html;
    return InlineImageFlavor::Token;
}

// Document text is untrusted. A UNC, device or scheme-like reference would make the
// operating system dial out to an attacker-chosen host the moment a file is opened,
// which on Windows hands the logged-on user's credentials to that host.
inline bool inlineImageReferenceIsRemote(const QString& reference) {
    QString value = reference;
    value.replace('\\', '/');
    return value.startsWith("//") || value.contains("://");
}

inline QString inlineImageUnescape(const QString& value) {
    QString result = value;
    result.replace("&lt;", "<");
    result.replace("&gt;", ">");
    result.replace("&quot;", "\"");
    result.replace("&#39;", "'");
    result.replace("&amp;", "&");
    return result;
}

inline std::optional<InlineImageLine> parseInlineImageLine(const QString& raw) {
    static const QRegularExpression token(
        QStringLiteral(R"(^\[\[image:[ \t]*([^|\[\]\r\n]{1,512}?)[ \t]*\|[ \t]*(\d{1,5})x(\d{1,5})[ \t]*\]\]$)"));
    static const QRegularExpression markdown(
        QStringLiteral(R"(^!\[([^\[\]\r\n]{0,256})\]\([ \t]*(?:<([^<>\r\n]{1,512})>|([^\s()\r\n]{1,512}))(?:[ \t]+=(\d{1,5})x(\d{1,5}))?[ \t]*\)$)"));
    static const QRegularExpression html(QStringLiteral(R"(^<img[ \t]+([^<>\r\n]{1,1024}?)[ \t]*/?>$)"));
    static const QRegularExpression attribute(QStringLiteral(R"RX(([A-Za-z]{1,32})[ \t]*=[ \t]*"([^"\r\n]{0,512})")RX"));
    const QString line = raw.trimmed();
    if (line.isEmpty() || line.size() > 2048) return std::nullopt;
    auto match = token.match(line);
    if (match.hasMatch()) {
        InlineImageLine image;
        image.flavor = InlineImageFlavor::Token;
        image.reference = match.captured(1);
        image.width = match.captured(2).toInt();
        image.height = match.captured(3).toInt();
        return image.width > 0 && image.height > 0 ? std::optional(image) : std::nullopt;
    }
    match = markdown.match(line);
    if (match.hasMatch()) {
        InlineImageLine image;
        image.flavor = InlineImageFlavor::Markdown;
        image.alt = match.captured(1);
        image.reference = match.captured(2).isEmpty() ? match.captured(3) : match.captured(2);
        image.width = match.captured(4).toInt();
        image.height = match.captured(5).toInt();
        if (image.reference.isEmpty()) return std::nullopt;
        if (image.width <= 0 || image.height <= 0) image.width = image.height = 0;
        return image;
    }
    match = html.match(line);
    if (match.hasMatch()) {
        InlineImageLine image;
        image.flavor = InlineImageFlavor::Html;
        auto attributes = attribute.globalMatch(match.captured(1));
        while (attributes.hasNext()) {
            const auto found = attributes.next();
            const auto name = found.captured(1).toLower();
            if (name == "src") image.reference = inlineImageUnescape(found.captured(2));
            else if (name == "alt") image.alt = inlineImageUnescape(found.captured(2));
            else if (name == "width") image.width = found.captured(2).toInt();
            else if (name == "height") image.height = found.captured(2).toInt();
        }
        if (image.reference.isEmpty()) return std::nullopt;
        if (image.width <= 0 || image.height <= 0) image.width = image.height = 0;
        return image;
    }
    return std::nullopt;
}

inline QString formatInlineImageLine(const InlineImageLine& image) {
    const auto width = QString::number(image.width);
    const auto height = QString::number(image.height);
    if (image.flavor == InlineImageFlavor::Markdown) {
        const bool brackets = image.reference.contains(' ') || image.reference.contains('(') || image.reference.contains(')');
        return "![" + image.alt + "](" + (brackets ? "<" + image.reference + ">" : image.reference) +
            " =" + width + "x" + height + ")";
    }
    if (image.flavor == InlineImageFlavor::Html)
        return "<img src=\"" + image.reference.toHtmlEscaped() + "\" alt=\"" + image.alt.toHtmlEscaped() +
            "\" width=\"" + width + "\" height=\"" + height + "\">";
    return "[[image:" + image.reference + "|" + width + "x" + height + "]]";
}

// Only references that survive a write/read round trip may enter a document.
inline bool inlineImageLineRoundTrips(const InlineImageLine& image) {
    if (image.reference.isEmpty() || image.width <= 0 || image.height <= 0) return false;
    if (image.width > 99999 || image.height > 99999) return false;
    const auto parsed = parseInlineImageLine(formatInlineImageLine(image));
    return parsed && parsed->flavor == image.flavor && parsed->reference == image.reference &&
        parsed->alt == image.alt && parsed->width == image.width && parsed->height == image.height;
}

// A whole line is read at once; anything longer than a path plus its size cannot be an image line.
inline QString inlineImageLineText(const ScintillaEditBase* editor, sptr_t line) {
    const sptr_t length = editor->send(SCI_LINELENGTH, static_cast<uptr_t>(line));
    if (length <= 0 || length > 4096) return {};
    QByteArray bytes(static_cast<qsizetype>(length) + 1, '\0');
    editor->sends(SCI_GETLINE, static_cast<uptr_t>(line), bytes.data());
    bytes.resize(static_cast<qsizetype>(length));
    return QString::fromUtf8(bytes).trimmed();
}

// Draws pasted images over the lines that reference them, and resizes them by dragging the corner grip.
class InlineImageView {
public:
    struct Placement {
        sptr_t line = 0;
        sptr_t start = 0;
        sptr_t end = 0;
        QRect cover;
        QRect image;
        QRect grip;
        InlineImageLine info;
        QString path;
    };

    explicit InlineImageView(ScintillaEditBase* owner) : editor(owner) { pixmaps.setMaxCost(128 * 1024); }

    QString folder;
    std::function<void(sptr_t, InlineImageLine)> commit;
    bool enabled = true;

    void forget() {
        selected = -1;
        dragging = false;
    }

    void paint() {
        const auto found = placements();
        if (found.empty()) return;
        QPainter painter(editor->viewport());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const auto background = colorOf(editor->send(SCI_STYLEGETBACK, STYLE_DEFAULT));
        const auto foreground = colorOf(editor->send(SCI_STYLEGETFORE, STYLE_DEFAULT));
        for (const auto& placement : found) {
            painter.fillRect(placement.cover, background);
            const auto pixmap = pixmapFor(placement.path);
            if (pixmap.isNull()) {
                painter.setPen(QPen(foreground, 1, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(placement.image.adjusted(0, 0, -1, -1));
                painter.setPen(foreground);
                painter.drawText(placement.image.adjusted(6, 6, -6, -6), Qt::AlignLeft | Qt::TextWordWrap,
                    "Missing image: " + placement.info.reference);
            } else {
                painter.drawPixmap(placement.image, pixmap);
            }
            if (placement.line != selected) continue;
            painter.setPen(QPen(QColor(0, 122, 204), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(placement.image.adjusted(1, 1, -1, -1));
            painter.fillRect(placement.grip, QColor(0, 122, 204));
        }
    }

    bool mousePress(QMouseEvent* event) {
        if (!enabled || event->button() != Qt::LeftButton) return false;
        const auto point = event->position().toPoint();
        const auto found = placements();
        for (auto it = found.rbegin(); it != found.rend(); ++it) {
            if (it->line == selected && it->grip.contains(point)) {
                dragging = true;
                dragLine = it->line;
                dragInfo = it->info;
                dragOrigin = point;
                dragStart = it->image.size();
                dragSize = dragStart;
                return true;
            }
            if (!it->image.contains(point)) continue;
            selected = it->line;
            editor->send(SCI_GOTOPOS, it->start);
            editor->viewport()->update();
            return true;
        }
        if (selected < 0) return false;
        selected = -1;
        editor->viewport()->update();
        return false;
    }

    bool mouseMove(QMouseEvent* event) {
        if (!enabled) return false;
        const auto point = event->position().toPoint();
        if (dragging) {
            const int width = std::clamp(dragStart.width() + point.x() - dragOrigin.x(), 24, 4096);
            int height = std::clamp(dragStart.height() + point.y() - dragOrigin.y(), 24, 4096);
            if (!(event->modifiers() & Qt::ShiftModifier) && dragStart.width() > 0 && dragStart.height() > 0)
                height = std::clamp(static_cast<int>(std::lround(static_cast<double>(width) * dragStart.height() /
                    dragStart.width())), 24, 4096);
            dragSize = QSize(width, height);
            editor->viewport()->update();
            return true;
        }
        bool overGrip = false;
        for (const auto& placement : placements())
            if (placement.line == selected && placement.grip.contains(point)) overGrip = true;
        if (overGrip) {
            editor->viewport()->setCursor(Qt::SizeFDiagCursor);
            reshaped = true;
            return true;
        }
        if (reshaped) {
            editor->viewport()->unsetCursor();
            reshaped = false;
        }
        return false;
    }

    bool mouseRelease(QMouseEvent* event) {
        if (!dragging || event->button() != Qt::LeftButton) return false;
        dragging = false;
        auto resized = dragInfo;
        resized.width = dragSize.width();
        resized.height = dragSize.height();
        if (commit && inlineImageLineRoundTrips(resized)) commit(dragLine, resized);
        editor->viewport()->update();
        return true;
    }

    std::vector<Placement> placements() {
        std::vector<Placement> found;
        if (!enabled || editor->send(SCI_GETLINECOUNT) <= 0) return found;
        const sptr_t lines = editor->send(SCI_GETLINECOUNT);
        const sptr_t first = editor->send(SCI_GETFIRSTVISIBLELINE);
        const sptr_t rows = editor->send(SCI_LINESONSCREEN) + 2;
        sptr_t previous = -1;
        for (sptr_t row = first; row <= first + rows; ++row) {
            const sptr_t line = editor->send(SCI_DOCLINEFROMVISIBLE, static_cast<uptr_t>(std::max<sptr_t>(row, 0)));
            if (line < 0 || line >= lines || line == previous) continue;
            previous = line;
            const auto info = parseInlineImageLine(inlineImageLineText(editor, line));
            if (!info) continue;
            const int textHeight = static_cast<int>(editor->send(SCI_TEXTHEIGHT, static_cast<uptr_t>(line)));
            if (textHeight <= 0) continue;
            Placement placement;
            placement.line = line;
            placement.info = *info;
            placement.start = editor->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line));
            placement.end = editor->send(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(line));
            placement.path = resolve(info->reference);
            const int x = static_cast<int>(editor->send(SCI_POINTXFROMPOSITION, 0, placement.start));
            const int y = static_cast<int>(editor->send(SCI_POINTYFROMPOSITION, 0, placement.start));
            const int wrapped = std::max(1, static_cast<int>(editor->send(SCI_WRAPCOUNT, static_cast<uptr_t>(line))));
            const int annotated = std::max(0, static_cast<int>(editor->send(SCI_ANNOTATIONGETLINES, static_cast<uptr_t>(line))));
            const int block = (wrapped + annotated) * textHeight;
            QSize size = displaySize(*info, placement.path);
            if (dragging && line == dragLine) size = dragSize;
            else if (size.height() > block && block > 0)
                size = QSize(std::max(1, size.width() * block / size.height()), block);
            placement.image = QRect(x, y, std::max(1, size.width()), std::max(1, size.height()));
            // The cover reaches the right edge so wrapped rows of the reference cannot peek out beside the image.
            placement.cover = QRect(x, y, std::max(editor->viewport()->width() - x, placement.image.width() + 2),
                std::max(block, placement.image.height()));
            placement.grip = QRect(placement.image.right() - 11, placement.image.bottom() - 11, 12, 12);
            found.push_back(placement);
        }
        return found;
    }

    // Rows a line must occupy for its image to fit; used to reserve space with annotations.
    int rowsFor(const InlineImageLine& info, int textHeight) {
        if (textHeight <= 0) return 1;
        const auto size = displaySize(info, resolve(info.reference));
        return std::clamp((size.height() + textHeight - 1) / textHeight, 1, 4096);
    }

    QString resolve(const QString& reference) const {
        if (reference.isEmpty() || inlineImageReferenceIsRemote(reference)) return {};
        const QFileInfo info(reference);
        if (info.isAbsolute() || folder.isEmpty()) return QDir::cleanPath(reference);
        const auto joined = QDir::cleanPath(QDir(folder).absoluteFilePath(reference));
        return inlineImageReferenceIsRemote(joined) ? QString() : joined;
    }

private:
    static QColor colorOf(sptr_t value) {
        return QColor(static_cast<int>(value & 0xff), static_cast<int>((value >> 8) & 0xff),
            static_cast<int>((value >> 16) & 0xff));
    }

    QSize displaySize(const InlineImageLine& info, const QString& path) {
        if (info.width > 0 && info.height > 0) return QSize(info.width, info.height);
        const auto pixmap = pixmapFor(path);
        if (pixmap.isNull()) return QSize(240, 120);
        const int limit = std::clamp(editor->viewport()->width() - 48, 64, 720);
        if (pixmap.width() <= limit) return pixmap.size();
        return QSize(limit, std::max(1, pixmap.height() * limit / pixmap.width()));
    }

    QPixmap pixmapFor(const QString& path) {
        if (path.isEmpty()) return {};
        const QFileInfo info(path);
        if (!info.isFile() || info.size() <= 0 || info.size() > 64LL * 1024 * 1024) return {};
        const auto key = path + "|" + QString::number(info.lastModified().toMSecsSinceEpoch()) + "|" +
            QString::number(info.size());
        if (const auto* cached = pixmaps.object(key)) return *cached;
        QImage image;
        if (!image.load(path) || image.isNull()) return {};
        const QPixmap pixmap = QPixmap::fromImage(image);
        const auto cost = static_cast<qint64>(pixmap.width()) * pixmap.height() * 4 / 1024;
        pixmaps.insert(key, new QPixmap(pixmap), std::max<int>(1, static_cast<int>(std::min<qint64>(cost, 65536))));
        return pixmap;
    }

    ScintillaEditBase* editor;
    QCache<QString, QPixmap> pixmaps;
    InlineImageLine dragInfo;
    QPoint dragOrigin;
    QSize dragStart;
    QSize dragSize;
    sptr_t selected = -1;
    sptr_t dragLine = -1;
    bool dragging = false;
    bool reshaped = false;
};
}
