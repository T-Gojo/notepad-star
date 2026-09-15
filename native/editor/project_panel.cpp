#include "project_panel.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QToolBar>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QRegularExpression>
#include <stdexcept>

namespace star {
namespace {
struct Node { QString kind; QString name; QList<Node> children; };
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
QList<Node> readNodes(QXmlStreamReader& reader, bool root) {
    QList<Node> nodes;
    while (reader.readNextStartElement()) {
        const auto kind = reader.name().toString();
        require(root ? kind == "Project" : kind == "Folder" || kind == "File", "Unsupported workspace element.");
        require(reader.attributes().size() == 1 && reader.attributes().hasAttribute("name"), "Workspace nodes require only a name attribute.");
        const auto name = reader.attributes().value("name").toString();
        require(!name.isEmpty() && name.size() <= 32767, "Invalid workspace name or path.");
        Node node{kind, name, {}};
        node.children = readNodes(reader, false);
        require(kind != "File" || node.children.isEmpty(), "File entries cannot contain children.");
        nodes.push_back(std::move(node));
    }
    return nodes;
}
void populate(QTreeWidgetItem* parent, const QList<Node>& nodes, const QString& directory) {
    for (const auto& node : nodes) {
        auto* item = new QTreeWidgetItem(parent, {node.kind == "File" ? QFileInfo(node.name).fileName() : node.name});
        item->setData(0, Qt::UserRole, node.kind);
        if (node.kind == "File") {
            auto path = node.name;
            path.replace('\\', '/');
            const bool drive = QRegularExpression("^[A-Za-z]:/").match(path).hasMatch();
#ifdef Q_OS_WIN
            const bool foreign = path.startsWith('/') && !path.startsWith("//");
#else
            const bool foreign = drive || path.startsWith("//");
#endif
            item->setData(0, Qt::UserRole + 1, foreign || drive || QDir::isAbsolutePath(path) ? path : QDir(directory).absoluteFilePath(path));
            item->setData(0, Qt::UserRole + 2, foreign);
            item->setToolTip(0, item->data(0, Qt::UserRole + 1).toString());
        } else item->setFlags(item->flags() | Qt::ItemIsEditable);
        populate(item, node.children, directory);
    }
}
void writeNodes(QXmlStreamWriter& writer, QTreeWidgetItem* parent, const QString& directory, int depth, int& count) {
    require(depth <= 32, "Workspace nesting exceeds its limit.");
    for (int i = 0; i < parent->childCount(); ++i) {
        require(++count <= 10000, "Workspace contains too many items.");
        auto* item = parent->child(i);
        const auto kind = item->data(0, Qt::UserRole).toString();
        writer.writeStartElement(kind);
        const auto name = kind == "File" ? item->data(0, Qt::UserRole + 2).toBool() ?
            item->data(0, Qt::UserRole + 1).toString() : QDir(directory).relativeFilePath(item->data(0, Qt::UserRole + 1).toString()) : item->text(0);
        require(!name.trimmed().isEmpty() && name.size() <= 32767, "Workspace entry names cannot be empty or oversized.");
        writer.writeAttribute("name", name);
        writeNodes(writer, item, directory, depth + 1, count);
        writer.writeEndElement();
    }
}
}

ProjectPanel::ProjectPanel(const QString& title, QWidget* parent) : QDockWidget(title, parent), baseTitle(title) {
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* toolbar = new QToolBar(content);
    items = new QTreeWidget(content);
    items->setHeaderHidden(true);
    layout->addWidget(toolbar);
    layout->addWidget(items);
    setWidget(content);
    toolbar->addAction("Load", this, [this] { report([&] {
        const auto path = QFileDialog::getOpenFileName(this, "Load Workspace", {}, "Workspace XML (*.xml);;All files (*)");
        if (path.isEmpty()) return;
        QFile file(path);
        require(file.open(QIODevice::ReadOnly) && file.size() <= 8 * 1024 * 1024, "Cannot read workspace or it exceeds 8 MiB.");
        importXml(file.readAll(), path);
    }); });
    toolbar->addAction("Save", this, [this] { save(); });
    toolbar->addAction("Save As", this, [this] { save(true); });
    toolbar->addAction("+ Project", this, [this] { report([&] {
        bool accepted = false;
        const auto name = QInputDialog::getText(this, "New Project", "Name", QLineEdit::Normal, "Project", &accepted);
        if (!accepted) return;
        require(!name.trimmed().isEmpty(), "Project name cannot be empty.");
        auto* item = new QTreeWidgetItem(items, {name});
        item->setData(0, Qt::UserRole, "Project");
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        items->setCurrentItem(item);
        changed();
    }); });
    toolbar->addAction("+ Folder", this, [this] { report([&] {
        auto* parentItem = items->currentItem();
        require(parentItem && parentItem->data(0, Qt::UserRole) != "File", "Select a project or virtual folder.");
        bool accepted = false;
        const auto name = QInputDialog::getText(this, "Virtual Folder", "Name", QLineEdit::Normal, "Folder", &accepted);
        if (!accepted) return;
        require(!name.trimmed().isEmpty(), "Folder name cannot be empty.");
        auto* item = new QTreeWidgetItem(parentItem, {name});
        item->setData(0, Qt::UserRole, "Folder");
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        parentItem->setExpanded(true);
        changed();
    }); });
    toolbar->addAction("+ Files", this, [this] { report([&] {
        auto* parentItem = items->currentItem();
        require(parentItem && parentItem->data(0, Qt::UserRole) != "File", "Select a project or virtual folder.");
        for (const auto& path : QFileDialog::getOpenFileNames(this, "Add Project Files")) {
            auto* item = new QTreeWidgetItem(parentItem, {QFileInfo(path).fileName()});
            item->setData(0, Qt::UserRole, "File");
            item->setData(0, Qt::UserRole + 1, path);
            item->setToolTip(0, path);
            changed();
        }
        parentItem->setExpanded(true);
    }); });
    toolbar->addAction("Remove", this, [this] {
        auto* item = items->currentItem();
        if (item && QMessageBox::question(this, "Remove Project Entry", "Remove this entry and its children from the workspace?\nNo disk files will be deleted.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) { delete item; changed(); }
    });
    connect(items, &QTreeWidget::itemChanged, this, [this] { changed(); });
    connect(items, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole) == "File" && openFile) {
            if (item->data(0, Qt::UserRole + 2).toBool()) {
                QMessageBox::warning(this, "Foreign project path", "This absolute path belongs to another operating system. Remove and re-add the local file to remap it.");
            } else openFile(item->data(0, Qt::UserRole + 1).toString());
        }
    });
}
void ProjectPanel::report(const std::function<void()>& operation) {
    try { operation(); }
    catch (const std::exception& error) { QMessageBox::critical(this, "Workspace operation failed", QString::fromUtf8(error.what())); }
}
void ProjectPanel::changed() {
    if (!updating) { dirty = true; setWindowTitle(baseTitle + " *"); }
}
void ProjectPanel::importXml(const QByteArray& xml, const QString& path) {
    require(xml.size() <= 8 * 1024 * 1024, "Workspace exceeds 8 MiB.");
    QXmlStreamReader validation(xml);
    int depth = 0;
    int count = 0;
    while (!validation.atEnd()) {
        const auto token = validation.readNext();
        require(token != QXmlStreamReader::DTD && token != QXmlStreamReader::EntityReference, "Workspace DTDs and entities are not allowed.");
        if (validation.isStartElement()) {
            require(++depth <= 32 && ++count <= 10000 && validation.namespaceUri().isEmpty(), "Workspace structure exceeds supported limits.");
        } else if (validation.isEndElement()) --depth;
    }
    require(!validation.hasError(), "Malformed workspace XML.");
    QXmlStreamReader reader(xml);
    require(reader.readNextStartElement() && reader.name() == u"NotepadPlus" && reader.attributes().isEmpty(), "Expected a NotepadPlus workspace root.");
    const auto nodes = readNodes(reader, true);
    require(!reader.hasError(), "Malformed workspace XML.");
    if (!confirmClose()) return;
    const QScopedValueRollback<bool> loading(updating, true);
    items->clear();
    populate(items->invisibleRootItem(), nodes, QFileInfo(path).absolutePath());
    filename = path;
    expectedHash = QCryptographicHash::hash(xml, QCryptographicHash::Sha256);
    dirty = false;
    setWindowTitle(baseTitle);
}
QByteArray ProjectPanel::exportXml(const QString& path) const {
    QByteArray output;
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement("NotepadPlus");
    int count = 0;
    writeNodes(writer, items->invisibleRootItem(), QFileInfo(path).absolutePath(), 0, count);
    writer.writeEndElement();
    writer.writeEndDocument();
    require(!writer.hasError() && output.size() <= 8 * 1024 * 1024, "Workspace output is invalid or too large.");
    return output;
}
bool ProjectPanel::save(bool saveAs) {
    bool saved = false;
    report([&] {
        const auto path = filename.isEmpty() || saveAs ? QFileDialog::getSaveFileName(this, "Save Workspace", filename, "Workspace XML (*.xml)") : filename;
        if (path.isEmpty()) return;
        require(!canSave || canSave(path), "Workspace cannot overwrite an open document.");
        if (path == filename) {
            QFile previous(path);
            require(previous.open(QIODevice::ReadOnly) && previous.size() <= 8 * 1024 * 1024 &&
                QCryptographicHash::hash(previous.readAll(), QCryptographicHash::Sha256) == expectedHash,
                "Workspace changed on disk. Use Save As to preserve your changes.");
        }
        const auto xml = exportXml(path);
        QSaveFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(xml) == xml.size() && file.commit(), "Cannot save workspace.");
        filename = path;
        expectedHash = QCryptographicHash::hash(xml, QCryptographicHash::Sha256);
        dirty = false; saved = true;
        setWindowTitle(baseTitle);
    });
    return saved;
}
bool ProjectPanel::confirmClose() {
    if (!dirty) return true;
    const auto answer = QMessageBox::question(this, baseTitle, "Save workspace changes?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save());
}
}
