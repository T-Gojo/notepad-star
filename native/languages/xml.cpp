// SPDX-License-Identifier: GPL-3.0-or-later
#include "xml_internal.h"
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <limits>

namespace star::languages::detail {

void fail(QString* error, const QString& message) {
    if (error) *error = message;
}

std::optional<Node> parseXml(const QByteArray& bytes, QString* error) {
    if (error) error->clear();
    if (bytes.isEmpty() || bytes.size() > MaxXmlBytes) {
        fail(error, QStringLiteral("XML is empty or exceeds the 8 MiB limit."));
        return std::nullopt;
    }
    QXmlStreamReader reader(bytes);
    QList<Node> stack;
    std::optional<Node> root;
    qsizetype nodes = 0;
    qsizetype textSize = 0;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference) {
            fail(error, QStringLiteral("DTD and entity declarations/references are forbidden."));
            return std::nullopt;
        }
        if (token == QXmlStreamReader::StartElement) {
            if (++nodes > 100000 || stack.size() >= 16 || reader.attributes().size() > 32
                || !reader.namespaceUri().isEmpty() || !reader.namespaceDeclarations().isEmpty()) {
                fail(error, QStringLiteral("XML exceeds structural limits or uses namespaces."));
                return std::nullopt;
            }
            Node node;
            node.name = reader.name().toString();
            for (const auto& attr : reader.attributes()) {
                if (!attr.namespaceUri().isEmpty()) {
                    fail(error, QStringLiteral("Namespaced XML attributes are unsupported."));
                    return std::nullopt;
                }
                node.attributes.insert(attr.name().toString(), attr.value().toString());
            }
            stack.push_back(std::move(node));
        } else if (token == QXmlStreamReader::Characters) {
            textSize += reader.text().size();
            if (textSize > MaxXmlBytes) {
                fail(error, QStringLiteral("XML text exceeds the limit."));
                return std::nullopt;
            }
            if (!stack.isEmpty()) stack.last().text += reader.text();
        } else if (token == QXmlStreamReader::EndElement) {
            if (stack.isEmpty()) break;
            Node node = stack.takeLast();
            if (stack.isEmpty()) root = std::move(node);
            else stack.last().children.push_back(std::move(node));
        }
    }
    if (reader.hasError() || !stack.isEmpty() || !root || root->name != QStringLiteral("NotepadPlus")) {
        fail(error, QStringLiteral("Malformed XML or missing NotepadPlus root (line %1).").arg(reader.lineNumber()));
        return std::nullopt;
    }
    return root;
}

const Node* child(const Node& node, const QString& name, QString* error, bool required) {
    const Node* found = nullptr;
    for (const auto& item : node.children) {
        if (item.name != name) continue;
        if (found) {
            fail(error, QStringLiteral("Duplicate XML element: %1.").arg(name));
            return nullptr;
        }
        found = &item;
    }
    if (!found && required) fail(error, QStringLiteral("Missing XML element: %1.").arg(name));
    return found;
}

std::optional<QByteArray> readAsset(const QString& path, QString* error) {
    if (!(path.startsWith(QStringLiteral(":/")) || QDir::isAbsolutePath(path))) {
        fail(error, QStringLiteral("Asset paths must be absolute or explicit Qt resource paths."));
        return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxXmlBytes) {
        fail(error, QStringLiteral("Could not open asset, or asset exceeds the byte limit."));
        return std::nullopt;
    }
    QByteArray bytes = file.read(MaxXmlBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > MaxXmlBytes) {
        fail(error, QStringLiteral("Could not read bounded XML asset."));
        return std::nullopt;
    }
    return bytes;
}

std::optional<int> integer(const Node& node, const QString& attribute, int fallback,
    int minimum, int maximum, QString* error) {
    const auto text = node.attributes.value(attribute);
    if (text.isEmpty()) return fallback;
    static const QRegularExpression digits(QStringLiteral("^-?[0-9]+$"));
    bool ok = false;
    const int value = text.toInt(&ok, 10);
    if (!ok || !digits.match(text).hasMatch() || value < minimum || value > maximum) {
        fail(error, QStringLiteral("Invalid numeric XML attribute: %1.").arg(attribute));
        return std::nullopt;
    }
    return value;
}

std::optional<bool> boolean(const Node& node, const QString& attribute, bool fallback, QString* error) {
    if (!node.attributes.contains(attribute)) return fallback;
    const auto text = node.attributes.value(attribute);
    if (text == QStringLiteral("yes")) return true;
    if (text == QStringLiteral("no")) return false;
    fail(error, QStringLiteral("Expected yes/no XML attribute: %1.").arg(attribute));
    return std::nullopt;
}

std::optional<Style> parseStyle(const Node& node, std::optional<int> fixedId, QString* error) {
    if (!node.children.isEmpty()) {
        fail(error, QStringLiteral("Style elements must not contain child elements."));
        return std::nullopt;
    }
    Style style;
    const auto id = integer(node, QStringLiteral("styleID"), fixedId.value_or(-1), 0, 255, error);
    const auto colorFlags = integer(node, QStringLiteral("colorStyle"), 3, 0, 3, error);
    const auto fontFlags = integer(node, QStringLiteral("fontStyle"), -1, -1, 7, error);
    const auto fontSize = integer(node, QStringLiteral("fontSize"), 0, 0, 128, error);
    const auto nesting = integer(node, QStringLiteral("nesting"), 0, 0, 0x7ffffff, error);
    if (!id || *id < 0 || (fixedId && *id != *fixedId) || !colorFlags || !fontFlags || !fontSize || !nesting) {
        fail(error, QStringLiteral("Invalid style ID, flags, size, or nesting."));
        return std::nullopt;
    }
    style.id = *id;
    style.name = node.attributes.value(QStringLiteral("name"));
    style.colorFlags = *colorFlags;
    style.fontFlags = *fontFlags;
    style.fontSize = *fontSize;
    style.fontFamily = node.attributes.value(QStringLiteral("fontName"));
    style.nesting = static_cast<quint32>(*nesting);
    static const QRegularExpression colorPattern(QStringLiteral("^[0-9a-fA-F]{6}$"));
    for (const auto& pair : {
        std::pair{QStringLiteral("fgColor"), &style.foreground},
        std::pair{QStringLiteral("bgColor"), &style.background}}) {
        const QString value = node.attributes.value(pair.first);
        if (value.isEmpty()) continue;
        if (!colorPattern.match(value).hasMatch()) {
            fail(error, QStringLiteral("Style colors must be six hexadecimal RGB digits."));
            return std::nullopt;
        }
        *pair.second = QColor(QStringLiteral("#") + value);
    }
    return style;
}

bool attributesAllowed(const Node& node, const QStringList& allowed, QString* error) {
    for (auto it = node.attributes.cbegin(); it != node.attributes.cend(); ++it) {
        if (!allowed.contains(it.key())) {
            fail(error, QStringLiteral("Unknown XML attribute %1 on %2.").arg(it.key(), node.name));
            return false;
        }
    }
    return true;
}

bool childrenAllowed(const Node& node, const QStringList& allowed, QString* error) {
    for (const auto& item : node.children) {
        if (!allowed.contains(item.name)) {
            fail(error, QStringLiteral("Unknown XML element %1 in %2.").arg(item.name, node.name));
            return false;
        }
    }
    return true;
}

} // namespace star::languages::detail
