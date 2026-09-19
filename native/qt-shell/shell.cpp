#include "star-native/src/lib.rs.h"
#include "ScintillaEditBase.h"
#include "ILexer.h"
#include "Lexilla.h"
#include "SciLexer.h"
#include "../editor/search_task.h"
#include "../editor/rich_export.h"
#include "../editor/project_panel.h"
#include "../editor/macro_import.h"
#include "../editor/config_import.h"
#include "../editor/instance_channel.h"
#include "../editor/compact_gutter.h"
#include "../editor/workspace_editor.h"
#include "json_panel.h"
#include "editor_tabs.h"
#include "search_dialog.h"
#include "language_catalog.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFontDatabase>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFontComboBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QKeySequenceEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCursor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTreeWidget>
#include <QDirIterator>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QDateTime>
#include <QLocale>
#include <QUuid>
#include <QPrinter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QProgressBar>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QCryptographicHash>
#include <QStringDecoder>
#include <QSaveFile>
#include <QPlainTextEdit>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QSet>
#include <QInputDialog>
#include <QIcon>
#include <QImage>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeView>
#include <QTabBar>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocalSocket>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QScrollArea>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QActionGroup>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>
#include <functional>
#include <cstring>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
#include <array>
#include <tuple>
#include <cstdio>

static void initializeApplicationResources() {
    Q_INIT_RESOURCE(star_application);
    Q_INIT_RESOURCE(star_brand_assets);
}

namespace star {
namespace {
QString qs(rust::Str text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}
QString qs(const rust::String& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
FilePath filePath(const QString& path) {
    FilePath value;
#ifdef Q_OS_WIN
    for (auto unit : path) value.windows.push_back(unit.unicode());
#else
    for (auto byte : QFile::encodeName(path)) value.unix.push_back(static_cast<std::uint8_t>(byte));
#endif
    return value;
}
QString pathText(const FilePath& value) {
#ifdef Q_OS_WIN
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(value.windows.data()), static_cast<qsizetype>(value.windows.size()));
#else
    return QFile::decodeName(QByteArray(reinterpret_cast<const char*>(value.unix.data()), static_cast<qsizetype>(value.unix.size())));
#endif
}
rust::Str rs(const QByteArray& value) {
    return rust::Str(value.constData(), static_cast<std::size_t>(value.size()));
}

class DocumentMapView final : public ScintillaEditBase {
public:
    explicit DocumentMapView(QWidget* parent) : ScintillaEditBase(parent) {
        setFocusPolicy(Qt::NoFocus);
        setAcceptDrops(false);
        setContextMenuPolicy(Qt::NoContextMenu);
    }
    std::function<void(sptr_t)> navigate;
protected:
    void mousePressEvent(QMouseEvent* event) override {
        const auto point = event->position().toPoint();
        const auto position = send(SCI_POSITIONFROMPOINTCLOSE, static_cast<uptr_t>(point.x()),
            static_cast<sptr_t>(point.y()));
        if (position >= 0 && navigate) navigate(position);
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override { mousePressEvent(event); }
    void mouseMoveEvent(QMouseEvent* event) override { event->accept(); }
    void keyPressEvent(QKeyEvent* event) override { event->ignore(); }
    void inputMethodEvent(QInputMethodEvent* event) override { event->ignore(); }
    void dropEvent(QDropEvent* event) override { event->ignore(); }
};

class EditorApplication final : public QApplication {
public:
    using QApplication::QApplication;
    void setFileHandler(std::function<void(const QString&)> handler) {
        fileHandler = std::move(handler);
        const auto waiting = pendingFiles;
        pendingFiles.clear();
        for (const auto& file : waiting) fileHandler(file);
    }
    void clearFileHandler() { fileHandler = {}; }
protected:
    bool event(QEvent* event) override {
        if (event->type() != QEvent::FileOpen) return QApplication::event(event);
        const auto* open = static_cast<QFileOpenEvent*>(event);
        const auto file = open->file();
        if (file.isEmpty() || file.size() > 32768 || file.contains(QChar(0)) ||
            (!open->url().isEmpty() && !open->url().isLocalFile())) {
            qCritical("Rejected a nonlocal or invalid operating-system file-open event.");
            return true;
        }
        if (fileHandler) fileHandler(file);
        else if (pendingFiles.size() < 64) pendingFiles.append(file);
        else qCritical("Operating-system file-open queue exceeded 64 entries.");
        return true;
    }
private:
    QStringList pendingFiles;
    std::function<void(const QString&)> fileHandler;
};
class LargeFileWindow final : public QMainWindow {
public:
    LargeFileWindow(QString path, QWidget* parent) : QMainWindow(parent, Qt::Window), filename(std::move(path)) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QFileInfo(filename).fileName() + " - READ-ONLY PAGED PREVIEW");
        resize(980, 680);
        editor = new ScintillaEditBase(this);
        editor->send(SCI_SETCODEPAGE, SC_CP_UTF8);
        const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont).family().toUtf8();
        editor->sends(SCI_STYLESETFONT, STYLE_DEFAULT, font.constData());
        editor->send(SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
        editor->send(SCI_STYLECLEARALL);
        editor->send(SCI_SETREADONLY, true);
        setCentralWidget(editor);
        auto* toolbar = addToolBar("Paged preview");
        previous = toolbar->addAction("Previous page", this, [this] { safely([&] {
            if (history.empty()) return;
            load(history.back());
            history.pop_back();
            previous->setEnabled(!history.empty());
        }); });
        next = toolbar->addAction("Next page", this, [this] { safely([&] {
            const auto old = offset;
            load(nextOffset);
            history.push_back(old);
            previous->setEnabled(true);
        }); });
        auto* location = new QLineEdit;
        location->setPlaceholderText("Byte offset");
        toolbar->addWidget(location);
        toolbar->addAction("Go", this, [this, location] { safely([&] {
            bool valid = false;
            const auto position = location->text().toULongLong(&valid);
            check(valid, "Enter an unsigned byte offset.");
            const auto old = offset;
            load(position);
            history.push_back(old);
            previous->setEnabled(true);
        }); });
        toolbar->addAction("Reload first page", this, [this] { safely([&] { load(0); history.clear(); previous->setEnabled(false); }); });
        load(0);
    }
    void load(std::uint64_t position) {
        const auto page = read_large_page(filePath(filename), position);
        editor->send(SCI_SETREADONLY, false);
        editor->send(SCI_SETSTATUS, SC_STATUS_OK);
        editor->send(SCI_CLEARALL);
        editor->sends(SCI_ADDTEXT, page.text.size(), page.text.data());
        editor->send(SCI_EMPTYUNDOBUFFER);
        editor->send(SCI_SETSAVEPOINT);
        editor->send(SCI_SETREADONLY, true);
        check(editor->send(SCI_GETSTATUS) == SC_STATUS_OK && editor->send(SCI_GETLENGTH) == static_cast<sptr_t>(page.text.size()),
            "Could not render the complete preview page.");
        offset = page.offset;
        nextOffset = page.next;
        previous->setEnabled(!history.empty());
        next->setEnabled(nextOffset < page.size);
        statusBar()->showMessage(QString("READ ONLY | bytes %1..%2 of %3 | %4 | Pages are read on demand; this is not a whole-file edit buffer.")
            .arg(static_cast<qulonglong>(page.offset)).arg(static_cast<qulonglong>(page.next)).arg(static_cast<qulonglong>(page.size))
            .arg(page.hex ? "hex representation (binary/legacy/invalid UTF-8)" : "UTF-8 text"));
    }
    ScintillaEditBase* pane() const { return editor; }
private:
    void safely(const std::function<void()>& action) {
        try { action(); }
        catch (const std::exception& error) { QMessageBox::critical(this, "Read-only preview", QString::fromUtf8(error.what())); }
    }
    QString filename;
    ScintillaEditBase* editor = nullptr;
    QAction* previous = nullptr;
    QAction* next = nullptr;
    std::uint64_t offset = 0;
    std::uint64_t nextOffset = 0;
    std::vector<std::uint64_t> history;
};

// Keeps the chrome at two bars: the button runs its primary command, and hovering
// reveals the secondary options that used to need a third toolbar row.
class ToolGroupButton final : public QToolButton {
public:
    explicit ToolGroupButton(QWidget* parent) : QToolButton(parent) {
        setPopupMode(QToolButton::MenuButtonPopup);
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        setAutoRaise(true);
        hover.setSingleShot(true);
        hover.setInterval(280);
        connect(&hover, &QTimer::timeout, this, [this] {
            if (isEnabled() && underMouse() && menu() && !menu()->isVisible()) showMenu();
        });
    }
    void bind(QAction* action, const QString& label, const QString& hint) {
        primary = action;
        setText(label);
        setIcon(action->icon());
        setCheckable(action->isCheckable());
        setToolTip(hint);
        syncFromAction();
        connect(this, &QToolButton::clicked, this, [this] {
            if (primary) primary->trigger();
            syncFromAction();
        });
        connect(action, &QAction::changed, this, [this] { syncFromAction(); });
    }
protected:
    void enterEvent(QEnterEvent* event) override {
        QToolButton::enterEvent(event);
        hover.start();
    }
    void leaveEvent(QEvent* event) override {
        hover.stop();
        QToolButton::leaveEvent(event);
    }
private:
    void syncFromAction() {
        if (!primary) return;
        if (isCheckable() && isChecked() != primary->isChecked()) setChecked(primary->isChecked());
        setEnabled(primary->isEnabled());
    }
    QAction* primary = nullptr;
    QTimer hover;
};

class Shell final : public QMainWindow {
public:
    explicit Shell(const LaunchSettings& launch, const rust::Vec<FilePath>& files) : controller(create_controller()) {
        const bool preview = launch.preview;
        const bool testing = launch.smoke_test;
        noSession = launch.no_session || preview || testing;
        testMode = testing;
        QString catalogError;
        catalog = languages::LanguageCatalog::load(&catalogError);
        if (!catalog) throw std::runtime_error(catalogError.toStdString());
        diskWatcher = new QFileSystemWatcher(this);
        diskTimer = new QTimer(this);
        diskTimer->setSingleShot(true);
        diskTimer->setInterval(200);
        connect(diskWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
            pendingDiskChecks.insert(path);
            if (!diskTimer->isActive()) diskTimer->start();
        });
        connect(diskTimer, &QTimer::timeout, this, [this] {
            if (macroPlaybackActive) { diskTimer->start(); return; }
            const auto changedPaths = pendingDiskChecks;
            pendingDiskChecks.clear();
            QString lastError;
            for (auto& view : views) {
                const auto path = pathText(document_path(*controller, view.id));
                if (!changedPaths.contains(path)) continue;
                try {
                    view.diskChanged = document_changed_on_disk(*controller, view.id);
                    if (view.monitoring && view.diskChanged && !document_dirty(*controller, view.id) && QFileInfo::exists(path)) {
                        applyReload(view, reload_document(*controller, view.id));
                        view.primary->send(SCI_DOCUMENTEND);
                    }
                } catch (const std::exception& error) {
                    lastError = "File monitoring: " + QString::fromUtf8(error.what());
                }
            }
            refresh();
            refreshWatches();
            if (!lastError.isEmpty()) statusBar()->showMessage(lastError);
        });
        setObjectName("notepad-star");
        setWindowTitle("Notepad Star");
        setAcceptDrops(true);
        resize(1120, 740);
        setMinimumSize(740, 440);
        menuBar()->setNativeMenuBar(false);

        auto* center = new QWidget(this);
        auto* layout = new QVBoxLayout(center);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        if (development_build()) {
            auto* warning = new QLabel(
                "  DEVELOPMENT BUILD: not a qualified public release.", center);
            warning->setObjectName("preview-warning");
            warning->setStyleSheet("QLabel { background: #fff2ca; color: #564416; padding: 5px; }");
            layout->addWidget(warning);
        }
        tabs = new EditorTabs(center);
        editorSplit = tabs;
        tabs->setObjectName("document-tabs");
        tabs->setTabsClosable(true);
        tabs->setMovable(true);
        tabs->setDocumentMode(true);
        layout->addWidget(editorSplit);
        toolsTimer = new QTimer(this);
        toolsTimer->setSingleShot(true);
        toolsTimer->setInterval(350);
        connect(toolsTimer, &QTimer::timeout, this, [this] {
            if (tearingDown || closingWindow) return;
            if (macroPlaybackActive) { toolsTimer->start(); return; }
            refreshJson();
            if (comparisonPreferences().value("auto_recompare").toBool(true)) refreshComparison();
            else invalidateComparison();
        });
        inlineImageTimer = new QTimer(this);
        inlineImageTimer->setSingleShot(true);
        inlineImageTimer->setInterval(80);
        connect(inlineImageTimer, &QTimer::timeout, this, [this] {
            if (tearingDown || closingWindow) return;
            guarded([&] { flushInlineImages(); });
        });
        tabs->tabDropped = [this](quint64 id, int group) { guarded([&] { moveDocumentToGroup(id, group); }); };
        comparisonStatus = new QLabel(this);
        comparisonStatus->setTextFormat(Qt::PlainText);
        statusBar()->addPermanentWidget(comparisonStatus);
        comparisonStatus->hide();
        setCentralWidget(center);
        documentList = new QListWidget;
        documentList->setObjectName("document-list");
        dock = new QDockWidget("Document Sidebar", this);
        dock->setObjectName("document-list-dock");
        dock->setWidget(documentList);
        addDockWidget(Qt::LeftDockWidgetArea, dock);
        resizeDocks({dock}, {170}, Qt::Horizontal);
        // The sidebar starts closed; the toolbar button, View menu or Ctrl+Alt+L opens it.
        dock->hide();

        search = new SearchDialog(this);
        query = search->findText;
        replacement = search->replacementText;
        matchCase = search->matchCase;
        wholeWord = search->wholeWord;
        searchMode = search->mode;
        dotNewline = search->dotNewline;
        search->findRequested = [this](bool reverse) { invoke(reverse ? "find_previous" : "find_next"); };
        search->replaceRequested = [this](bool all) { guarded([&] {
            check(!macroPlaybackActive && !macroRecording, "Finish the macro before replacing text.");
            replace(all);
        }); };
        search->findAllRequested = [this](bool all, bool count) { invoke(count ? "search_count" : all ? "find_all_open" : "find_all_current"); };
        search->cancelRequested = [this] { invoke("cancel_search"); };
        search->filesRequested = [this] { invoke("find_in_files"); };

        auto* toolbar = addToolBar("Main");
        toolbar->setObjectName("main-toolbar");
        toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        toolbar->setIconSize(QSize(16, 16));
        const QStringList menuNames{
            "File", "Edit", "Search", "View", "Encoding", "Language", "Settings",
            "Tools", "Macro", "Run", "Plugins", "Window", "Help"};
        for (const auto& name : menuNames) menus[name] = menuBar()->addMenu(name);
        for (const auto& spec : star::actions(*controller)) {
            const QString id = qs(spec.id);
            const auto route = qs(spec.menu).split('/');
            auto* parentMenu = menus.at(route.first());
            QString routeKey = route.first();
            for (qsizetype part = 1; part < route.size(); ++part) {
                routeKey += "/" + route[part];
                if (menus.find(routeKey) == menus.end()) menus[routeKey] = parentMenu->addMenu(route[part]);
                parentMenu = menus.at(routeKey);
            }
            auto* action = parentMenu->addAction(qs(spec.label));
            action->setObjectName(id);
            action->setCheckable(spec.checkable);
            if (!spec.shortcut.empty()) action->setShortcut(QKeySequence(qs(spec.shortcut)));
            action->setProperty("defaultShortcut", action->shortcut().toString(QKeySequence::PortableText));
            action->setShortcutContext(Qt::WindowShortcut);
            commands.emplace(id, action);
            connect(action, &QAction::triggered, this, [this, id] { invoke(id); });
            const std::map<QString, QStyle::StandardPixmap> toolbarIcons{
                {"new", QStyle::SP_FileIcon}, {"open", QStyle::SP_DialogOpenButton},
                {"save", QStyle::SP_DialogSaveButton}, {"close", QStyle::SP_DialogCloseButton},
                {"undo", QStyle::SP_ArrowBack}, {"redo", QStyle::SP_ArrowForward},
                {"find", QStyle::SP_FileDialogContentsView}};
            if (toolbarIcons.count(id)) {
                action->setIcon(style()->standardIcon(toolbarIcons.at(id)));
                toolbar->addAction(action);
            }
        }
        commands.at("document_list")->setChecked(!dock->isHidden());
        connect(dock, &QDockWidget::visibilityChanged, this, [this] {
            commands.at("document_list")->setChecked(!dock->isHidden());
        });
        toolbar->addSeparator();
        auto* sidebarButton = new QToolButton(toolbar);
        commands.at("document_list")->setIcon(style()->standardIcon(QStyle::SP_FileDialogListView));
        sidebarButton->setDefaultAction(commands.at("document_list"));
        sidebarButton->setText("Sidebar");
        sidebarButton->setToolTip("Show or hide Document Sidebar (Ctrl+Alt+L)");
        toolbar->addWidget(sidebarButton);
        toolbar->addSeparator();
        commands.at("toggle_comparison")->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        commands.at("json_inspector")->setIcon(style()->standardIcon(QStyle::SP_FileDialogInfoView));
        auto* compareMenu = new QMenu("Compare", this);
        compareMenu->setObjectName("compare-tool-menu");
        for (const auto* id : {"compare_tabs", "next_difference", "previous_difference"})
            compareMenu->addAction(commands.at(id));
        compareMenu->addSeparator();
        for (const auto* id : {"swap_comparison", "comparison_settings", "stop_comparison"})
            compareMenu->addAction(commands.at(id));
        compareButton = new ToolGroupButton(toolbar);
        compareButton->setObjectName("compare-tool-button");
        compareButton->setMenu(compareMenu);
        compareButton->bind(commands.at("toggle_comparison"), "Compare",
            "Compare tabs \xe2\x80\x94 hover for difference navigation and options");
        toolbar->addWidget(compareButton);
        auto* jsonViews = new QActionGroup(this);
        jsonViews->setExclusive(true);
        auto* jsonMenu = new QMenu("JSON", this);
        jsonMenu->setObjectName("json-tool-menu");
        for (const auto* id : {"json_view_tree", "json_view_graph", "json_view_pretty"}) {
            jsonViews->addAction(commands.at(id));
            jsonMenu->addAction(commands.at(id));
        }
        jsonMenu->addSeparator();
        for (const auto* id : {"json_format", "json_minify"}) jsonMenu->addAction(commands.at(id));
        jsonButton = new ToolGroupButton(toolbar);
        jsonButton->setObjectName("json-tool-button");
        jsonButton->setMenu(jsonMenu);
        jsonButton->bind(commands.at("json_inspector"), "JSON",
            "JSON Inspector (Ctrl+Alt+I) \xe2\x80\x94 hover for tree, graph and pretty views");
        toolbar->addWidget(jsonButton);
        syncToolStates();
        recentMenu = new QMenu("Recent Files", this);
        commands.at("open_recent")->setMenu(recentMenu);
        connect(recentMenu, &QMenu::aboutToShow, this, [this] { guarded([&] { populateRecentMenu(); }); });
        menus.at("Language")->addSeparator();
        {
            // Grouping by initial letter keeps the menu short; hovering a letter reveals its languages.
            std::vector<std::pair<QString, QByteArray>> ordered;
            for (const auto& language : catalog->languages())
                if (!language.displayName.isEmpty()) ordered.emplace_back(language.displayName, language.id.toUtf8());
            std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
                const int compared = left.first.compare(right.first, Qt::CaseInsensitive);
                return compared != 0 ? compared < 0 : left.second < right.second;
            });
            std::map<QString, QMenu*> letters;
            for (const auto& entry : ordered) {
                const QChar initial = entry.first.at(0).toUpper();
                const QString group = initial.isLetter() ? QString(initial) : QStringLiteral("#");
                auto letter = letters.find(group);
                if (letter == letters.end())
                    letter = letters.emplace(group, menus.at("Language")->addMenu(group)).first;
                const auto identifier = entry.second;
                letter->second->addAction(entry.first, this, [this, identifier] {
                    guarded([&] { validate_command(*controller, "select_language"); applyLexer(current(), identifier); });
                });
            }
        }
        connect(tabs, &EditorTabs::tabCloseRequested, this, [this](int index) {
            guarded([&] { closeTab(index); });
        });
        connect(tabs, &EditorTabs::currentChanged, this, [this](int) {
            if (!tearingDown) guarded([&] { refresh(); });
        });
        connect(documentList, &QListWidget::currentRowChanged, this, [this](int row) {
            if (row >= 0) tabs->setCurrentIndex(row);
        });
        connect(tabs, &EditorTabs::tabOrderChanged, this, [this] {
            guarded([&] { normalizePinnedOrder(); refresh(); recoveryPending = true; });
        });
        connect(tabs, &EditorTabs::tabContextRequested, this, [this](int index, const QPoint& point) {
            tabs->setCurrentIndex(index);
            QMenu menu(this);
            for (const auto* id : {"pin_tab", "tab_color", "close", "close_others", "close_left", "close_right", "close_unpinned"})
                menu.addAction(commands.at(id));
            menu.addSeparator();
            menu.addAction(commands.at("move_other_view"));
            menu.addAction(commands.at("move_left_view"));
            menu.addAction(commands.at("move_right_view"));
            menu.addAction(commands.at("compare_tabs"));
            menu.addAction(commands.at("stop_comparison"));
            menu.addSeparator();
            menu.addAction(commands.at("json_inspector"));
            menu.addAction(commands.at("json_format"));
            menu.addAction(commands.at("json_preview"));
            menu.addAction(commands.at("json_graph"));
            menu.exec(point);
        });
        QString recoveryPath;
        if (testing) {
            testDirectory = std::make_unique<QTemporaryDir>();
            check(testDirectory->isValid(), "Cannot create test recovery storage.");
            recoveryPath = testDirectory->path();
        } else {
            const auto profile = launch.profile.windows.empty() && launch.profile.unix.empty() ?
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : pathText(launch.profile);
            recoveryPath = QDir(profile).filePath("recovery");
        }
        guarded([&] {
            settingsPath = QFileInfo(recoveryPath).dir().filePath("settings.json");
            if (testing) settingsPath = testDirectory->filePath("settings.json");
            const auto json = qs(load_settings(*controller, filePath(settingsPath)));
            options = QJsonDocument::fromJson(json.toUtf8()).object();
            settingsReady = true;
            applyOptions();
        });
        guarded([&] {
            initialize_recovery(*controller, filePath(recoveryPath),
                !noSession && settingsReady && options.value("restore_session").toBool(true));
            recoveryReady = true;
            if (!noSession && settingsReady && options.value("restore_session").toBool(true))
                restoreViews(resume_workspace(*controller));
        });
        lastSessionPath = QFileInfo(settingsPath).dir().filePath("last-session.json");
        if (!noSession && recoveryReady && legacy_session_allowed(*controller) && tabs->count() == 0 &&
            options.value("restore_session").toBool(true) && QFileInfo::exists(lastSessionPath))
            guarded([&] { restoreViews(reopen_session(*controller, filePath(lastSessionPath))); });
        guarded([&] {
            const auto notices = qs(resume_notices(*controller));
            if (notices.isEmpty()) return;
            auto* message = new QMessageBox(QMessageBox::Warning, "Workspace restored with warnings",
                notices, QMessageBox::Ok, this);
            message->setTextFormat(Qt::PlainText);
            message->setAttribute(Qt::WA_DeleteOnClose);
            message->open();
        });
        for (const auto& path : files) guarded([&] {
            if (launch.reuse_instance) check_forward_file(path);
            openNativeFile(path);
        });
        if (tabs->count() == 0 || preview) addDocument(preview);
        if (!launch.language.empty()) for (auto& view : views) applyLexer(view, qs(launch.language).toUtf8());
        if (launch.line != 0 || launch.column != 0) {
            auto* pane = current().primary;
            const auto line = launch.line == 0 ? pane->send(SCI_LINEFROMPOSITION, pane->send(SCI_GETCURRENTPOS)) :
                std::min<sptr_t>(static_cast<sptr_t>(launch.line - 1), pane->send(SCI_GETLINECOUNT) - 1);
            const auto position = pane->send(SCI_FINDCOLUMN, static_cast<uptr_t>(line), launch.column == 0 ? 0 : static_cast<sptr_t>(launch.column - 1));
            pane->send(SCI_GOTOPOS, position);
        }
        if (launch.read_only) for (const auto& view : views) view.primary->send(SCI_SETREADONLY, true);
        if (!noSession && settingsReady && recoveryReady && options.value("restore_session").toBool(true))
            guarded([&] { retain_workspace(*controller, snapshots(), dark, wrap); recoveryPending = false; });
        recoveryTimer = new QTimer(this);
        recoveryTimer->setInterval(5000);
        connect(recoveryTimer, &QTimer::timeout, this, [this] {
            if (!recoveryPending || !recoveryReady || macroPlaybackActive) return;
            try { checkpointNow(); }
            catch (const std::exception& error) {
                recoveryTimer->stop();
                QMessageBox::critical(this, "Recovery paused", QString::fromUtf8(error.what()) +
                    "\nAutomatic recovery is paused. Save your documents explicitly.");
            }
        });
        if (!testing) recoveryTimer->start();
    }

    ~Shell() override {
        tearingDown = true;
        if (toolsTimer) toolsTimer->stop();
        if (inlineImageTimer) inlineImageTimer->stop();
        if (jsonPanel) jsonPanel->selectSource = {};
        if (managementTask) delete managementTask.data();
        if (activeSearch) delete activeSearch.data();
        if (outlineTask) delete outlineTask.data();
        // Editor callbacks must stop before the Rust controller is destroyed.
        delete takeCentralWidget();
    }
    bool startMaximized() const { return options.value("start_maximized").toBool(true); }

    void openFromOperatingSystem(const QString& path) {
        guarded([&] {
            check(!tearingDown && !closingWindow && isEnabled() && !macroRecording && !macroPlaybackActive && !QApplication::activeModalWidget(),
                "The window is busy. Finish the current dialog or macro and open the file again.");
            openFile(path);
            if (isMinimized()) showNormal();
            raise(); activateWindow();
        });
    }
    QByteArray acceptForwarded(const QByteArray& payload) {
        const auto request = decode_forward_request(rs(payload));
        rust::Vec<rust::String> errors;
        if (tearingDown || closingWindow || !isEnabled() || QApplication::activeModalWidget() || macroRecording || macroPlaybackActive || !composingEditors.isEmpty()) {
            errors.push_back(rust::String("The existing window is busy or closing. Retry after its current operation, or use --new-instance."));
            return qs(forward_response(0, std::move(errors))).toUtf8();
        }
        const auto language = qs(request.settings.language);
        if (!language.isEmpty() && !catalog->language(language)) {
            errors.push_back(rust::String("Unknown forwarded language profile; no files were opened."));
            return qs(forward_response(0, std::move(errors))).toUtf8();
        }
        std::uint32_t opened = 0;
        for (const auto& path : request.files) {
            try {
                check_forward_file(path);
                openNativeFile(path, {}, false);
                auto& view = current();
                if (!language.isEmpty()) applyLexer(view, language.toUtf8());
                if (request.settings.read_only) {
                    view.primary->send(SCI_SETREADONLY, true);
                    if (view.monitoring) view.previousReadOnly = true;
                }
                if (request.settings.line || request.settings.column) {
                    auto* pane = activeEditor();
                    const auto line = request.settings.line == 0 ? pane->send(SCI_LINEFROMPOSITION, pane->send(SCI_GETCURRENTPOS)) :
                        std::min<sptr_t>(static_cast<sptr_t>(request.settings.line - 1), pane->send(SCI_GETLINECOUNT) - 1);
                    pane->send(SCI_GOTOPOS, pane->send(SCI_FINDCOLUMN, line,
                        request.settings.column == 0 ? 0 : static_cast<sptr_t>(request.settings.column - 1)));
                    pane->send(SCI_SCROLLCARET);
                }
                ++opened;
            } catch (const std::exception& error) {
                auto message = QString::fromUtf8(error.what());
                if (message.size() > 256) message = message.left(256) + " [truncated]";
                errors.push_back(rust::String(message.toStdString()));
            }
        }
        const auto response = qs(forward_response(opened, std::move(errors))).toUtf8();
        if (isMinimized()) showNormal();
        raise(); activateWindow();
        const auto messages = QJsonDocument::fromJson(response).object()["errors"].toArray();
        if (!messages.isEmpty() && !testMode) {
            QStringList lines;
            for (const auto& error : messages) lines.append(error.toString());
            auto* message = new QMessageBox(this);
            message->setAttribute(Qt::WA_DeleteOnClose);
            message->setTextFormat(Qt::PlainText);
            message->setWindowTitle("Forwarded File Errors");
            message->setText(QString("Opened %1 file(s).\n\n").arg(opened) + lines.join('\n'));
            message->open();
        }
        return response;
    }
    void smokeTest() {
        check((findChild<QLabel*>("preview-warning") != nullptr) == development_build(),
            "The visible build channel does not match the compiled profile.");
        check(QApplication::applicationDisplayName() == "Notepad Star" && !QApplication::windowIcon().isNull(),
            "Application branding/icon resources were not initialized.");
        check(!QPixmap(":/notepad-star/brand/notepad-star-wordmark.png").isNull() &&
            !QPixmap(":/notepad-star/brand/notepad-star-wordmark-dark.png").isNull(),
            "The light/dark branded wordmarks were not embedded.");
        if (qEnvironmentVariableIsSet("NOTEPAD_STAR_TEXT_MEASUREMENT_CHECK")) {
            textMeasurementChecks();
            finish_recovery(*controller);
            return;
        }
        uiToolsChecks();
        if (qEnvironmentVariableIsSet("NOTEPAD_STAR_UI_TOOLS_CHECK")) {
            finish_recovery(*controller);
            return;
        }
        check(!menuBar()->isNativeMenuBar(), "Menus escaped the application window.");
        check(tabs->count() == 1, "Expected an initial document.");
        workspaceRestartChecks();
        auto* editor = activeEditor();
        check(editor->send(SCI_MARKERSYMBOLDEFINED, SC_MARKNUM_FOLDERSUB) == SC_MARK_VLINE,
            "Fold continuation lines must not display collapse buttons.");
        editor->send(SCI_CLEARALL);
        QTest::keyClicks(editor, "hello rust");
        check(text(editor) == "hello rust", "Keyboard input failed.");
        check(editor->send(SCI_GETMODIFY) != 0, "Dirty state was not set.");
        check(qs(document_title(*controller, current().id)).endsWith('*'), "Rust dirty state did not update.");
        dispatch("clone_view");
        auto& first = current();
        check(first.clone->isVisible(), "Split view was not shown.");
        check(first.clone->send(SCI_GETDOCPOINTER) == editor->send(SCI_GETDOCPOINTER), "Cloned views do not share a document.");
        first.clone->send(SCI_BEGINUNDOACTION);
        first.clone->sends(SCI_APPENDTEXT, 1, "!");
        first.clone->send(SCI_ENDUNDOACTION);
        check(text(editor) == "hello rust!", "Clone edits did not reach the shared buffer.");
        dispatch("undo");
        check(text(editor) == "hello rust", "Undo failed across shared views.");

        const QString composed = QString::fromUtf8("\xe6\x97\xa5\xe6\x9c\xac\xf0\x9f\x9a\x80");
        editor->send(SCI_GOTOPOS, editor->send(SCI_GETLENGTH));
        QInputMethodEvent preedit(composed, {});
        QApplication::sendEvent(editor, &preedit);
        check(composingEditors.contains(editor) && editor->send(SCI_AUTOCACTIVE) == 0,
            "IME preedit was not protected from automatic completion.");
        bool compositionRecordingRefused = false;
        try { dispatch("macro_start"); } catch (const std::exception&) { compositionRecordingRefused = true; }
        check(compositionRecordingRefused && !macroRecording, "Macro recording began during active composition.");
        QInputMethodEvent commit;
        commit.setCommitString(composed);
        QApplication::sendEvent(editor, &commit);
        check(!composingEditors.contains(editor), "Committed IME input remained marked as composing.");
        check(text(editor).endsWith(composed.toUtf8()), "Unicode IME commit did not reach the buffer.");
        editor->send(SCI_CLEARALL);
        editor->sends(SCI_ADDTEXT, 11, "abc 123 abc");
        query->setText("abc");
        editor->send(SCI_SETSEL, 0, 0);
        dispatch("find_next");
        check(editor->send(SCI_GETSELECTIONSTART) == 0 && editor->send(SCI_GETSELECTIONEND) == 3,
            "Literal find selected the wrong range.");
        // This proves the Boost build path, not complete regex/replacement parity.
        editor->send(SCI_SETTARGETRANGE, 0, editor->send(SCI_GETLENGTH));
        editor->send(SCI_SETSEARCHFLAGS, SCFIND_REGEXP | SCFIND_CXX11REGEX);
        const char* regex = "(?<=abc )\\d+";
        const auto match = editor->sends(SCI_SEARCHINTARGET, std::strlen(regex), regex);
        check(match == 4, "Boost lookbehind regex integration failed.");
        dispatch("word_wrap");
        check(editor->send(SCI_GETWRAPMODE) == SC_WRAP_WORD, "Wrap action failed.");
        dispatch("dark_theme");
        const auto darkStyle = catalog->defaultStyle(languages::Theme::Dark);
        check(darkStyle && darkStyle->background &&
            editor->send(SCI_STYLEGETBACK, STYLE_DEFAULT) == sciColor(*darkStyle->background), "Theme action failed.");
        dispatch("new");
        check(tabs->count() == 2, "New command did not create a separate tab.");
        check(activeEditor()->send(SCI_GETLENGTH) == 0, "New tab inherited previous text.");
        closeTab(1);
        check(tabs->count() == 1, "Clean tab close failed.");
        check(text(editor) == "abc 123 abc", "Closing another tab lost the original text.");
        editor->send(SCI_SETSAVEPOINT);
        check(encoding_labels().size() == 52, "The native encoding selector omitted supported codecs.");
        const std::vector<std::tuple<QByteArray, QByteArray, QByteArray>> codecFixtures{
            {"OEM 437", QByteArray("\x82"), QByteArray("\xc3\xa9")},
            {"Windows-1251", QByteArray("\xcf\xf0\xe8\xe2\xe5\xf2"), QByteArray("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82")},
            {"ISO 8859-1", QByteArray("\x80"), QByteArray("\xc2\x80")},
            {"UTF-16 LE (no BOM)", QByteArray("a\0", 2), QByteArray("a")}};
        int codecIndex = 0;
        for (const auto& fixture : codecFixtures) {
            const auto path = testDirectory->filePath(QString("codec-%1.txt").arg(codecIndex++));
            {
                QFile source(path);
                check(source.open(QIODevice::WriteOnly) && source.write(std::get<1>(fixture)) == std::get<1>(fixture).size(),
                    "Cannot write encoding fixture.");
            }
            openFile(path, std::get<0>(fixture));
            check(text(activeEditor()) == std::get<2>(fixture), "Native file open decoded the wrong code page.");
            check(saveView(current(), false), "Native legacy save failed.");
            {
                QFile saved(path);
                check(saved.open(QIODevice::ReadOnly) && saved.readAll() == std::get<1>(fixture),
                    "Opening and saving silently changed original encoding bytes.");
            }
            closeTab(tabs->currentIndex());
        }
        check(tabs->count() == 1, "Codec fixture windows did not close cleanly.");
        const QString fileName = testDirectory->filePath("unicode.txt");
        const QByteArray initial = QByteArray("\xef\xbb\xbf") + QString::fromUtf8("caf\xc3\xa9\r\nalpha alpha").toUtf8();
        {
            QFile file(fileName);
            check(file.open(QIODevice::WriteOnly), "Cannot create test document.");
            check(file.write(initial) == initial.size(), "Cannot write test document.");
        }
        openFile(fileName);
        check(tabs->count() == 2, "File open failed.");
        const auto fileId = current().id;
        check(qs(document_encoding(*controller, fileId)) == "UTF-8 BOM", "BOM encoding was not detected.");
        openFile(fileName);
        check(tabs->count() == 2 && current().id == fileId, "Duplicate file opened twice.");
        auto* fileEditor = current().primary;
        query->setText("alpha");
        replacement->setText("beta");
        replace(true);
        saveView(current(), false);
        {
            QFile file(fileName);
            check(file.open(QIODevice::ReadOnly), "Cannot read saved file.");
            check(file.readAll() == QByteArray("\xef\xbb\xbf") + QString::fromUtf8("caf\xc3\xa9\r\nbeta beta").toUtf8(),
                "Saving lost BOM, Unicode, EOLs or replacement content.");
        }
        fileEditor->send(SCI_UNDO);
        check(text(fileEditor) == initial.mid(3), "Replace-all undo did not restore one operation.");
        saveView(current(), false);
        const auto sessionFile = filePath(testDirectory->filePath("exported-session.json"));
        save_session(*controller, snapshots(), sessionFile, dark, wrap);
        auto restored = create_controller();
        const auto loaded = load_session(*restored, sessionFile);
        check(loaded.size() == 2, "Session export/import lost documents.");
        check(loaded[1].dirty, "Recovered snapshot must require save or discard.");
        checkpointNow();
        {
            QFile file(fileName);
            check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot simulate external edit.");
            check(file.write("external") == 8, "Cannot write external edit.");
        }
        bool refused = false;
        try {
            const auto fileText = text(fileEditor);
            save_document(*controller, fileId, rs(fileText), filePath(QString()), rust::Str(), false);
        } catch (const std::exception&) { refused = true; }
        check(refused, "An external change was overwritten.");
        const auto reloaded = reload_document(*controller, fileId);
        check(qs(reloaded.text) == "external", "Reload did not read the disk version.");
        fileEditor->send(SCI_SETREADONLY, true);
        applyReload(current(), reloaded);
        check(text(fileEditor) == "external" && fileEditor->send(SCI_GETREADONLY) != 0,
            "Reload must update read-only documents without unlocking them.");
        fileEditor->send(SCI_SETREADONLY, false);
        closeTab(tabs->currentIndex());
        searchMode->setCurrentIndex(2);
        query->setText("(?<=abc )\\d+");
        auto* regexPane = activeEditor();
        regexPane->send(SCI_SETSEL, 0, 0);
        find(false);
        waitForSearch();
        if (regexPane->send(SCI_GETSELECTIONSTART) != 4 || regexPane->send(SCI_GETSELECTIONEND) != 7)
            throw std::runtime_error(QString("Regex selection %1..%2; status: %3; test text: %4")
                .arg(regexPane->send(SCI_GETSELECTIONSTART)).arg(regexPane->send(SCI_GETSELECTIONEND))
                .arg(statusBar()->currentMessage()).arg(QString::fromUtf8(text(regexPane))).toStdString());
        query->setText("(abc)");
        replacement->setText("$1!");
        replace(true);
        waitForSearch();
        check(text(current().primary) == "abc! 123 abc!", "Regex captures were not expanded by the worker.");
        dispatch("undo");
        check(text(current().primary) == "abc 123 abc", "Regex replacement was not one undo action.");
        activeEditor()->send(SCI_SETSEL, 0, 3);
        query->setText("abc");
        replacement->setText("x");
        replace(false); waitForSearch();
        check(text(activeEditor()) == "x 123 abc" && activeEditor()->send(SCI_GETCURRENTPOS) == 1,
            "Single regex replacement did not preserve its target and cursor semantics.");
        dispatch("undo");
        query->setText("(");
        find(false);
        waitForSearch(false);
        check(!lastSearchError.isEmpty(), "Invalid regex was silently accepted.");
        query->setText("abc");
        find(false);
        dispatch("cancel_search");
        check(!activeSearch, "Cancelled worker remained active.");
        check(text(current().primary) == "abc 123 abc", "Cancellation changed the source document.");
        query->setText("(?=a)");
        activeEditor()->send(SCI_SETSEL, 0, 0);
        find(false); waitForSearch();
        check(activeEditor()->send(SCI_GETSELECTIONSTART) == 0, "Initial zero-width search failed.");
        find(false); waitForSearch();
        check(activeEditor()->send(SCI_GETSELECTIONSTART) == 8, "Repeated zero-width search did not advance.");
        const auto overlapSource = text(activeEditor());
        activeEditor()->send(SCI_CLEARALL);
        activeEditor()->sends(SCI_ADDTEXT, 5, "ababa");
        activeEditor()->send(SCI_SETSEL, 5, 5);
        query->setText("aba");
        find(true); waitForSearch();
        check(activeEditor()->send(SCI_GETSELECTIONSTART) == 2 && activeEditor()->send(SCI_GETSELECTIONEND) == 5,
            "Reverse regex navigation must use the upstream overlapping-match semantics.");
        activeEditor()->send(SCI_CLEARALL);
        activeEditor()->sends(SCI_ADDTEXT, overlapSource.size(), overlapSource.constData());
        searchMode->setCurrentIndex(0);
        const auto persisted = QJsonDocument::fromJson(qs(current_settings(*controller)).toUtf8()).object();
        check(persisted["editor"].toObject()["dark"].toBool() == dark, "Theme was not persisted.");
        current().primary->send(SCI_SETSAVEPOINT);
        auto* automationPane = activeEditor();
        automationPane->send(SCI_DOCUMENTEND);
        dispatch("macro_start");
        QTest::keyClicks(automationPane, "xy");
        dispatch("macro_stop");
        check(!macroSteps.isEmpty(), "Macro recorder captured no text.");
        dispatch("macro_play");
        check(text(automationPane).endsWith("xyxy"), "Recorded macro did not replay its insertion.");
        dispatch("undo");
        check(text(automationPane).endsWith("xy") && !text(automationPane).endsWith("xyxy"), "Macro replay was not undoable.");
        const QByteArray shortcutFixture =
            "<NotepadPlus><Macros><Macro name=\"Imported\">"
            "<Action type=\"1\" message=\"2170\" wParam=\"0\" lParam=\"0\" sParam=\"a\tb\r\nc&#10;d&amp;&quot;\"/>"
            "</Macro><Macro name=\"Unsafe\"><Action type=\"1\" message=\"2170\" sParam=\"partial\"/>"
            "<Action type=\"2\" message=\"42000\" wParam=\"0\" lParam=\"0\" sParam=\"\"/>"
            "</Macro></Macros></NotepadPlus>";
        const auto importedMacros = importMacros(shortcutFixture);
        check(importedMacros.size() == 2 && importedMacros[0].error.isEmpty()
            && !importedMacros[1].error.isEmpty() && importedMacros[1].steps.isEmpty(),
            "Unsupported imported actions must reject the entire selected macro, not other macros.");
        check(importedMacros[0].steps[0].toObject()["text"] == "a\tb\r\nc\nd&\"",
            "Shortcut import normalized literal attribute whitespace or lost XML escapes.");
        const auto beforeImportedMacro = text(automationPane);
        macroSteps = importedMacros[0].steps; macroName = importedMacros[0].name;
        check(text(automationPane) == beforeImportedMacro, "Macro import executed automatically.");
        automationPane->send(SCI_DOCUMENTEND);
        playMacro();
        check(text(automationPane) == beforeImportedMacro + "a\tb\r\nc\nd&\"", "Imported macro replay changed text semantics.");
        dispatch("undo");
        check(text(automationPane) == beforeImportedMacro, "Imported macro was not one undo action.");
        automationPane->send(SCI_CLEARALL);
        automationPane->sends(SCI_ADDTEXT, 5, "    a");
        automationPane->send(SCI_DOCUMENTEND);
        dispatch("macro_start");
        bool unsupportedRecordingAction = false;
        try { dispatch("column_editor"); } catch (const std::exception&) { unsupportedRecordingAction = true; }
        check(unsupportedRecordingAction && macroRecording, "Unsupported UI operations were silently omitted from recording.");
        QTest::keyClick(automationPane, Qt::Key_Return);
        QTest::keyClicks(automationPane, "z");
        check(macroRecording, "Automatic indentation stopped the macro recorder.");
        dispatch("macro_stop");
        const auto indentedRecording = text(automationPane);
        check(indentedRecording.endsWith("    z"), "Recorded newline was not auto-indented.");
        automationPane->send(SCI_CLEARALL);
        automationPane->sends(SCI_ADDTEXT, 5, "    a");
        automationPane->send(SCI_DOCUMENTEND);
        playMacro();
        check(text(automationPane) == indentedRecording, "Auto-indentation was not preserved by macro replay.");
        dispatch("undo");
        check(text(automationPane) == "    a", "Indented macro replay did not share one undo group.");
        const QByteArray macroBudgetFixture(4 * 1024 * 1024, 'a');
        automationPane->send(SCI_CLEARALL);
        automationPane->sends(SCI_ADDTEXT, macroBudgetFixture.size(), macroBudgetFixture.constData());
        macroSteps = QJsonArray();
        for (int repeat = 0; repeat < 32; ++repeat)
            macroSteps.append(QJsonObject{{"kind", "Command"}, {"command", "duplicate_line"}});
        bool stoppedAtBudget = false;
        try { playMacro(); } catch (const std::exception&) { stoppedAtBudget = true; }
        if (automationPane->send(SCI_GETUNDOSEQUENCE) != 0)
            throw std::runtime_error(QString("Macro left undo nesting at %1").arg(automationPane->send(SCI_GETUNDOSEQUENCE)).toStdString());
        check(stoppedAtBudget && automationPane->send(SCI_GETLENGTH) <= 32 * 1024 * 1024,
            "Macro commands bypassed the editable document size limit.");
        dispatch("undo");
        check(text(automationPane) == macroBudgetFixture, "Interrupted macro failed to close its undo group.");
        automationPane->send(SCI_CLEARALL);
        automationPane->sends(SCI_ADDTEXT, beforeImportedMacro.size(), beforeImportedMacro.constData());
        for (const auto& invalidXml : {
            QByteArray("<!DOCTYPE NotepadPlus [<!ENTITY x SYSTEM 'file:///not-allowed'>]><NotepadPlus><Macros/></NotepadPlus>"),
            QByteArray("<NotepadPlus><Macros><Macro name='x'></Macros></NotepadPlus>"),
            QByteArray("<NotepadPlus><Macros/><Macros/></NotepadPlus>"),
            QByteArray(2 * 1024 * 1024 + 1, 'x')}) {
            bool rejected = false;
            try { importMacros(invalidXml); } catch (const std::exception&) { rejected = true; }
            check(rejected, "Unsafe, malformed or oversized shortcut input was accepted.");
        }
        automationPane->send(SCI_SELECTALL);
        const auto beforeEncoding = text(automationPane);
        utility("base64_encode");
        utility("base64_decode");
        check(text(automationPane) == beforeEncoding, "Base64 text round trip changed the document.");
        QPrinter pdf(QPrinter::HighResolution);
        pdf.setOutputFormat(QPrinter::PdfFormat);
        const auto pdfPath = testDirectory->filePath("print-test.pdf");
        pdf.setOutputFileName(pdfPath);
        QTextDocument printed;
        printed.setPlainText("Notepad Star print smoke test");
        printed.print(&pdf);
        QFile pdfFile(pdfPath);
        check(pdfFile.open(QIODevice::ReadOnly) && pdfFile.read(4) == "%PDF", "PDF print backend failed.");
        const auto canonicalTestDirectory = QFileInfo(testDirectory->path()).canonicalFilePath();
        check(!canonicalTestDirectory.isEmpty(), "Cannot resolve the private test directory.");
        const auto example = qs(create_extension_example(filePath(QDir(canonicalTestDirectory).filePath("uppercase-extension")))).toUtf8();
        automationPane->send(SCI_SETSEL, 0, 3);
        executeExtension(example);
        waitForSearch();
        check(text(automationPane).startsWith("ABC"), "Isolated Wasm extension did not apply its edit proposal.");
        dispatch("undo");
        if (!text(automationPane).startsWith("abc"))
            throw std::runtime_error(QString("Extension undo failed: target=%1 active=%2 readonly=%3 canUndo=%4 depth=%5")
                .arg(QString::fromUtf8(text(automationPane).left(30)), QString::fromUtf8(text(activeEditor()).left(30)))
                .arg(activeEditor()->send(SCI_GETREADONLY)).arg(activeEditor()->send(SCI_CANUNDO))
                .arg(activeEditor()->send(SCI_GETUNDOSEQUENCE)).toStdString());
        dispatch("manage_extensions");
        waitForManagement();
        check(extensionRootReady && extensionList->count() == 0, "Managed extension root was not initialized empty.");
        QJsonObject packageReview;
        managementJob(QJsonObject{{"operation", "inspect"}, {"source", QJsonDocument::fromJson(example).object()["directory"]}},
            "review", [&packageReview](const QJsonObject& result) { packageReview = result["review"].toObject(); });
        waitForManagement();
        QString managedId;
        managementJob(installationAction(packageReview), "installation", [&managedId](const QJsonObject& result) {
            const auto package = result["result"].toObject()["package"].toObject();
            check(!package["enabled"].toBool(), "Installing a package enabled it without approval.");
            managedId = package["id"].toString();
        });
        waitForManagement();
        refreshExtensions(); waitForManagement();
        auto* managedRunButton = extensionDock->findChild<QPushButton*>("extensions-run");
        check(extensionList->count() == 1 && managedRunButton && !managedRunButton->isEnabled(),
            "Disabled installed packages must not have a runnable UI action.");
        auto enabledAction = managementAction("set_enabled");
        enabledAction["id"] = managedId; enabledAction["enabled"] = true;
        managementJob(enabledAction, "enabled", [this](const QJsonObject&) { refreshExtensions(); });
        waitForManagement();
        check(managedRunButton->isEnabled(), "Explicit enabling did not update the extension manager.");
        automationPane->send(SCI_SETSEL, 0, 3);
        QMessageBox::StandardButton extensionAnswer = QMessageBox::Cancel;
        int permissionPrompts = 0;
        bool permissionPlainText = true;
        QTimer answerExtensionPermission;
        answerExtensionPermission.setInterval(20);
        connect(&answerExtensionPermission, &QTimer::timeout, this, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* message = qobject_cast<QMessageBox*>(widget);
                if (!message || !message->isVisible() || message->windowTitle() != "Allow Selected-Text Extension") continue;
                permissionPlainText = permissionPlainText && message->textFormat() == Qt::PlainText;
                if (auto* button = message->button(extensionAnswer)) { ++permissionPrompts; button->click(); }
                break;
            }
        });
        answerExtensionPermission.start();
        QTest::mouseClick(managedRunButton, Qt::LeftButton);
        check(permissionPrompts == 1 && !activeSearch && text(automationPane).startsWith("abc"),
            "Declining managed execution still ran or changed the document.");
        extensionAnswer = QMessageBox::Yes;
        QTest::mouseClick(managedRunButton, Qt::LeftButton);
        answerExtensionPermission.stop();
        check(permissionPrompts == 2, "Managed execution did not request per-run permission.");
        check(permissionPlainText, "Package permission metadata was interpreted as markup.");
        waitForSearch();
        check(text(automationPane).startsWith("ABC"), "Managed worker did not apply an enabled extension.");
        dispatch("undo");
        check(text(automationPane).startsWith("abc"), "Managed extension proposal was not undoable.");
        const auto updatedSource = QDir(canonicalTestDirectory).filePath("managed-update-source");
        create_extension_example(filePath(updatedSource));
        const auto updatedManifestPath = QDir(updatedSource).filePath("notepad-star-extension.json");
        {
            QFile manifestFile(updatedManifestPath);
            check(manifestFile.open(QIODevice::ReadOnly), "Cannot read managed-update fixture.");
            auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
            manifestFile.close();
            manifest["name"] = "Updated uppercase fixture";
            const auto encoded = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
            QSaveFile updatedManifest(updatedManifestPath);
            check(updatedManifest.open(QIODevice::WriteOnly) && updatedManifest.write(encoded) == encoded.size()
                && updatedManifest.commit(), "Cannot write managed-update fixture.");
        }
        managementJob(QJsonObject{{"operation", "inspect"}, {"source", nativePathJson(updatedSource)}}, "review",
            [&packageReview](const QJsonObject& result) { packageReview = result["review"].toObject(); });
        waitForManagement();
        QString updatedId;
        managementJob(installationAction(packageReview, managedId), "installation", [&updatedId](const QJsonObject& result) {
            const auto package = result["result"].toObject()["package"].toObject();
            updatedId = package["id"].toString();
            check(!package["enabled"].toBool(), "An updated package inherited an execution-enabled state.");
        });
        waitForManagement();
        refreshExtensions(); waitForManagement();
        check(updatedId != managedId && extensionList->count() == 2, "Update replaced an immutable installed version.");
        enabledAction["enabled"] = false;
        managementJob(enabledAction, "enabled", [this](const QJsonObject&) { refreshExtensions(); });
        waitForManagement();
        check(!managedRunButton->isEnabled(), "Disabling did not remove the managed run action.");
        for (const auto& installedId : {managedId, updatedId}) {
            auto action = managementAction("uninstall"); action["id"] = installedId;
            managementJob(action, "uninstalled", [this](const QJsonObject&) { refreshExtensions(); });
            waitForManagement();
        }
        check(extensionList->count() == 0 && QFileInfo(extensionRoot).isDir() &&
            QFileInfo(updatedManifestPath).isFile(), "Uninstall removed the root/source package or retained ready versions.");
        extensionDock->hide();
        ensureResults();
        searchResults->clear();
        query->setText("external");
        fileSearchRequest = qs(search_request(rust::Str(), rs(query->text().toUtf8()), rust::Str(), searchOptions(false))).toUtf8();
        fileQueue = {fileName};
        fileSearchCount = fileResultCount = 0;
        fileSearchRunning = true;
        nextSearchFile();
        waitForSearch();
        check(fileSearchCount == 1 && fileResultCount == 1 && searchResults->topLevelItemCount() == 1,
            "Search-in-files worker did not return the expected disk match.");
        const auto legacySearchPath = testDirectory->filePath("legacy-search.txt");
        {
            QFile legacy(legacySearchPath);
            check(legacy.open(QIODevice::WriteOnly) && legacy.write("caf\xe9", 4) == 4, "Cannot create legacy search fixture.");
        }
        searchResults->clear();
        fileSearchRequest = qs(search_request(rust::Str(), rs(QByteArray("\xc3\xa9")), rust::Str(), searchOptions(false))).toUtf8();
        fileSearchRequest = qs(search_file_encoding(rs(fileSearchRequest), "ISO 8859-1")).toUtf8();
        fileQueue = {legacySearchPath};
        fileSearchCount = fileResultCount = 0;
        fileSearchRunning = true;
        nextSearchFile(); waitForSearch();
        check(fileResultCount == 1 && searchResults->topLevelItemCount() == 1, "Legacy disk search used a guessed/default encoding.");
        auto* legacyResult = searchResults->topLevelItem(0);
        check(legacyResult->data(0, Qt::UserRole + 4).toString() == "ISO 8859-1", "Search results lost their source encoding.");
        searchResults->itemDoubleClicked(legacyResult, 0);
        check(QFileInfo(pathText(document_path(*controller, current().id))).canonicalFilePath() ==
            QFileInfo(legacySearchPath).canonicalFilePath() &&
            activeEditor()->send(SCI_GETSELECTIONSTART) == 3 && activeEditor()->send(SCI_GETSELECTIONEND) == 5,
            "Legacy search navigation did not use decoded UTF-8 offsets.");
        closeTab(tabs->currentIndex());
        const auto legacySource = read_search_file(filePath(legacySearchPath), "ISO 8859-1");
        rust::Vec<DiskProposal> legacyEdits;
        DiskProposal legacyEdit;
        legacyEdit.path = filePath(legacySearchPath);
        legacyEdit.stamp = legacySource.stamp;
        legacyEdit.encoding = legacySource.encoding;
        legacyEdit.text = "th\xc3\xa9";
        legacyEdits.push_back(std::move(legacyEdit));
        const auto legacyBackup = testDirectory->filePath("legacy-replacement-backup");
        const auto legacyBatch = qs(disk_request(*controller, std::move(legacyEdits), filePath(legacyBackup))).toUtf8();
        executeDiskOperation(legacyBatch, legacyBackup, false); waitForSearch();
        {
            QFile changed(legacySearchPath);
            check(changed.open(QIODevice::ReadOnly) && changed.readAll() == QByteArray("th\xe9"),
                "Disk replacement silently converted a legacy file.");
        }
        executeDiskOperation(qs(extension_inspection_request(filePath(legacyBackup))).toUtf8(), legacyBackup, true);
        waitForSearch();
        {
            QFile restoredLegacy(legacySearchPath);
            check(restoredLegacy.open(QIODevice::ReadOnly) && restoredLegacy.readAll() == QByteArray("caf\xe9"),
                "Legacy disk backup did not restore exact original bytes.");
        }
        check(catalog->languages().size() >= 90, "Bundled language catalog is incomplete.");
        check(catalog->language("C++") && catalog->language("C++")->engine == "cpp", "C++ language alias is incorrect.");
        check(catalog->detectFileName("example.ini") && catalog->detectFileName("example.ini")->engine == "props",
            "INI language did not select the real properties lexer.");
        QString apiError;
        const auto api = catalog->completions("python", &apiError);
        check(api && !api->entries.isEmpty(), "Python completion asset was not embedded.");
        QFile udl(":/notepad-star/languages/PowerEditor/bin/userDefineLangs/markdown._preinstalled.udl.xml");
        check(udl.open(QIODevice::ReadOnly), "Embedded UDL example is unavailable.");
        const auto udlBytes = udl.readAll();
        applyUdl(current(), udlBytes);
        check(current().lexer == "user" && current().udlProfile != 0, "UDL did not configure the real user lexer.");
        const auto profileId = current().udlProfile;
        restyle(current());
        check(current().udlProfile == profileId, "Restyling must preserve UDL cache identity.");
        const auto udlSession = filePath(testDirectory->filePath("udl-session.json"));
        save_session(*controller, snapshots(), udlSession, dark, wrap);
        auto udlController = create_controller();
        const auto restoredUdl = load_session(*udlController, udlSession);
        check(!restoredUdl.empty() && qs(restoredUdl[0].view).contains("udl_xml_base64"),
            "Session did not preserve the imported UDL definition.");
        const QList<StyledRun> exportedRuns{{QString::fromUtf8("<script> & {\\} \xce\xb2\r\n"), QColor("#112233"), QColor("#ffffff"), true, false, false}};
        const auto html = exportHtml(exportedRuns, 11, 4);
        check(html.contains("&lt;script&gt;") && !html.contains("<script>") && html.contains("#112233"),
            "HTML export did not preserve colors and escape text.");
        const auto rtf = exportRtf(exportedRuns, 11, 4);
        check(rtf.startsWith("{\\rtf1") && rtf.contains("\\u946?") && rtf.contains("\\{"), "RTF export did not encode Unicode or braces.");
        applyLexer(current(), "rust");
        const auto backup = testDirectory->filePath("replacement-backup");
        const auto original = read_search_file(filePath(fileName), rust::Str());
        rust::Vec<DiskProposal> proposals;
        DiskProposal proposal;
        proposal.path = filePath(fileName);
        proposal.stamp = original.stamp;
        proposal.encoding = original.encoding;
        proposal.text = "batch updated\n";
        proposals.push_back(std::move(proposal));
        const auto batch = qs(disk_request(*controller, std::move(proposals), filePath(backup))).toUtf8();
        executeDiskOperation(batch, backup, false);
        waitForSearch();
        {
            QFile changed(fileName);
            check(changed.open(QIODevice::ReadOnly) && changed.readAll() == "batch updated\n",
                "Isolated disk replacement did not write the confirmed proposal.");
        }
        const auto restore = qs(extension_inspection_request(filePath(backup))).toUtf8();
        executeDiskOperation(restore, backup, true);
        waitForSearch();
        {
            QFile restoredFile(fileName);
            check(restoredFile.open(QIODevice::ReadOnly) && restoredFile.readAll() == "external",
                "Guarded disk backup restoration failed.");
        }
        dispatch("document_map");
        check(documentMap && documentMap->send(SCI_GETDOCPOINTER) == current().primary->send(SCI_GETDOCPOINTER),
            "Document map is not bound to the active document.");
        mapDock->hide();
        check(documentMap->send(SCI_GETDOCPOINTER) != current().primary->send(SCI_GETDOCPOINTER),
            "A hidden document map retained the live editor buffer.");
        mapDock->show();
        check(documentMap->send(SCI_GETDOCPOINTER) == current().primary->send(SCI_GETDOCPOINTER),
            "Showing the document map did not rebind the active buffer.");
        const auto beforeMapInput = text(current().primary);
        QTest::keyClicks(documentMap, "ignored");
        check(text(current().primary) == beforeMapInput && current().primary->send(SCI_GETREADONLY) == 0,
            "Document-map input changed text or locked the primary document.");
        ProjectPanel project("Project test", this);
        const auto workspacePath = testDirectory->filePath("workspace.xml");
        const QByteArray workspaceXml = "<NotepadPlus><Project name=\"Example\"><Folder name=\"Source\"><File name=\"unicode.txt\"/></Folder></Project></NotepadPlus>";
        project.importXml(workspaceXml, workspacePath);
        check(project.tree()->topLevelItemCount() == 1 && project.tree()->topLevelItem(0)->childCount() == 1,
            "Project workspace structure was not imported.");
        const auto exportedWorkspace = project.exportXml(workspacePath);
        check(exportedWorkspace.contains("name=\"unicode.txt\""), "Project paths did not remain relative on export.");
        bool rejected = false;
        try { project.importXml("<!DOCTYPE NotepadPlus [<!ENTITY x SYSTEM 'file:///missing'>]><NotepadPlus/>", workspacePath); }
        catch (const std::exception&) { rejected = true; }
        check(rejected && project.tree()->topLevelItemCount() == 1, "Malformed workspace changed the loaded project.");
        openFile(fileName);
        dispatch("monitoring");
        {
            QFile tail(fileName);
            check(tail.open(QIODevice::Append) && tail.write(".tail") == 5, "Cannot update monitoring fixture.");
        }
        QElapsedTimer monitorWait;
        monitorWait.start();
        while (!text(current().primary).endsWith(".tail") && monitorWait.elapsed() < 4000) {
            QApplication::processEvents();
            QTest::qWait(20);
        }
        check(text(current().primary).endsWith(".tail") && current().primary->send(SCI_GETREADONLY) != 0,
            "Read-only file monitoring failed to follow an append.");
        dispatch("monitoring");
        const auto beforeRename = text(current().primary);
        renameView(current(), "renamed-monitor.txt");
        check(pathText(document_path(*controller, current().id)).endsWith("renamed-monitor.txt") &&
            text(current().primary) == beforeRename && !QFileInfo::exists(fileName),
            "Rename did not preserve the document or move its file.");
        closeTab(tabs->currentIndex());
        addDocument(false);
        applyLexer(current(), "python");
        const QByteArray functionSource = "def alpha():\n    pass\nclass Beta:\n    def gamma(self):\n        pass\n";
        current().primary->sends(SCI_ADDTEXT, functionSource.size(), functionSource.constData());
        showFunctions();
        requestFunctions();
        QElapsedTimer outlineWait;
        outlineWait.start();
        while ((outlineTask || functionTimer->isActive()) && outlineWait.elapsed() < 8000) {
            QApplication::processEvents();
            QTest::qWait(10);
        }
        if (outlineTask || functionTimer->isActive() || !outlineError.isEmpty())
            throw std::runtime_error(("Function-list worker failed or timed out: " + outlineError).toStdString());
        const auto alpha = functionTree->findItems("alpha", Qt::MatchContains | Qt::MatchRecursive);
        const auto gamma = functionTree->findItems("gamma", Qt::MatchContains | Qt::MatchRecursive);
        check(!alpha.isEmpty() && !gamma.isEmpty(), "Function list omitted Python functions.");
        functionTree->itemActivated(alpha.first(), 0);
        check(current().primary->send(SCI_LINEFROMPOSITION, current().primary->send(SCI_GETCURRENTPOS)) == 0,
            "Function-list navigation selected the wrong source line.");
        functionDock->hide();
        current().primary->send(SCI_SETSAVEPOINT);
        closeTab(tabs->currentIndex());
        for (const auto& view : views) view.primary->send(SCI_SETSAVEPOINT);
        addDocument(false);
        const auto pinnedId = current().id;
        dispatch("pin_tab");
        check(current().id == pinnedId && tabs->currentIndex() == 0 && document_tab(*controller, pinnedId).pinned,
            "Pinning did not preserve the active document or pinned-prefix order.");
        addDocument(false);
        closeCollection("close_unpinned");
        check(tabs->count() == 1 && current().id == pinnedId, "Close Unpinned removed the pinned tab.");
        dispatch("pin_tab");
        closeTab(tabs->currentIndex());
        addDocument(false);
        current().primary->sends(SCI_ADDTEXT, 1, "a");
        addDocument(false);
        current().primary->sends(SCI_ADDTEXT, 1, "b");
        const int beforeCancelledClose = tabs->count();
        const auto beforeCancelledHistory = closedFiles.size();
        int prompts = 0;
        QTimer answerClosePrompts;
        answerClosePrompts.setInterval(20);
        connect(&answerClosePrompts, &QTimer::timeout, this, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* message = qobject_cast<QMessageBox*>(widget);
                if (!message || !message->isVisible()) continue;
                const auto answer = prompts++ == 0 ? QMessageBox::Discard : QMessageBox::Cancel;
                if (auto* button = message->button(answer)) button->click();
                break;
            }
        });
        answerClosePrompts.start();
        closeCollection("close_all");
        answerClosePrompts.stop();
        check(prompts >= 2 && tabs->count() == beforeCancelledClose, "Cancelling a later close prompt must retain every tab.");
        check(closedFiles.size() == beforeCancelledHistory, "A cancelled close entered the reopen history.");
        for (const auto& view : views) view.primary->send(SCI_SETSAVEPOINT);
        closeCollection("close_all");
        const auto largePath = testDirectory->filePath("large-preview.txt");
        current().primary->sends(SCI_ADDTEXT, 8, "ab\nc\nxyz");
        applyColumnValues(current().primary, {{2, 2, 0}, {4, 4, 1}, {7, 7, 0}}, {"0", "1", "2"});
        check(text(current().primary) == "ab0\nc 1\nxy2z", "Column insertion did not respect per-line padding.");
        current().primary->send(SCI_UNDO);
        check(text(current().primary) == "ab\nc\nxyz", "Column insertion did not undo atomically.");
        ColumnOptions numberOptions;
        numberOptions.start = "10"; numberOptions.step = "-2"; numberOptions.rows = 3; numberOptions.repeat = 1;
        numberOptions.base = "decimal"; numberOptions.padding = "zero"; numberOptions.width = 3;
        const auto numbers = column_values(numberOptions);
        check(numbers.size() == 3 && qs(numbers[0]) == "010" && qs(numbers[2]) == "006", "Column generator returned wrong values.");
        {
            QFile large(largePath);
            check(large.open(QIODevice::WriteOnly) && large.resize(1024LL * 1024 * 1024) &&
                large.seek(1024LL * 1024 * 1024 - 4) && large.write("end\n") == 4, "Cannot create large-file fixture.");
        }
        check(needs_large_preview(*controller, filePath(largePath)), "Large file was not routed to bounded preview.");
        {
            auto preview = std::make_unique<LargeFileWindow>(largePath, this);
            preview->setAttribute(Qt::WA_DeleteOnClose, false);
            preview->load(1024ULL * 1024 * 1024 - 4);
            check(preview->pane()->send(SCI_GETREADONLY) != 0 && text(preview->pane()) == "end\n",
                "Large-file preview did not render a bounded read-only end page.");
        }
        auto* commentPane = current().primary;
        applyLexer(current(), "cpp");
        commentPane->send(SCI_CLEARSELECTIONS);
        commentPane->send(SCI_CLEARALL);
        const QByteArray commentFixture("\talpha\r\nbeta\n\n");
        commentPane->sends(SCI_ADDTEXT, commentFixture.size(), commentFixture.constData());
        commentPane->send(SCI_SELECTALL);
        dispatch("toggle_line_comment");
        check(text(commentPane) == "\t// alpha\r\n// beta\n\n", "Language-aware line comments changed whitespace/EOL semantics.");
        dispatch("toggle_line_comment");
        check(text(commentPane) == commentFixture, "Line-comment toggle was not reversible.");
        dispatch("undo");
        check(text(commentPane) == "\t// alpha\r\n// beta\n\n", "Comment operation was not one undo action.");
        dispatch("undo");
        commentPane->send(SCI_SETSEL, 1, 6);
        dispatch("block_comment");
        check(text(commentPane).startsWith("\t/* alpha */"), "Block-comment delimiters were not read from the language catalog.");
        dispatch("block_uncomment");
        check(text(commentPane) == commentFixture, "Selected comment content was not unwrapped safely.");
        commentPane->send(SCI_CLEARALL);
        const QByteArray braceFixture("a(\xe6\x97\xa5[1])z");
        commentPane->sends(SCI_ADDTEXT, braceFixture.size(), braceFixture.constData());
        commentPane->send(SCI_SETSEL, 1, 1);
        dispatch("matching_brace");
        check(commentPane->send(SCI_GETCURRENTPOS) == 8, "Brace navigation used character indexes instead of UTF-8 offsets.");
        dispatch("select_braces");
        check(commentPane->send(SCI_GETSELECTIONSTART) == 5 && commentPane->send(SCI_GETSELECTIONEND) == 8,
            "Brace navigation did not prioritize the character before the caret.");
        commentPane->send(SCI_SETSEL, 9, 9);
        dispatch("select_braces");
        check(commentPane->send(SCI_GETSELECTIONSTART) == 1 && commentPane->send(SCI_GETSELECTIONEND) == 9,
            "Brace selection omitted a delimiter.");
        applyUdl(current(), udlBytes);
        commentPane->send(SCI_CLEARALL);
        commentPane->sends(SCI_ADDTEXT, 5, "title");
        commentPane->send(SCI_SELECTALL);
        dispatch("toggle_line_comment");
        check(text(commentPane) == "# title", "UDL line-comment syntax was not used.");
        dispatch("undo");
        check(text(commentPane) == "title", "UDL commenting was not undoable.");
        commentPane->send(SCI_CLEARALL);
        commentPane->sends(SCI_ADDTEXT, 6, "a\r\nb\nc");
        commentPane->send(SCI_MARKERDELETEALL, 0);
        commentPane->send(SCI_MARKERADD, 0, 0);
        commentPane->send(SCI_MARKERADD, 2, 0);
        check(bookmarkedText() == "a\r\nc", "Bookmarked copy reordered text or normalized EOLs.");
        editBookmarks("paste_bookmarks", "X\nY");
        check(text(commentPane) == "X\nY\r\nb\nX\nY", "Bookmarked paste did not preserve original line endings.");
        dispatch("undo");
        check(text(commentPane) == "a\r\nb\nc", "Bookmarked paste was not one undo action.");
        commentPane->send(SCI_MARKERDELETEALL, 0);
        commentPane->send(SCI_MARKERADD, 0, 0);
        commentPane->send(SCI_MARKERADD, 2, 0);
        dispatch("delete_bookmarks");
        check(text(commentPane) == "b\n", "Removing bookmarked lines did not include their EOLs.");
        dispatch("undo");
        check(text(commentPane) == "a\r\nb\nc", "Bookmarked deletion was not undoable.");
        commentPane->send(SCI_MARKERDELETEALL, 0);
        commentPane->send(SCI_MARKERADD, 1, 0);
        dispatch("invert_bookmarks");
        check((commentPane->send(SCI_MARKERGET, 0) & 1) && !(commentPane->send(SCI_MARKERGET, 1) & 1)
            && (commentPane->send(SCI_MARKERGET, 2) & 1), "Bookmark inversion changed the wrong marker set.");
        dispatch("delete_unmarked");
        check(text(commentPane) == "a\r\nc", "Removing unmarked lines removed bookmarked text.");
        dispatch("undo");
        check(text(commentPane) == "a\r\nb\nc", "Unmarked deletion was not undoable.");
        commentPane->send(SCI_MARKERDELETEALL, 0);
        commentPane->send(SCI_MARKERADD, 0, 0);
        commentPane->send(SCI_MARKERADD, 2, 0);
        const auto bookmarkSession = filePath(testDirectory->filePath("bookmarks-session.json"));
        save_session(*controller, snapshots(), bookmarkSession, dark, wrap);
        const auto beforeBookmarkRestore = tabs->count();
        restoreViews(load_session(*controller, bookmarkSession));
        check(tabs->count() == beforeBookmarkRestore + 1 && (activeEditor()->send(SCI_MARKERGET, 0) & 1) &&
            (activeEditor()->send(SCI_MARKERGET, 2) & 1), "Session restore lost document bookmarks.");
        closeTab(tabs->currentIndex(), true);
        const QByteArray configurationFixture =
            "<NotepadPlus><GUIConfigs>"
            "<GUIConfig name='TabSetting' replaceBySpace='yes' size='8' backspaceUnindent='yes'/>"
            "<GUIConfig name='Caret' width='2' blinkRate='0'/>"
            "<GUIConfig name='RememberLastSession'>no</GUIConfig>"
            "<GUIConfig name='MaintainIndent'>2</GUIConfig>"
            "<GUIConfig name='auto-completion' autoCAction='3' triggerFromNbChar='3' autoCIgnoreNumbers='yes' funcParams='no'/>"
            "<GUIConfig name='ScintillaPrimaryView' lineNumberMargin='hide' bookMarkMargin='hide' indentGuideLine='hide' "
            "whiteSpaceShow='show' eolShow='show' Wrap='yes' virtualSpace='yes' scrollBeyondLastLine='yes' lineWrapMethod='aligned'/>"
            "<GUIConfig name='DarkMode' enable='yes' customColorText='123'/>"
            "<GUIConfig name='UnrecognizedSetting'/>"
            "</GUIConfigs><UserDefinedCommands><Command name='untrusted'>not executed</Command></UserDefinedCommands></NotepadPlus>";
        const auto beforePreferences = qs(current_settings(*controller)).toUtf8();
        QByteArray originalProfile;
        {
            QFile profile(settingsPath);
            check(profile.open(QIODevice::ReadOnly), "Cannot read the settings fixture.");
            originalProfile = profile.readAll();
        }
        const auto configuration = importNotepadConfig(configurationFixture);
        check(configuration.unsupported.join('\n').contains("customColorText") &&
            configuration.unsupported.join('\n').contains("UserDefinedCommands") &&
            configuration.unsupported.join('\n').contains("UnrecognizedSetting"), "Unsupported configuration was silently dropped.");
        const auto importedFont = importNotepadConfig(
            "<NotepadPlus><GlobalStyles><WidgetStyle name='Default Style' styleID='32' "
            "fontName='Consolas' fontSize='13' fgColor='112233'/></GlobalStyles></NotepadPlus>");
        check(importedFont.patch["font_family"] == "Consolas" && importedFont.patch["font_size"].toInt() == 13 &&
            importedFont.unsupported.join('\n').contains("fgColor"), "Font import lost values or silently claimed color migration.");
        const auto migrated = mergeConfigPatch(QJsonDocument::fromJson(beforePreferences).object(), configuration.patch);
        const auto migratedJson = QJsonDocument(migrated).toJson(QJsonDocument::Compact);
        validate_settings_json(rs(migratedJson));
        check(qs(current_settings(*controller)).toUtf8() == beforePreferences, "Import preview changed live settings.");
        const auto preferenceBackup = import_settings(*controller, rs(migratedJson));
        {
            QFile backupProfile(pathText(preferenceBackup));
            check(backupProfile.open(QIODevice::ReadOnly) && backupProfile.readAll() == originalProfile,
                "Preference import did not preserve exact previous settings.");
        }
        options = migrated;
        applyOptions();
        for (auto* pane : {current().primary, current().clone}) {
            check(pane->send(SCI_GETTABWIDTH) == 8 && pane->send(SCI_GETUSETABS) == 0 &&
                pane->send(SCI_GETMARGINWIDTHN, 0) == 0 && pane->send(SCI_GETMARGINWIDTHN, 1) == 0 &&
                pane->send(SCI_GETVIEWWS) != SCWS_INVISIBLE && pane->send(SCI_GETVIEWEOL) != 0 &&
                pane->send(SCI_GETCARETWIDTH) == 2 && pane->send(SCI_GETCARETPERIOD) == 0 &&
                pane->send(SCI_GETWRAPINDENTMODE) == SC_WRAPINDENT_SAME,
                "Imported display preferences did not reach both editor views.");
        }
        addDocument(false);
        check(activeEditor()->send(SCI_GETTABWIDTH) == 8 && activeEditor()->send(SCI_GETMARGINWIDTHN, 0) == 0,
            "New documents did not inherit imported preferences.");
        closeTab(tabs->currentIndex());
        for (const auto& rejectedConfig : {
            QByteArray("<!DOCTYPE NotepadPlus [<!ENTITY e SYSTEM 'file:///no'>]><NotepadPlus/>"),
            QByteArray("<NotepadPlus><GUIConfigs><GUIConfig name='TabSetting' size='oops'/></GUIConfigs></NotepadPlus>"),
            QByteArray("<NotepadPlus><GUIConfigs><GUIConfig name='RememberLastSession'>yes</GUIConfig>"
                "<GUIConfig name='RememberLastSession'>no</GUIConfig></GUIConfigs></NotepadPlus>")}) {
            bool rejectedImport = false;
            try { importNotepadConfig(rejectedConfig); } catch (const std::exception&) { rejectedImport = true; }
            check(rejectedImport, "Invalid or ambiguous configuration XML was accepted.");
        }
        store_settings(*controller, rs(beforePreferences));
        options = QJsonDocument::fromJson(beforePreferences).object();
        applyOptions();
        {
            QFile brokenProfile(settingsPath);
            check(brokenProfile.open(QIODevice::WriteOnly | QIODevice::Truncate) && brokenProfile.write("{broken") == 7,
                "Cannot create corrupt preferences fixture.");
        }
        const auto resetBackup = reset_settings(*controller, filePath(settingsPath));
        {
            QFile retainedProfile(pathText(resetBackup));
            check(retainedProfile.open(QIODevice::ReadOnly) && retainedProfile.readAll() == "{broken",
                "Reset discarded malformed preferences without retaining them.");
        }
        options = QJsonDocument::fromJson(qs(current_settings(*controller)).toUtf8()).object();
        applyOptions();
        check(current().primary->send(SCI_GETMARGINWIDTHN, 0) > 0 &&
            current().primary->send(SCI_GETMARGINWIDTHN, 1) == 10, "Reset preferences did not restore compact gutters.");
        store_settings(*controller, rs(beforePreferences));
        options = QJsonDocument::fromJson(beforePreferences).object();
        applyOptions();
        auto* layoutPane = activeEditor();
        layoutPane->send(SCI_CLEARSELECTIONS);
        layoutPane->send(SCI_CLEARALL);
        layoutPane->sends(SCI_ADDTEXT, 3, "abc");
        layoutPane->send(SCI_SETEOLMODE, SC_EOL_CRLF);
        layoutPane->send(SCI_SETSEL, 1, 2);
        dispatch("duplicate_selection");
        check(text(layoutPane) == "abbc", "Selection duplication duplicated an entire line.");
        dispatch("undo");
        layoutPane->send(SCI_SETSEL, 1, 2);
        dispatch("macro_start");
        dispatch("duplicate_selection");
        dispatch("macro_stop");
        check(macroJson().contains("duplicate_selection"), "Selection duplication was omitted from the macro tape.");
        layoutPane->send(SCI_CLEARALL);
        layoutPane->sends(SCI_ADDTEXT, 3, "abc");
        layoutPane->send(SCI_SETSEL, 1, 2);
        playMacro();
        check(text(layoutPane) == "abbc", "Recorded selection duplication did not replay its semantics.");
        dispatch("undo");
        dispatch("duplicate_line");
        check(text(layoutPane) == "abc\r\nabc", "The reference Duplicate Line command changed its line semantics.");
        dispatch("undo");
        layoutPane->send(SCI_DOCUMENTEND);
        dispatch("blank_line_below");
        check(text(layoutPane) == "abc\r\n" && layoutPane->send(SCI_GETCURRENTPOS) == 5,
            "Blank-line insertion below a final line lost its EOL/caret semantics.");
        dispatch("undo");
        dispatch("blank_line_above");
        check(text(layoutPane) == "\r\nabc", "Blank-line insertion above used the wrong document EOL.");
        dispatch("undo");
        layoutPane->send(SCI_CLEARALL);
        layoutPane->sends(SCI_ADDTEXT, 6, "a\r\nb\nc");
        layoutPane->send(SCI_SELECTALL);
        dispatch("join_lines");
        check(text(layoutPane) == "a b c", "Joining lines did not use Scintilla's spacing semantics.");
        dispatch("undo");
        check(text(layoutPane) == "a\r\nb\nc", "Line joining did not undo as one action.");
        const QByteArray splitFixture = QByteArray("word ").repeated(100);
        layoutPane->send(SCI_CLEARALL);
        layoutPane->sends(SCI_ADDTEXT, splitFixture.size(), splitFixture.constData());
        const auto priorSize = layoutPane->size();
        const auto priorWrap = layoutPane->send(SCI_GETWRAPMODE);
        layoutPane->resize(150, 200);
        layoutPane->send(SCI_SETWRAPMODE, SC_WRAP_NONE);
        layoutPane->send(SCI_SELECTALL);
        dispatch("split_lines");
        check(layoutPane->send(SCI_GETLINECOUNT) > 1, "Splitting lines did not use the visible editor width.");
        dispatch("undo");
        check(text(layoutPane) == splitFixture, "Line splitting did not undo as one action.");
        layoutPane->resize(priorSize);
        layoutPane->send(SCI_SETWRAPMODE, priorWrap);
        const auto completionFixture = QByteArray("alpha ").repeated(250) + "alpine al";
        layoutPane->send(SCI_CLEARALL);
        layoutPane->sends(SCI_ADDTEXT, completionFixture.size(), completionFixture.constData());
        layoutPane->send(SCI_DOCUMENTEND);
        completeWord(layoutPane, true);
        check(layoutPane->send(SCI_AUTOCACTIVE) != 0, "Document-word completion did not open.");
        layoutPane->sends(SCI_AUTOCSELECT, 0, "alpine");
        const auto completionLength = layoutPane->send(SCI_AUTOCGETCURRENTTEXT);
        check(completionLength > 0 && completionLength < 128, "Repeated words exhausted the unique completion budget.");
        QByteArray selectedCompletion(completionLength + 1, '\0');
        layoutPane->send(SCI_AUTOCGETCURRENTTEXT, 0, reinterpret_cast<sptr_t>(selectedCompletion.data()));
        selectedCompletion.resize(completionLength);
        check(selectedCompletion == "alpine", "Unique completion candidates were omitted after repeated words.");
        layoutPane->send(SCI_AUTOCCOMPLETE);
        check(text(layoutPane).endsWith("alpine"), "Selected word completion was not applied.");
        dispatch("undo");
        check(text(layoutPane) == completionFixture, "Word completion was not undoable.");
        const auto forwardedPath = testDirectory->filePath("forwarded.py");
        {
            QFile forwardedFile(forwardedPath);
            check(forwardedFile.open(QIODevice::WriteOnly) && forwardedFile.write("a\nbc\n") == 5, "Cannot create forwarding fixture.");
        }
        const auto instanceProfile = QDir(canonicalTestDirectory).filePath("instance-profile");
        InstanceChannel instance(instanceProfile);
        check(instance.primary(), "The first instance did not acquire its startup lock.");
        {
            InstanceChannel competing(instanceProfile);
            check(!competing.primary() && competing.endpoint() == instance.endpoint(), "A second instance stole the live startup lock.");
        }
        instance.setHandler([this](const QByteArray& payload) { return acceptForwarded(payload); });
        {
            QLocalSocket fragmented;
            fragmented.connectToServer(instance.endpoint());
            check(fragmented.waitForConnected(2000), "Cannot connect the framing fixture.");
            const QByteArray oversizedHeader("\0\x20\0\0", 4);
            check(fragmented.write(oversizedHeader.left(2)) == 2, "Cannot send a partial instance header.");
            fragmented.flush();
            QTest::qWait(30);
            check(fragmented.bytesAvailable() == 0, "An incomplete instance header was dispatched.");
            check(fragmented.write(oversizedHeader.mid(2)) == 2, "Cannot finish the oversized frame header.");
            fragmented.flush();
            QElapsedTimer frameWait; frameWait.start();
            QByteArray framedResponse;
            while (fragmented.state() != QLocalSocket::UnconnectedState && frameWait.elapsed() < 3000) {
                QTest::qWait(10);
                framedResponse += fragmented.readAll();
            }
            framedResponse += fragmented.readAll();
            check(!QJsonDocument::fromJson(framedResponse.mid(4)).object()["errors"].toArray().isEmpty(),
                "An oversized fragmented frame was not rejected.");
        }
        {
            QLocalSocket idle;
            idle.connectToServer(instance.endpoint());
            check(idle.waitForConnected(2000), "Cannot connect the idle-client fixture.");
            QElapsedTimer idleWait; idleWait.start();
            while (idle.state() != QLocalSocket::UnconnectedState && idleWait.elapsed() < 7500) QTest::qWait(20);
            check(idle.state() == QLocalSocket::UnconnectedState && idleWait.elapsed() >= 4900 && idleWait.elapsed() < 7500,
                "Idle local clients did not expire at the five-second request deadline.");
        }
        auto roundTrip = [&](const QByteArray& payload) {
            const auto envelope = QJsonDocument(QJsonObject{{"endpoint", instance.endpoint()}, {"payload", QString::fromUtf8(payload)}})
                .toJson(QJsonDocument::Compact);
            QByteArray reply;
            auto* client = new SearchTask(this);
            activeSearch = client;
            lastSearchError.clear();
            client->start(envelope, [this, client, &reply](QByteArray bytes) {
                activeSearch = nullptr; client->deleteLater(); reply = std::move(bytes);
            }, [this, client](const QString& error) {
                activeSearch = nullptr; client->deleteLater(); lastSearchError = error;
            }, "--instance-client-worker", 256 * 1024);
            waitForSearch();
            return reply;
        };
        LaunchSettings forwardedSettings{};
        forwardedSettings.line = 2; forwardedSettings.column = 2; forwardedSettings.language = "python";
        forwardedSettings.read_only = true;
        rust::Vec<FilePath> forwardedFiles;
        forwardedFiles.push_back(filePath(forwardedPath));
        const auto forwardPayload = qs(forward_request(forwardedSettings,
            rust::Slice<const FilePath>(forwardedFiles.data(), forwardedFiles.size()))).toUtf8();
        const auto beforeForwardTabs = tabs->count();
        const auto delivered = roundTrip(forwardPayload);
        accept_forward_response(rs(delivered));
        LaunchSettings pingSettings{};
        rust::Vec<FilePath> pingFiles;
        const auto pingPayload = qs(forward_request(pingSettings,
            rust::Slice<const FilePath>(pingFiles.data(), pingFiles.size()))).toUtf8();
        for (int attempt = 0; attempt < 16; ++attempt) accept_forward_response(rs(roundTrip(pingPayload)));
        check(tabs->count() == beforeForwardTabs + 1 && text(activeEditor()) == "a\nbc\n" &&
            activeEditor()->send(SCI_GETCURRENTPOS) == 3 && activeEditor()->send(SCI_GETREADONLY) != 0 &&
            current().languageId == "python", "Forwarding lost its file, language, position or read-only options.");
        activeEditor()->send(SCI_SETREADONLY, false);
        activeEditor()->sends(SCI_APPENDTEXT, 7, "unsaved");
        forwardedSettings.line = forwardedSettings.column = 0;
        forwardedSettings.language = ""; forwardedSettings.read_only = false;
        auto repeatPayload = qs(forward_request(forwardedSettings,
            rust::Slice<const FilePath>(forwardedFiles.data(), forwardedFiles.size()))).toUtf8();
        accept_forward_response(rs(roundTrip(repeatPayload)));
        check(tabs->count() == beforeForwardTabs + 1 && text(activeEditor()).endsWith("unsaved"),
            "Repeated forwarding discarded or duplicated an unsaved open buffer.");
        forwardedFiles.push_back(filePath(testDirectory->filePath("missing-forwarded.txt")));
        const auto partialPayload = qs(forward_request(forwardedSettings,
            rust::Slice<const FilePath>(forwardedFiles.data(), forwardedFiles.size()))).toUtf8();
        const auto partial = QJsonDocument::fromJson(roundTrip(partialPayload)).object();
        check(partial["opened"].toInt() == 1 && partial["errors"].toArray().size() == 1,
            "A partial forwarded open was reported as unconditional success.");
        dispatch("macro_start");
        const auto busyReply = QJsonDocument::fromJson(roundTrip(repeatPayload)).object();
        dispatch("macro_stop");
        check(busyReply["opened"].toInt() == 0 && !busyReply["errors"].toArray().isEmpty(),
            "Forwarding modified a window during macro recording.");
        closingWindow = true;
        const auto closingReply = QJsonDocument::fromJson(roundTrip(repeatPayload)).object();
        closingWindow = false;
        check(closingReply["opened"].toInt() == 0 && !closingReply["errors"].toArray().isEmpty(),
            "A closing window accepted new forwarded files after shutdown began.");
        const auto badReply = QJsonDocument::fromJson(roundTrip("{\"schema\":1,\"command\":\"run\"}")).object();
        check(badReply["opened"].toInt() == 0 && !badReply["errors"].toArray().isEmpty(),
            "The local endpoint accepted arbitrary commands.");
        closeTab(tabs->currentIndex(), true);
        instance.close();
        {
            InstanceChannel recovered(instanceProfile);
            check(recovered.primary(), "Closing a primary instance did not release its lock.");
        }
        const auto beforeOsOpen = tabs->count();
        QFileOpenEvent osOpen(forwardedPath);
        QApplication::sendEvent(QCoreApplication::instance(), &osOpen);
        check(tabs->count() == beforeOsOpen + 1 && text(activeEditor()) == "a\nbc\n",
            "Operating-system file-open events did not reach the document lifecycle.");
        closeTab(tabs->currentIndex());
#ifdef Q_OS_WIN
        const auto occupiedProfile = QDir(canonicalTestDirectory).filePath("occupied-instance");
        check(QDir().mkdir(occupiedProfile), "Cannot create the occupied-lock fixture.");
        const auto occupiedLock = QDir(occupiedProfile).filePath("shared-instance.lock");
        {
            QFile unrelated(occupiedLock);
            check(unrelated.open(QIODevice::WriteOnly) && unrelated.write("unrelated") == 9, "Cannot write occupied-lock fixture.");
        }
        bool retainedLock = false;
        try { InstanceChannel occupied(occupiedProfile); } catch (const std::exception&) { retainedLock = true; }
        {
            QFile unrelated(occupiedLock);
            check(retainedLock && unrelated.open(QIODevice::ReadOnly) && unrelated.readAll() == "unrelated",
                "An unrecognized startup-lock file was removed or adopted.");
        }
        const auto processProfile = QDir(canonicalTestDirectory).filePath("process-profile");
        QProcess primaryProcess;
        primaryProcess.start(QCoreApplication::applicationFilePath(), {"--reuse-instance", "--profile-dir", processProfile});
        check(primaryProcess.waitForStarted(3000), "Cannot launch an isolated primary instance.");
        QElapsedTimer primaryStartup; primaryStartup.start();
        while (!QFileInfo::exists(QDir(processProfile).filePath("shared-instance.lock")) &&
            primaryProcess.state() != QProcess::NotRunning && primaryStartup.elapsed() < 6000) QTest::qWait(20);
        check(primaryProcess.state() != QProcess::NotRunning, "The isolated primary failed during startup.");
        QProcess secondaryProcess;
        secondaryProcess.setWorkingDirectory(canonicalTestDirectory);
        secondaryProcess.start(QCoreApplication::applicationFilePath(),
            {"--reuse-instance", "--profile-dir", processProfile, "--line", "2", "--column", "2", "--language", "python", "-ro", "forwarded.py"});
        check(secondaryProcess.waitForStarted(3000) && secondaryProcess.waitForFinished(10000) &&
            secondaryProcess.exitStatus() == QProcess::NormalExit && secondaryProcess.exitCode() == 0,
            "The real secondary CLI did not hand off and exit successfully.");
        check(primaryProcess.state() != QProcess::NotRunning, "Forwarding terminated the primary instance.");
        primaryProcess.terminate();
        check(primaryProcess.waitForFinished(10000) && primaryProcess.exitStatus() == QProcess::NormalExit &&
            primaryProcess.exitCode() == 0, "The isolated primary did not close cleanly.");
        auto processController = create_controller();
        initialize_recovery(*processController, filePath(QDir(processProfile).filePath("recovery")), true);
        const auto processDocuments = resume_workspace(*processController);
        check(processDocuments.size() == 2 && qs(processDocuments[1].text) == "a\nbc\n" &&
            QJsonDocument::fromJson(qs(processDocuments[1].view).toUtf8()).object()["caret"].toInt() == 3,
            "The real primary did not persist the forwarded file and requested position.");
#endif
        auto* repeatPane = activeEditor();
        repeatPane->send(SCI_CLEARSELECTIONS);
        repeatPane->send(SCI_CLEARALL);
        const auto metadataRevision = document_revision(*controller, current().id);
        repeatPane->send(SCI_MARKERADD, 0, 0);
        repeatPane->send(SCI_COLOURISE, 0, -1);
        repeatPane->send(SCI_SETSAVEPOINT);
        check(document_revision(*controller, current().id) == metadataRevision,
            "Non-text metadata/savepoint notifications changed the document revision.");
        repeatPane->send(SCI_MARKERDELETEALL, 0);
        macroSteps = QJsonArray{QJsonObject{{"kind", "Insert"}, {"text", "x"}}};
        check(playMacro(3) && text(repeatPane) == "xxx", "Macro repeat did not execute the requested count.");
        dispatch("undo");
        check(text(repeatPane).isEmpty(), "Repeated macro playback was not one undo action.");
        const auto fullDocumentWrap = repeatPane->send(SCI_GETWRAPMODE);
        const bool mapWasVisible = mapDock && mapDock->isVisible();
        const bool longLineCheck = qEnvironmentVariableIsSet("NOTEPAD_STAR_LONG_LINE_CHECK");
        repeatPane->send(SCI_SETWRAPMODE, SC_WRAP_NONE);
        if (mapWasVisible && !longLineCheck) mapDock->hide();
        if (longLineCheck) {
            check(mapDock && documentMap, "Long-line qualification requires the shared Document Map.");
            mapDock->show();
            updateMap();
            check(mapDock->isVisible() && documentMap->send(SCI_GETDOCPOINTER) == repeatPane->send(SCI_GETDOCPOINTER),
                "Long-line qualification did not include a visible, shared Document Map.");
        }
        const QByteArray fullMacroDocument = longLineCheck ? QByteArray(32 * 1024 * 1024, 'a') :
            (QByteArray(1023, 'a') + '\n').repeated(32768);
        check(fullMacroDocument.size() == 32 * 1024 * 1024, "Macro boundary fixture is not exactly 32 MiB.");
        QElapsedTimer longLineTimer; longLineTimer.start();
        repeatPane->sends(SCI_ADDTEXT, fullMacroDocument.size(), fullMacroDocument.constData());
        if (longLineCheck) std::fprintf(stderr, "Long-line stage insert: %lld ms\n", static_cast<long long>(longLineTimer.elapsed()));
        repeatPane->send(SCI_SELECTALL);
        if (longLineCheck) std::fprintf(stderr, "Long-line stage select: %lld ms\n", static_cast<long long>(longLineTimer.elapsed()));
        check(playMacro() && text(repeatPane) == "x", "A shrinking macro replacement was incorrectly rejected at the document limit.");
        if (longLineCheck) std::fprintf(stderr, "Long-line stage shrink: %lld ms\n", static_cast<long long>(longLineTimer.elapsed()));
        dispatch("undo");
        if (longLineCheck) std::fprintf(stderr, "Long-line stage undo: %lld ms\n", static_cast<long long>(longLineTimer.elapsed()));
        check(text(repeatPane) == fullMacroDocument, "Shrinking a full-size document was not byte-exactly undoable.");
        repeatPane->send(SCI_CLEARALL);
        if (longLineCheck) {
            std::fprintf(stderr, "Long-line qualification: 33554432 ASCII bytes, insert/select/shrink/undo/clear = %lld ms\n",
                static_cast<long long>(longLineTimer.elapsed()));
            check(longLineTimer.elapsed() <= 15000, "The 32 MiB ASCII long-line workflow exceeded its 15-second budget.");
        }
        repeatPane->send(SCI_SETWRAPMODE, fullDocumentWrap);
        if (mapDock) mapDock->setVisible(mapWasVisible);
        bool cancelClicked = false;
        bool inputBlocked = false;
        bool commandsBlocked = false;
        QTimer::singleShot(0, this, [&] {
            if (!macroPlaybackDialog) return;
            const auto beforeInput = text(repeatPane);
            QKeyEvent blockedInput(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, "blocked");
            QApplication::sendEvent(repeatPane, &blockedInput);
            inputBlocked = text(repeatPane) == beforeInput;
            try { dispatch("new"); } catch (const std::exception&) { commandsBlocked = true; }
            if (auto* cancel = macroPlaybackDialog->findChild<QPushButton*>("cancel-macro-playback")) {
                cancelClicked = true;
                cancel->click();
            }
        });
        check(!playMacro(1000) && cancelClicked && inputBlocked && commandsBlocked,
            "Macro cancellation or concurrent-input guards failed.");
        check(!text(repeatPane).isEmpty() && text(repeatPane).size() < 1000 &&
            repeatPane->send(SCI_GETUNDOSEQUENCE) == 0 && !macroPlaybackActive,
            "Cancelled macro did not close its undo scope or expose the applied prefix.");
        dispatch("undo");
        check(text(repeatPane).isEmpty(), "Cancelled macro prefix was not undoable.");
        const auto fixedDate = QDateTime::fromString("2026-01-02T03:04:05Z", Qt::ISODate);
        const auto generatedDate = formattedDateTime(fixedDate, "date_time_custom", "yyyy-MM-dd HH:mm:ss");
        check(generatedDate == "2026-01-02 03:04:05", "Custom date/time formatting changed its Qt-pattern contract.");
        insertGeneratedText(generatedDate);
        check(text(repeatPane) == generatedDate, "Generated date/time was not inserted at the selection.");
        dispatch("undo");
        check(text(repeatPane).isEmpty(), "Generated date/time insertion was not undoable.");
        check(!formattedDateTime(fixedDate, "date_time_short").isEmpty() &&
            !formattedDateTime(fixedDate, "date_time_long").isEmpty(), "Locale date/time formats were unavailable.");
        const auto imageFolder = testDirectory->filePath("pasted");
        check(QDir().mkpath(imageFolder), "Could not create the pasted-image test folder.");
        auto& repeatImages = static_cast<WorkspaceEditor*>(repeatPane)->images;
        const auto wrapModeBefore = repeatPane->send(SCI_GETWRAPMODE);
        repeatPane->send(SCI_SETWRAPMODE, SC_WRAP_WORD);
        QImage pastedImage(40, 120, QImage::Format_ARGB32);
        pastedImage.fill(QColor(12, 34, 56));
        QApplication::clipboard()->setImage(pastedImage);
        check(clipboardHoldsOnlyImage(), "An image-only clipboard was not routed to the image paste.");
        QApplication::clipboard()->setText("plain");
        check(!clipboardHoldsOnlyImage(), "A text clipboard must keep the plain-text paste behavior.");
        pasteImageAs(pastedImage, imageFolder);
        const auto pastedRow = repeatPane->send(SCI_LINEFROMPOSITION, repeatPane->send(SCI_GETCURRENTPOS)) - 1;
        const auto pastedEntry = parseInlineImageLine(inlineImageLineText(repeatPane, pastedRow));
        check(pastedEntry && pastedEntry->flavor == InlineImageFlavor::Token &&
            pastedEntry->width == pastedImage.width() && pastedEntry->height == pastedImage.height(),
            "Pasting an image did not insert a renderable inline-image line.");
        const QFileInfo pastedFile(repeatImages.resolve(pastedEntry->reference));
        check(pastedFile.isFile() && pastedFile.absolutePath() == QFileInfo(imageFolder).absoluteFilePath(),
            "Pasting an image did not write the file into the folder that holds the document's images.");
        const QImage writtenImage(pastedFile.absoluteFilePath());
        check(writtenImage.size() == pastedImage.size() && writtenImage.pixel(0, 0) == pastedImage.pixel(0, 0),
            "Pasting an image did not write a decodable file.");
        check(repeatPane->send(SCI_ANNOTATIONGETLINES, pastedRow) > 0 && repeatImages.placements().size() == 1,
            "An inline image did not reserve the rows it is drawn into.");
        const auto pastedPlacement = repeatImages.placements().front();
        const auto canvas = repeatPane->viewport()->grab().toImage();
        const QPoint sample(static_cast<int>(pastedPlacement.image.center().x() * canvas.devicePixelRatio()),
            static_cast<int>(pastedPlacement.image.center().y() * canvas.devicePixelRatio()));
        check(canvas.rect().contains(sample) && QColor(canvas.pixel(sample)) == QColor(12, 34, 56),
            "An inline image was not painted inside the editor canvas.");
        const auto backValue = repeatPane->send(SCI_STYLEGETBACK, STYLE_DEFAULT);
        const QColor editorBackground(static_cast<int>(backValue & 0xff), static_cast<int>((backValue >> 8) & 0xff),
            static_cast<int>((backValue >> 16) & 0xff));
        const auto ratio = canvas.devicePixelRatio();
        const int bandY = static_cast<int>((pastedPlacement.image.top() +
            static_cast<int>(repeatPane->send(SCI_TEXTHEIGHT, static_cast<uptr_t>(pastedRow))) / 2) * ratio);
        bool referenceHidden = bandY >= 0 && bandY < canvas.height();
        for (int x = static_cast<int>((pastedPlacement.image.right() + 2) * ratio); x < canvas.width(); ++x)
            if (QColor(canvas.pixel(x, bandY)) != editorBackground) referenceHidden = false;
        check(referenceHidden, "A wrapped inline-image reference stayed visible beside the image.");
        repeatPane->send(SCI_SETWRAPMODE, wrapModeBefore);
        auto resizedEntry = *pastedEntry;
        resizedEntry.width = 120;
        resizedEntry.height = 90;
        resizeInlineImage(current(), repeatPane, pastedRow, resizedEntry);
        const auto storedEntry = parseInlineImageLine(inlineImageLineText(repeatPane, pastedRow));
        check(storedEntry && storedEntry->width == 120 && storedEntry->height == 90,
            "Resizing an inline image did not rewrite its recorded size.");
        dispatch("undo");
        const auto restoredEntry = parseInlineImageLine(inlineImageLineText(repeatPane, pastedRow));
        check(restoredEntry && restoredEntry->width == pastedImage.width(),
            "Resizing an inline image was not undoable.");
        dispatch("undo");
        check(text(repeatPane).isEmpty() && repeatImages.placements().empty(),
            "A pasted inline image was not undoable.");
        check(!parseInlineImageLine("[[image:art/shot.png|0x10]]") && !parseInlineImageLine("plain text"),
            "An unusable inline-image line was accepted.");
        const InlineImageLine markdownEntry{InlineImageFlavor::Markdown, "art/a b(1).png", "pasted image", 320, 240};
        check(formatInlineImageLine(markdownEntry) == "![pasted image](<art/a b(1).png> =320x240)" &&
            inlineImageLineRoundTrips(markdownEntry),
            "Markdown documents did not receive a bracketed, sized Markdown image line.");
        const InlineImageLine htmlEntry{InlineImageFlavor::Html, "art/shot.png", "pasted image", 320, 240};
        check(formatInlineImageLine(htmlEntry) ==
            "<img src=\"art/shot.png\" alt=\"pasted image\" width=\"320\" height=\"240\">" &&
            inlineImageLineRoundTrips(htmlEntry),
            "HTML documents did not receive a sized img line.");
        check(inlineImageFlavorFor("/notes/readme.md") == InlineImageFlavor::Markdown &&
            inlineImageFlavorFor("/notes/page.html") == InlineImageFlavor::Html &&
            inlineImageFlavorFor({}) == InlineImageFlavor::Token,
            "Inline images did not follow the markup of the document that holds them.");
        check(inlineImageReference("/notes/readme.md", "/notes/art/shot.png") == "art/shot.png" &&
            inlineImageReference({}, "/pictures/shot.png") == "/pictures/shot.png",
            "Inline image references were not written relative to the document when possible.");
        check(repeatImages.resolve("\\\\attacker\\share\\a.png").isEmpty() &&
            repeatImages.resolve("//attacker/share/a.png").isEmpty() &&
            repeatImages.resolve("\\\\?\\C:\\a.png").isEmpty() &&
            repeatImages.resolve("http://attacker/a.png").isEmpty() &&
            repeatImages.resolve("smb://attacker/a.png").isEmpty() &&
            !repeatImages.resolve("art/shot.png").isEmpty(),
            "A remote inline-image reference in untrusted document text was still dereferenced.");
        const QByteArray summaryFixture("\xc3\xa9 \xf0\x9f\x9a\x80\r\na\rb\n");
        repeatPane->sends(SCI_ADDTEXT, summaryFixture.size(), summaryFixture.constData());
        bool summaryShown = false;
        bool summaryCorrect = false;
        QTimer answerSummary;
        answerSummary.setInterval(20);
        connect(&answerSummary, &QTimer::timeout, this, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* message = qobject_cast<QMessageBox*>(widget);
                if (!message || !message->isVisible() || message->windowTitle() != "Document Summary") continue;
                summaryShown = true;
                summaryCorrect = message->textFormat() == Qt::PlainText &&
                    message->text().contains("UTF-8 buffer bytes: 13") && message->text().contains("Unicode scalar values: 9");
                message->accept();
                break;
            }
        });
        answerSummary.start();
        dispatch("document_summary");
        answerSummary.stop();
        check(summaryShown && summaryCorrect && text(repeatPane) == summaryFixture,
            "Document summary changed text or reported incorrect Unicode counts.");
        dispatch("clear_recent");
        const auto recentPath = testDirectory->filePath("recent&latin1.txt");
        {
            QFile recentSource(recentPath);
            check(recentSource.open(QIODevice::WriteOnly) && recentSource.write("caf\xe9", 4) == 4, "Cannot create recent-file fixture.");
        }
        openNativeFile(filePath(recentPath), "ISO 8859-1");
        const auto recordedRecent = recent_files(*controller);
        check(recordedRecent.size() == 1 && qs(recordedRecent[0].encoding) == "ISO 8859-1",
            "A successful open did not retain its recent-file encoding.");
        activeEditor()->sends(SCI_APPENDTEXT, 7, "discard");
        change_encoding(*controller, current().id, rs(text(activeEditor())), "UTF-8");
        closeTab(tabs->currentIndex(), true);
        dispatch("reopen_closed");
        check(text(activeEditor()) == QByteArray("caf\xc3\xa9"), "Reopening a closed file resurrected discarded text or lost its encoding.");
        closeTab(tabs->currentIndex());
        populateRecentMenu();
        check(recentMenu->actions().size() == 1, "Recent menu contains duplicate history entries.");
        check(recentMenu->actions().front()->text().contains("&&"), "A filename became an unintended menu accelerator.");
        recentMenu->actions().front()->trigger();
        check(text(activeEditor()) == QByteArray("caf\xc3\xa9"), "Recent menu action did not open its captured native path.");
        closeTab(tabs->currentIndex());
        const auto otherRecentPath = testDirectory->filePath("other-recent.txt");
        {
            QFile otherRecent(otherRecentPath);
            check(otherRecent.open(QIODevice::WriteOnly) && otherRecent.write("other") == 5, "Cannot create another recent-file fixture.");
        }
        openNativeFile(filePath(otherRecentPath));
        closeTab(tabs->currentIndex());
        const auto beforeAllRecent = tabs->count();
        dispatch("open_all_recent");
        check(tabs->count() == beforeAllRecent + 2, "Open All Recent did not use a stable complete history snapshot.");
        closeTab(tabs->currentIndex());
        closeTab(tabs->currentIndex());
        dispatch("clear_recent");
        check(recent_files(*controller).empty() && closedFiles.empty() && QFileInfo(recentPath).exists(),
            "Clearing history retained entries or deleted an actual file.");
        bool missingRecentRefused = false;
        try { openNativeFile(filePath(testDirectory->filePath("missing-recent.txt"))); }
        catch (const std::exception&) { missingRecentRefused = true; }
        check(missingRecentRefused && recent_files(*controller).empty(), "A failed open entered the recent-file list.");
        bool aboutShown = false;
        bool brandShown = false;
        QTimer closeAbout;
        closeAbout.setInterval(20);
        connect(&closeAbout, &QTimer::timeout, this, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QDialog*>(widget);
                if (!dialog || !dialog->isVisible() || dialog->windowTitle() != "About Notepad Star") continue;
                aboutShown = true;
                const auto* logo = dialog->findChild<QLabel*>("brand-wordmark");
                brandShown = logo && logo->accessibleName() == "Notepad Star logo" && !logo->pixmap().isNull() &&
                    qAbs(logo->pixmap().deviceIndependentSize().width() - 460) < 1;
                dialog->accept();
                break;
            }
        });
        closeAbout.start();
        dispatch("about");
        closeAbout.stop();
        check(aboutShown && brandShown, "The About window did not display the embedded brand wordmark.");
        if (!testFailure.isEmpty()) throw std::runtime_error(testFailure.toStdString());
        finish_recovery(*controller);
        qInfo("Editor checks passed: Rust state, Unicode/BOM file saves, conflicts, sessions, IME, clone/undo, replace, regex integration and themes.");
    }

private:
    void uiToolsChecks() {
        LaunchSettings launch{};
        launch.smoke_test = true;
        rust::Vec<FilePath> files;
        Shell fixture(launch, files);
        fixture.showMaximized();
        QTest::qWait(30);
        check(fixture.startMaximized() && fixture.isMaximized() && !fixture.isFullScreen(),
            "Default maximized startup was confused with full-screen mode.");
        check(fixture.dock->isHidden() && !fixture.commands.at("document_list")->isChecked(),
            "The document sidebar opened by itself instead of waiting for its toggle.");
        fixture.commands.at("document_list")->trigger();
        check(!fixture.dock->isHidden() && fixture.commands.at("document_list")->isChecked(),
            "The sidebar could not be opened from View.");
        fixture.dock->close();
        check(fixture.dock->isHidden() && !fixture.commands.at("document_list")->isChecked(),
            "Closing the sidebar did not update its menu toggle.");
        fixture.commands.at("document_list")->trigger();
        check(!fixture.dock->isHidden() && fixture.commands.at("document_list")->isChecked(),
            "The sidebar could not be reopened from View.");
        check(fixture.commands.at("document_list")->shortcut() == QKeySequence("Ctrl+Alt+L"),
            "The sidebar keyboard shortcut is missing.");
        int letterGroups = 0;
        int flatLanguages = 0;
        bool jsonUnderJ = false;
        for (auto* entry : fixture.menus.at("Language")->actions()) {
            if (entry->isSeparator() || fixture.commands.count(entry->objectName())) continue;
            if (entry->menu() && entry->text().size() == 1) {
                ++letterGroups;
                if (entry->text() == "J")
                    for (auto* language : entry->menu()->actions())
                        if (language->text().compare("JSON", Qt::CaseInsensitive) == 0) jsonUnderJ = true;
            } else ++flatLanguages;
        }
        check(letterGroups >= 10 && flatLanguages == 0 && jsonUnderJ,
            "The Language menu still lists every language instead of grouping them by initial letter.");
        auto* pane = fixture.activeEditor();
        const auto narrow = pane->send(SCI_GETMARGINWIDTHN, 0);
        check(narrow + pane->send(SCI_GETMARGINWIDTHN, 1) + pane->send(SCI_GETMARGINWIDTHN, 2) < 80,
            "The small-document gutter is still as wide as the previous 80-unit gutter.");
        const QByteArray manyLines(2000, '\n');
        pane->sends(SCI_ADDTEXT, manyLines.size(), manyLines.constData());
        check(pane->send(SCI_GETMARGINWIDTHN, 0) > narrow, "The number gutter did not grow with the line count.");
        pane->send(SCI_CLEARALL);
        check(pane->send(SCI_GETMARGINWIDTHN, 0) == narrow, "The number gutter did not shrink with the line count.");
        const QByteArray json("{\"name\":\"Notepad Star\",\"meta\":{\"count\":9007199254740993,\"ok\":true},"
            "\"items\":[1,{\"text\":\"\xe6\x97\xa5\xe6\x9c\xac\"}]}");
        pane->sends(SCI_ADDTEXT, json.size(), json.constData());
        const auto jsonId = fixture.current().id;
        const auto jsonRevisionBefore = document_revision(*fixture.controller, jsonId);
        fixture.dispatch("json_graph");
        check(fixture.jsonPanel && fixture.jsonPanel->nodeCount() == 9 &&
            fixture.jsonPanel->graphCardCount() == 4 && fixture.jsonPanel->graphRowCount() == 8,
            "The JSON graph did not group primitive members into their container cards.");
        check(fixture.jsonPanel->view() == JsonView::Graph && !fixture.jsonPanel->findChild<QTabWidget*>() &&
            fixture.commands.at("json_view_graph")->isChecked() && !fixture.commands.at("json_view_tree")->isChecked(),
            "The JSON view selector did not move out of the panel into the toolbar.");
        {
            // Inspector chrome stays on single rows so the preview keeps the panel height.
            auto* jsonStatus = fixture.jsonPanel->findChild<QLabel*>("json-status");
            auto* jsonPointer = fixture.jsonPanel->findChild<QLineEdit*>("json-pointer");
            auto* pointerCopy = fixture.jsonPanel->findChild<QPushButton*>("json-copy-pointer");
            check(jsonStatus && !jsonStatus->wordWrap() &&
                jsonStatus->sizeHint().height() <= jsonStatus->fontMetrics().height() + 4,
                "The JSON inspector status line still wraps into several rows of chrome.");
            check(jsonPointer && pointerCopy &&
                jsonPointer->geometry().bottom() >= pointerCopy->geometry().top() &&
                pointerCopy->geometry().bottom() >= jsonPointer->geometry().top(),
                "The JSON pointer field and its copy buttons still occupy separate footer rows.");
        }
        auto* mainToolbar = fixture.findChild<QToolBar*>("main-toolbar");
        const auto chromeRows = fixture.findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly);
        check(mainToolbar && chromeRows.size() == 1 && chromeRows.first() == mainToolbar &&
            !fixture.findChild<QToolBar*>("tools-toolbar") && fixture.compareButton && fixture.jsonButton &&
            mainToolbar->isAncestorOf(fixture.compareButton) && mainToolbar->isAncestorOf(fixture.jsonButton),
            "The Compare and JSON tool groups did not collapse into the single main toolbar row.");
        check(fixture.jsonButton->menu() && fixture.compareButton->menu() &&
            fixture.jsonButton->menu()->actions().contains(fixture.commands.at("json_view_graph")) &&
            fixture.compareButton->menu()->actions().contains(fixture.commands.at("next_difference")) &&
            fixture.compareButton->menu()->actions().contains(fixture.commands.at("comparison_settings")),
            "The secondary Compare and JSON options are missing from their hover menus.");
        check(fixture.jsonButton->isChecked() && fixture.jsonButton->isCheckable() &&
            fixture.compareButton->isChecked() == fixture.commands.at("toggle_comparison")->isChecked(),
            "The tool group buttons did not mirror the checked state of their primary commands.");
        fixture.jsonPanel->toggleNode(7);
        check(fixture.jsonPanel->graphCardCount() == 3 && fixture.jsonPanel->graphRowCount() == 7,
            "Collapsing a nested container did not remove its card.");
        fixture.jsonPanel->toggleNode(7);
        check(fixture.jsonPanel->graphCardCount() == 4 && fixture.jsonPanel->graphLayoutProblem().isEmpty(),
            "Re-expanding a nested container did not restore a well-formed card layout.");
        fixture.jsonPanel->selectNode(8);
        const auto selectionStart = pane->send(SCI_GETSELECTIONSTART);
        check(text(pane).mid(selectionStart, pane->send(SCI_GETSELECTIONEND) - selectionStart) ==
            QByteArray("\"\xe6\x97\xa5\xe6\x9c\xac\"") &&
            document_revision(*fixture.controller, jsonId) == jsonRevisionBefore && text(pane) == json,
            "JSON node selection did not locate the exact Unicode source without editing it.");
        fixture.dispatch("json_view_pretty");
        auto* prettyView = fixture.jsonPanel->findChild<QPlainTextEdit*>("json-pretty-preview");
        check(prettyView && prettyView->toPlainText().contains("\"name\": \"Notepad Star\"") &&
            fixture.jsonPanel->view() == JsonView::Pretty && fixture.commands.at("json_view_pretty")->isChecked(),
            "The toolbar did not switch the inspector to its pretty view.");
        int colorized = 0;
        for (auto block = prettyView->document()->begin(); block != prettyView->document()->end(); block = block.next())
            if (block.layout() && !block.layout()->formats().isEmpty()) ++colorized;
        check(colorized >= 3, "Pretty JSON was not colorized by default.");
        fixture.dispatch("json_view_tree");
        check(fixture.jsonPanel->view() == JsonView::Tree, "The toolbar did not switch back to the tree view.");
        {
            // A deeper document must still group primitives instead of drawing one card per value.
            const QByteArray deep("{\"a\":{\"b\":{\"c\":{\"d\":1,\"e\":2,\"f\":3}},\"g\":[{\"h\":4},{\"i\":5},{\"j\":6}]},"
                "\"k\":1,\"l\":2,\"m\":3,\"n\":4,\"o\":[[1,2],[3,4]]}");
            pane->send(SCI_SETTARGETRANGE, 0, pane->send(SCI_GETLENGTH));
            pane->sends(SCI_REPLACETARGET, deep.size(), deep.constData());
            fixture.refreshJson(true);
            check(fixture.jsonPanel->nodeCount() > 2 * fixture.jsonPanel->graphCardCount() &&
                fixture.jsonPanel->graphLayoutProblem().isEmpty(),
                "The deeper graph layout overlapped cards, misplaced a child, or drew one card per value.");
            pane->send(SCI_SETTARGETRANGE, 0, pane->send(SCI_GETLENGTH));
            pane->sends(SCI_REPLACETARGET, json.size(), json.constData());
            fixture.refreshJson(true);
            check(text(pane) == json, "Restoring the graph fixture text failed.");
        }
        pane->send(SCI_SETSEL, 0, 0);
        fixture.dispatch("json_format");
        check(text(pane).contains("\n  \"name\":") && text(pane).contains("9007199254740993"),
            "Pretty JSON changed a large number or failed to indent.");
        fixture.dispatch("undo");
        check(text(pane) == json, "Pretty JSON was not one undoable operation.");
        pane->send(SCI_SETSEL, 0, 0);
        fixture.dispatch("json_minify");
        check(text(pane) == json, "Minify JSON changed already compact tokens.");
        pane->sends(SCI_APPENDTEXT, 1, "?");
        fixture.refreshJson(true);
        check(fixture.jsonPanel->nodeCount() == 0 && fixture.jsonPanel->graphCardCount() == 0 &&
            fixture.jsonPanel->findChild<QLabel*>("json-status")->text().contains("Invalid JSON") &&
            !fixture.jsonPanel->findChild<QPushButton*>("json-copy-pretty")->isEnabled() &&
            text(pane).endsWith('?'), "Invalid JSON left a stale graph or mutated the source.");
        fixture.dispatch("undo");
        fixture.refreshJson(true);
        check(fixture.jsonPanel->nodeCount() == 9, "Correcting JSON did not restore its preview.");
        pane->send(SCI_SETREADONLY, true);
        fixture.dispatch("json_preview");
        check(fixture.jsonPanel->nodeCount() == 9 && pane->send(SCI_GETREADONLY),
            "Preview changed a source document's read-only state.");
        bool readonlyRefused = false;
        try { fixture.dispatch("json_format"); } catch (const std::exception&) { readonlyRefused = true; }
        check(readonlyRefused && text(pane) == json, "Formatting modified read-only JSON.");
        pane->send(SCI_SETREADONLY, false);
        fixture.jsonDock->close();
        fixture.dispatch("json_graph");
        check(!fixture.jsonDock->isHidden(), "The closed JSON inspector could not be reopened.");
        fixture.jsonDock->hide();
        pane->send(SCI_CLEARALL);
        const QByteArray leftText("same\nleft \xe6\x97\xa5\xe6\x9c\xac\nend\n");
        pane->sends(SCI_ADDTEXT, leftText.size(), leftText.constData());
        pane->send(SCI_MARKERADD, 1, 0);
        const auto leftId = fixture.current().id;
        auto* leftPage = fixture.current().page;
        const auto leftRevision = document_revision(*fixture.controller, leftId);
        fixture.addDocument(false);
        auto* rightSource = fixture.activeEditor();
        const QByteArray rightText("intro\nsame\nright \xe6\x97\xa5\xe6\x9c\xac\nextra\nend\n");
        rightSource->sends(SCI_ADDTEXT, rightText.size(), rightText.constData());
        const auto rightId = fixture.current().id;
        auto* rightPage = fixture.current().page;
        const auto rightDocumentPointer = rightSource->send(SCI_GETDOCPOINTER);
        fixture.dispatch("move_other_view");
        check(fixture.tabs->groupCount(0) == 1 && fixture.tabs->groupCount(1) == 1 &&
            fixture.viewInGroup(1)->primary == rightSource && !rightSource->send(SCI_GETREADONLY),
            "Move to Other View did not move the actual editable document.");
        fixture.addDocument(false);
        fixture.activeEditor()->sends(SCI_ADDTEXT, 6, "third\n");
        fixture.dispatch("move_left_view");
        fixture.addDocument(false);
        fixture.activeEditor()->sends(SCI_ADDTEXT, 11, "other same\n");
        const auto fourthId = fixture.current().id;
        auto* fourthPage = fixture.current().page;
        fixture.dispatch("move_right_view");
        check(fixture.tabs->groupCount(0) == 2 && fixture.tabs->groupCount(1) == 2 && fixture.tabs->count() == 4,
            "Multiple documents did not retain independent left/right tab groups.");
        fixture.tabs->setCurrentWidget(rightPage);
        rightSource->send(SCI_BEGINUNDOACTION);
        rightSource->sends(SCI_APPENDTEXT, 1, "!");
        rightSource->send(SCI_ENDUNDOACTION);
        rightSource->send(SCI_SETSEL, 1, 4);
        const auto movedRevision = document_revision(*fixture.controller, rightId);
        fixture.dispatch("move_left_view");
        fixture.dispatch("move_right_view");
        check(rightSource->send(SCI_GETDOCPOINTER) == rightDocumentPointer &&
            rightSource->send(SCI_GETANCHOR) == 1 && rightSource->send(SCI_GETCURRENTPOS) == 4 &&
            document_revision(*fixture.controller, rightId) == movedRevision && rightSource->send(SCI_CANUNDO),
            "Moving a tab copied its text or lost selection/undo/revision identity.");
        rightSource->send(SCI_UNDO);
        check(text(rightSource) == rightText, "Undo was lost when moving a tab between groups.");
        const auto rightRevision = document_revision(*fixture.controller, rightId);
        fixture.tabs->setCurrentWidget(leftPage);
        fixture.tabs->selectInGroup(1, rightPage);
        pane->send(SCI_SETWRAPMODE, SC_WRAP_WORD);
        fixture.dispatch("compare_tabs");
        QTest::qWait(30);
        check(fixture.comparisonEnabled && fixture.comparisonLeft == leftId && fixture.comparisonTarget == rightId &&
            fixture.comparisonResult["hunks"].toArray().size() == 2 &&
            !fixture.findChild<QWidget*>("comparison-panel"),
            "Comparison did not use the selected editable pair, or retained the old button panel.");
        check(!pane->send(SCI_GETREADONLY) && !rightSource->send(SCI_GETREADONLY) &&
            document_revision(*fixture.controller, leftId) == leftRevision &&
            document_revision(*fixture.controller, rightId) == rightRevision &&
            text(pane) == leftText && text(rightSource) == rightText && (pane->send(SCI_MARKERGET, 1) & 1),
            "Comparison altered original text, read-only state, revision or bookmarks.");
        fixture.dispatch("json_inspector");
        check(!fixture.jsonDock->isHidden() && fixture.comparisonEnabled &&
            fixture.commands.at("json_inspector")->isChecked() && fixture.commands.at("toggle_comparison")->isChecked() &&
            fixture.tabs->groupCount(0) == 2 && fixture.tabs->groupCount(1) == 2,
            "Opening the JSON inspector cancelled the comparison or disturbed the document groups.");
        fixture.dispatch("json_inspector");
        check(fixture.jsonDock->isHidden() && !fixture.commands.at("json_inspector")->isChecked() &&
            fixture.comparisonEnabled, "Toggling the inspector off also stopped the comparison.");
        const auto globalLineY = [](ScintillaEditBase* editor, int line) {
            const auto position = editor->send(SCI_POSITIONFROMLINE, line);
            return editor->viewport()->mapToGlobal(QPoint(0, static_cast<int>(editor->send(SCI_POINTYFROMPOSITION, 0, position)))).y();
        };
        check(qAbs(globalLineY(pane, 0) - globalLineY(rightSource, 1)) <= 1 &&
            qAbs(globalLineY(pane, 2) - globalLineY(rightSource, 4)) <= 1,
            "Matching lines were not visually aligned across leading/middle insertions.");
        const auto spans = fixture.comparisonResult["spans"].toArray();
        check(!spans.isEmpty(), "Character comparison did not produce intra-line ranges.");
        for (const auto& value : spans) {
            const auto span = value.toObject();
            auto* editor = span["side"].toString() == "left" ? pane : rightSource;
            check(editor->send(SCI_INDICATORVALUEAT, 24, span["start"].toInteger()) != 0,
                "A changed character span was not highlighted in its original editor.");
        }
        fixture.navigateComparison(1);
        fixture.dispatch("swap_comparison");
        check(fixture.tabs->groupOf(rightPage) == 0 && fixture.tabs->groupOf(leftPage) == 1,
            "Swapping compared tabs did not move the real documents.");
        fixture.dispatch("swap_comparison");
        fixture.dispatch("stop_comparison");
        check(!fixture.comparisonEnabled && fixture.tabs->groupCount(0) == 2 && fixture.tabs->groupCount(1) == 2 &&
            (pane->send(SCI_MARKERGET, 1) & 1) && pane->send(SCI_GETWRAPMODE) == SC_WRAP_WORD &&
            pane->send(SCI_ANNOTATIONGETLINES, 1) == 0 && static_cast<WorkspaceEditor*>(pane)->leadingRows == 0,
            "Closing comparison removed tabs/bookmarks or failed to restore the normal layout.");
        std::vector<std::pair<QWidget*, int>> groupsBefore;
        for (const auto& view : fixture.views) groupsBefore.emplace_back(view.page, fixture.tabs->groupOf(view.page));
        fixture.dispatch("move_all_other_view");
        fixture.dispatch("move_all_other_view");
        check(fixture.tabs->groupCount(1) == 0 && fixture.tabs->groupCount(0) == 4,
            "Cannot collect every tab in a single view for the adjacent-compare check.");
        fixture.tabs->setCurrentIndex(0);
        auto* keptPage = fixture.tabs->widget(0);
        auto* adjacentPage = fixture.tabs->widget(1);
        fixture.dispatch("toggle_comparison");
        QTest::qWait(30);
        check(fixture.comparisonEnabled && fixture.tabs->groupOf(adjacentPage) == 1 &&
            fixture.tabs->currentWidget() == keptPage && fixture.commands.at("toggle_comparison")->isChecked(),
            "Comparing with an empty other view did not adopt the adjacent tab.");
        fixture.dispatch("toggle_comparison");
        check(!fixture.comparisonEnabled && !fixture.commands.at("toggle_comparison")->isChecked() &&
            fixture.tabs->count() == 4, "The Compare toggle did not switch comparison off and keep every tab.");
        for (const auto& entry : groupsBefore) fixture.tabs->movePage(entry.first, entry.second);
        fixture.tabs->revealDropTargets();
        fixture.tabs->tabDropped(rightId, 0);
        fixture.tabs->tabDropped(rightId, 1);
        fixture.tabs->finishDrag();
        check(fixture.tabs->groupOf(rightPage) == 1 && fixture.tabs->tabData(fixture.tabs->indexOf(rightPage)).toULongLong() == rightId,
            "Tab drops did not retain document identity across groups.");
        // Windows OLE reads physical mouse-button state, not synthetic Qt button events.
        check(!fixture.tabs->dropTab(nullptr, QByteArray::number(rightId), 0) &&
            !fixture.tabs->dropTab(fixture.tabs->bar(1), "invalid", 0),
            "A foreign or malformed tab drop was accepted.");
        check(fixture.tabs->dropTab(fixture.tabs->bar(1), QByteArray::number(rightId), 0) &&
            fixture.tabs->groupOf(rightPage) == 0 &&
            fixture.tabs->dropTab(fixture.tabs->bar(0), QByteArray::number(rightId), 1) &&
            rightSource->send(SCI_GETDOCPOINTER) == rightDocumentPointer,
            "The native drop handler did not move the original document safely.");
        fixture.tabs->setCurrentWidget(leftPage);
        fixture.tabs->selectInGroup(1, rightPage);
        fixture.dispatch("compare_tabs");
        rightSource->send(SCI_SETTARGETRANGE, 0, rightSource->send(SCI_GETLENGTH));
        rightSource->sends(SCI_REPLACETARGET, leftText.size(), leftText.constData());
        QTest::qWait(500);
        check(fixture.comparisonResult["hunks"].toArray().isEmpty(), "Editing the right tab did not update live comparison.");
        rightSource->send(SCI_UNDO);
        QTest::qWait(500);
        check(!fixture.comparisonResult["hunks"].toArray().isEmpty() && text(rightSource) == rightText,
            "Undo in the right view failed to refresh comparison.");
        fixture.tabs->setCurrentWidget(fourthPage);
        QTest::qWait(500);
        check(fixture.comparisonTarget == fourthId, "Selecting another right-hand tab did not compare the new active pair.");
        fixture.dispatch("stop_comparison");
        fixture.tabs->setCurrentWidget(leftPage);
        pane->send(SCI_SETSEL, 0, 0);
        fixture.dispatch("find");
        check(fixture.search->isWindow() && !fixture.search->isModal() && fixture.search->isVisible() &&
            !fixture.findChild<QToolBar*>("find-toolbar"), "Find still uses an inline toolbar instead of a modeless dialog.");
        fixture.query->setText("same");
        fixture.search->findChild<QPushButton*>("find-all-open")->click();
        fixture.waitForSearch();
        check(fixture.bufferMatchCount == 3 && fixture.searchResults->topLevelItemCount() == 3,
            "Find All did not include unsaved documents from both views.");
        QTreeWidgetItem* fourthHit = nullptr;
        for (int group = 0; group < fixture.searchResults->topLevelItemCount(); ++group) {
            auto* item = fixture.searchResults->topLevelItem(group)->child(0);
            if (item->data(0, Qt::UserRole + 10).toULongLong() == fourthId) fourthHit = item;
        }
        check(fourthHit, "The right-view Find All result is missing.");
        fixture.searchResults->itemDoubleClicked(fourthHit, 0);
        check(fixture.current().id == fourthId && fixture.tabs->activeGroup() == 1,
            "A search result did not activate its document in the correct group.");
        fixture.activeEditor()->sends(SCI_APPENDTEXT, 1, "!");
        const auto staleCaret = fixture.activeEditor()->send(SCI_GETCURRENTPOS);
        fixture.searchResults->itemDoubleClicked(fourthHit, 0);
        check(fixture.search->findChild<QLabel*>("find-status")->text().contains("changed") &&
            fixture.activeEditor()->send(SCI_GETCURRENTPOS) == staleCaret, "A stale Find All result moved the caret.");
        fixture.tabs->setCurrentWidget(leftPage);
        pane->send(SCI_SETSEL, 0, 0);
        fixture.dispatch("replace");
        fixture.query->setText("left");
        fixture.replacement->setText("updated");
        fixture.search->findChild<QPushButton*>("replace-all")->click();
        check(text(pane).contains("updated") && text(rightSource) == rightText, "Popup Replace All edited the wrong tab or both groups.");
        pane->send(SCI_UNDO);
        check(text(pane) == leftText, "Popup Replace All was not one undo operation.");
        fixture.query->setText("same");
        fixture.search->wrapAround->setChecked(false);
        pane->send(SCI_GOTOPOS, pane->send(SCI_GETLENGTH));
        fixture.search->findChild<QPushButton*>("find-next")->click();
        check(pane->send(SCI_GETCURRENTPOS) == pane->send(SCI_GETLENGTH), "Find wrapped despite the dialog option being off.");
        fixture.search->wrapAround->setChecked(true);
        fixture.search->findChild<QPushButton*>("find-next")->click();
        check(pane->send(SCI_GETSELECTIONSTART) == 0 && pane->send(SCI_GETSELECTIONEND) == 4, "Find did not wrap when enabled.");
        fixture.search->findChild<QPushButton*>("find-count")->click();
        fixture.waitForSearch();
        check(fixture.bufferMatchCount == 1 && fixture.searchResults->topLevelItemCount() == 3,
            "Count did not use the active document or unexpectedly replaced the result list.");
        fixture.startBufferSearch(true, false);
        fixture.dispatch("cancel_search");
        fixture.waitForSearch(false);
        check(!fixture.bufferSearchRunning && text(pane) == leftText && text(rightSource) == rightText,
            "Cancelling Find All retained a running queue or changed source text.");
        fixture.searchMode->setCurrentIndex(fixture.searchMode->findData("regex"));
        fixture.query->setText("(?=same)");
        fixture.search->findChild<QPushButton*>("find-all-current")->click();
        fixture.waitForSearch();
        check(fixture.bufferMatchCount == 1, "Regex Find All did not handle a zero-width match.");
        fixture.query->setText("(");
        fixture.search->findChild<QPushButton*>("find-all-current")->click();
        fixture.waitForSearch(false);
        check(!fixture.lastSearchError.isEmpty() && text(pane) == leftText, "Malformed regex did not report an error safely.");
        fixture.search->hide();
        fixture.tabs->setCurrentWidget(leftPage);
        fixture.addDocument(false);
        auto* emptySource = fixture.activeEditor();
        fixture.tabs->selectInGroup(1, rightPage);
        fixture.dispatch("compare_tabs");
        check(static_cast<WorkspaceEditor*>(emptySource)->leadingRows == 0 &&
            !emptySource->send(SCI_GETREADONLY) && text(emptySource).isEmpty(),
            "Comparing an empty file hid or locked its first editable row.");
        emptySource->sends(SCI_ADDTEXT, 3, "new");
        QTest::qWait(500);
        check(text(emptySource) == "new" && text(rightSource) == rightText,
            "Typing in an empty compared document changed the other side.");
        fixture.dispatch("stop_comparison");
        check(fixture.testFailure.isEmpty(), "A UI-tools callback reported an unexpected failure.");
        finish_recovery(*fixture.controller);
        dualGroupRestartChecks();
        std::fprintf(stderr, "UI tools passed: editable tab groups, preserved undo, aligned live character diffs, JSON tools and popup Find/Replace/Find All.\n");
    }
    void dualGroupRestartChecks() {
        LaunchSettings launch{};
        launch.profile = filePath(testDirectory->filePath("dual-group-session"));
        rust::Vec<FilePath> files;
        QByteArray left("\xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x9a\x80\n");
        for (int line = 0; line < 100; ++line) left += "line " + QByteArray::number(line) + "\n";
        const QByteArray right = "one\ntwo\nthree\n" + left;
        {
            Shell source(launch, files);
            source.testMode = true;
            source.show();
            auto* leftPane = source.activeEditor();
            auto* leftPage = source.current().page;
            leftPane->sends(SCI_ADDTEXT, left.size(), left.constData());
            leftPane->send(SCI_SETSEL, 0, 6);
            source.addDocument(false);
            auto* rightPage = source.current().page;
            source.activeEditor()->sends(SCI_ADDTEXT, right.size(), right.constData());
            source.dispatch("move_right_view");
            source.addDocument(false);
            source.activeEditor()->sends(SCI_ADDTEXT, 10, "third note");
            set_document_tab(*source.controller, source.current().id, true, 3);
            source.normalizePinnedOrder();
            source.tabs->setCurrentWidget(leftPage);
            source.tabs->selectInGroup(1, rightPage);
            source.dispatch("compare_tabs");
            leftPane->send(SCI_SETFIRSTVISIBLELINE, 40);
            source.syncComparisonScroll(false);
            source.tabs->setCurrentWidget(rightPage);
            check(source.close() && source.testFailure.isEmpty(), "Normal close did not retain both editable groups.");
        }
        {
            Shell resumed(launch, files);
            resumed.testMode = true;
            resumed.show();
            check(resumed.tabs->groupCount(0) == 1 && resumed.tabs->groupCount(1) == 2 &&
                resumed.tabs->activeGroup() == 1 && text(resumed.activeEditor()) == right,
                "Restart did not restore group membership and the independently selected right tab.");
            auto* leftPane = resumed.viewInGroup(0)->primary;
            check(text(leftPane) == left && leftPane->send(SCI_GETCURRENTPOS) == 6 &&
                leftPane->send(SCI_GETFIRSTVISIBLELINE) == 40 && !resumed.comparisonEnabled &&
                static_cast<WorkspaceEditor*>(leftPane)->leadingRows == 0 &&
                leftPane->send(SCI_ANNOTATIONGETLINES, 0) == 0,
                "Comparison decorations contaminated session text, caret or source-line scrolling.");
            auto* pinned = resumed.tabs->widget(resumed.tabs->groupStart(1));
            const auto found = std::find_if(resumed.views.begin(), resumed.views.end(), [pinned](const auto& view) { return view.page == pinned; });
            check(found != resumed.views.end() && text(found->primary) == "third note" &&
                document_tab(*resumed.controller, found->id).pinned &&
                document_tab(*resumed.controller, found->id).color == 3,
                "Restart lost per-group pinned ordering or tab color.");
            resumed.closeTab(resumed.tabs->indexOf(pinned), true);
            check(resumed.close(), "Cannot close the restored two-group session.");
        }
        {
            Shell resumed(launch, files);
            resumed.testMode = true;
            check(resumed.tabs->groupCount(0) == 1 && resumed.tabs->groupCount(1) == 1 &&
                text(resumed.viewInGroup(0)->primary) == left && text(resumed.viewInGroup(1)->primary) == right,
                "A discarded right-hand tab reappeared or another group's text was lost.");
            check(resumed.close(), "Cannot close the final group-restart fixture.");
        }
    }
    void workspaceRestartChecks() {
        const auto profile = testDirectory->filePath("restart-profile");
        LaunchSettings launch{};
        launch.profile = filePath(profile);
        rust::Vec<FilePath> files;
        const auto namedPath = testDirectory->filePath("restart-named.txt");
        const auto cleanPath = testDirectory->filePath("restart-clean.txt");
        const auto extraPath = testDirectory->filePath("restart-extra.txt");
        const auto write = [](const QString& path, const QByteArray& bytes) {
            QFile file(path);
            check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write restart fixture.");
        };
        const auto read = [](const QString& path) {
            QFile file(path);
            check(file.open(QIODevice::ReadOnly), "Cannot read restart fixture.");
            return file.readAll();
        };
        write(namedPath, QByteArray("caf\xe9"));
        write(cleanPath, "original\nsecond\nthird\n");
        write(extraPath, "CLI file");
        QByteArray scratch("scratch \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x9a\x80\r\n");
        for (int line = 0; line < 80; ++line) scratch += "line " + QByteArray::number(line) + "\n";
        int prompts = 0;
        auto answer = QMessageBox::Cancel;
        QTimer dialogs;
        dialogs.setInterval(10);
        connect(&dialogs, &QTimer::timeout, this, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* message = qobject_cast<QMessageBox*>(widget);
                if (message && message->isVisible() && message->windowTitle() == "Unsaved document") {
                    ++prompts;
                    message->button(answer)->click();
                }
            }
        });
        dialogs.start();
        {
            Shell source(launch, files);
            source.testMode = true;
            source.show();
            auto& scratchView = source.current();
            scratchView.primary->sends(SCI_ADDTEXT, scratch.size(), scratch.constData());
            scratchView.clone->show();
            QTest::qWait(20);
            scratchView.primary->send(SCI_SETSEL, 2, 7);
            scratchView.primary->send(SCI_SETZOOM, 2);
            scratchView.primary->send(SCI_SETFIRSTVISIBLELINE, 10);
            scratchView.clone->send(SCI_SETSEL, 20, 23);
            scratchView.clone->send(SCI_SETFIRSTVISIBLELINE, 12);
            scratchView.primary->send(SCI_MARKERADD, 2, 0);
            scratchView.primary->send(SCI_SETREADONLY, true);
            set_document_tab(*source.controller, scratchView.id, true, 3);
            source.openNativeFile(filePath(namedPath), "Windows-1252");
            source.activeEditor()->sends(SCI_APPENDTEXT, 8, " unsaved");
            change_encoding(*source.controller, source.current().id, rs(text(source.activeEditor())), "UTF-16 BE BOM");
            source.openNativeFile(filePath(cleanPath));
            source.activeEditor()->send(SCI_GOTOPOS, 20);
            source.activeEditor()->send(SCI_MARKERADD, 2, 0);
            source.tabs->setCurrentIndex(0);
            check(source.close() && source.testFailure.isEmpty() && prompts == 0,
                "Normal application exit prompted for or failed to retain unsaved text.");
            check(read(namedPath) == QByteArray("caf\xe9"), "Exiting wrote unsaved changes into the original file.");
        }
        write(namedPath, "external change");
        write(cleanPath, QByteArray("\xc3\xa9"));
        files.push_back(filePath(extraPath));
        {
            Shell resumed(launch, files);
            resumed.testMode = true;
            resumed.show();
            QTest::qWait(20);
            check(resumed.tabs->count() == 4, "Startup did not restore all tabs before opening command-line files.");
            auto& restored = resumed.views[0];
            const auto tab = document_tab(*resumed.controller, restored.id);
            check(text(restored.primary) == scratch && document_dirty(*resumed.controller, restored.id) &&
                pathText(document_path(*resumed.controller, restored.id)).isEmpty() &&
                restored.primary->send(SCI_GETCURRENTPOS) == 7 && restored.primary->send(SCI_GETANCHOR) == 2 &&
                restored.primary->send(SCI_GETZOOM) == 2 && !restored.clone->isHidden() &&
                restored.clone->send(SCI_GETCURRENTPOS) == 23 && restored.clone->send(SCI_GETANCHOR) == 20 &&
                restored.primary->send(SCI_GETREADONLY) != 0 && (restored.primary->send(SCI_MARKERGET, 2) & 1) &&
                tab.pinned && tab.color == 3, "Restart lost unsaved Unicode text or tab/view state.");
            auto& modified = resumed.views[1];
            check(text(modified.primary) == QByteArray("caf\xc3\xa9 unsaved") &&
                qs(document_encoding(*resumed.controller, modified.id)) == "UTF-16 BE BOM" &&
                qs(document_saved_encoding(*resumed.controller, modified.id)) == "Windows-1252" &&
                document_dirty(*resumed.controller, modified.id), "Restart lost an unsaved encoding conversion.");
            bool conflict = false;
            try { save_document(*resumed.controller, modified.id, rs(text(modified.primary)), FilePath{}, "", false); }
            catch (const std::exception&) { conflict = true; }
            check(conflict && read(namedPath) == "external change", "Restored edits overwrote an externally changed file.");
            const auto& clean = resumed.views[2];
            check(text(clean.primary) == QByteArray("\xc3\xa9") && !document_dirty(*resumed.controller, clean.id) &&
                clean.primary->send(SCI_GETCURRENTPOS) == 2 && clean.primary->send(SCI_MARKERNEXT, 0, 1) == -1,
                "A clean file was not refreshed or its saved positions were not adjusted.");
            answer = QMessageBox::Discard;
            check(resumed.closeTab(0) && prompts == 1, "Explicit tab close did not offer a deliberate discard.");
            check(resumed.closeTab(0, true), "Cannot close the modified-file fixture.");
            check(resumed.close() && resumed.testFailure.isEmpty(), "Cannot close the resumed workspace.");
        }
        files.clear();
        {
            Shell resumed(launch, files);
            resumed.testMode = true;
            check(resumed.tabs->count() == 2, "Explicitly discarded tabs reappeared after restarting.");
            resumed.tabs->setCurrentIndex(0);
            auto* pane = resumed.activeEditor();
            pane->sends(SCI_APPENDTEXT, 7, " latest");
            const auto expected = text(pane);
            const auto recoveryRoot = QDir(profile).filePath("recovery");
            QString snapshot;
            QDirIterator iterator(recoveryRoot, {"session.json"}, QDir::Files, QDirIterator::Subdirectories);
            while (iterator.hasNext()) {
                check(snapshot.isEmpty(), "Acknowledged workspace snapshots were not removed.");
                snapshot = iterator.next();
            }
            check(!snapshot.isEmpty(), "Startup did not checkpoint its transferred workspace.");
            const auto permissions = QFile::permissions(snapshot);
            check(QFile::setPermissions(snapshot, permissions &
                ~(QFileDevice::WriteOwner | QFileDevice::WriteUser | QFileDevice::WriteGroup | QFileDevice::WriteOther)),
                "Cannot make snapshot failure fixture read-only.");
            const bool refused = !resumed.close();
            const bool restoredPermissions = QFile::setPermissions(snapshot, permissions);
            check(restoredPermissions && refused && !resumed.testFailure.isEmpty() && text(pane) == expected &&
                !resumed.closingWindow, "A failed session write closed the window or lost its buffer.");
            resumed.testFailure.clear();
            check(resumed.close(), "A repaired session store did not allow a safe retry.");
        }
        {
            Shell resumed(launch, files);
            resumed.testMode = true;
            check(resumed.tabs->count() == 2 && text(resumed.views[0].primary) == QByteArray("\xc3\xa9 latest"),
                "The successful retry did not preserve the latest unsaved contents.");
            check(resumed.close(), "Cannot close the retry verification window.");
        }
        const auto multiProfile = testDirectory->filePath("multi-restart-profile");
        launch.profile = filePath(multiProfile);
        {
            Shell first(launch, files);
            first.testMode = true;
            first.activeEditor()->sends(SCI_ADDTEXT, 5, "first");
            first.checkpointNow();
            Shell second(launch, files);
            second.testMode = true;
            check(second.tabs->count() == 1 && text(second.activeEditor()).isEmpty(),
                "Another live window's recovery lock was stolen.");
            second.activeEditor()->sends(SCI_ADDTEXT, 6, "second");
            check(second.close() && first.close(), "Independent windows did not retain their own workspaces.");
        }
        {
            Shell resumed(launch, files);
            resumed.testMode = true;
            check(resumed.tabs->count() == 2, "Independent closed windows lost unsaved tabs.");
            QList<QByteArray> contents;
            for (const auto& view : resumed.views) contents.append(text(view.primary));
            check(contents.contains("first") && contents.contains("second"), "Multi-instance restart lost a buffer.");
            check(resumed.close(), "Cannot close multi-instance restart fixture.");
        }
        launch.no_session = true;
        {
            Shell isolated(launch, files);
            isolated.testMode = true;
            check(isolated.tabs->count() == 1 && text(isolated.activeEditor()).isEmpty(), "--no-session restored retained tabs.");
            isolated.activeEditor()->sends(SCI_ADDTEXT, 7, "opt-out");
            answer = QMessageBox::Cancel;
            check(!isolated.close() && prompts == 2, "--no-session exit failed to preserve Save/Discard/Cancel.");
            answer = QMessageBox::Discard;
            check(isolated.close() && prompts == 3, "--no-session explicit discard failed.");
        }
        dialogs.stop();
        launch.no_session = false;
        {
            Shell resumed(launch, files);
            resumed.testMode = true;
            check(resumed.tabs->count() == 2, "--no-session deleted another window's retained workspace.");
            check(resumed.close(), "Cannot close final restart fixture.");
        }
#ifdef Q_OS_WIN
        for (int restart = 0; restart < 2; ++restart) {
            QProcess process;
            process.start(QCoreApplication::applicationFilePath(), {"--reuse-instance", "--profile-dir", multiProfile});
            check(process.waitForStarted(3000), "Cannot start the workspace-restart subprocess.");
            QElapsedTimer startup;
            startup.start();
            while (!QFileInfo::exists(QDir(multiProfile).filePath("shared-instance.lock")) &&
                process.state() != QProcess::NotRunning && startup.elapsed() < 6000) QTest::qWait(20);
            check(process.state() != QProcess::NotRunning, "The workspace-restart subprocess failed during startup.");
            QProcess ping;
            ping.start(QCoreApplication::applicationFilePath(), {"--reuse-instance", "--profile-dir", multiProfile});
            check(ping.waitForStarted(3000) && ping.waitForFinished(10000) &&
                ping.exitStatus() == QProcess::NormalExit && ping.exitCode() == 0,
                "The restarted application was not responsive.");
            process.terminate();
            check(process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
                "The real process prompted or failed when retaining unsaved text on close.");
            auto inspect = create_controller();
            initialize_recovery(*inspect, filePath(QDir(multiProfile).filePath("recovery")), true);
            const auto documents = resume_workspace(*inspect);
            QList<QByteArray> contents;
            for (const auto& document : documents) {
                check(document.dirty, "A subprocess restart marked an unsaved note as saved.");
                contents.append(qs(document.text).toUtf8());
            }
            check(documents.size() == 2 && contents.contains("first") && contents.contains("second"),
                "Unsaved notes did not survive a real process restart and normal exit.");
        }
#endif
        qInfo("Workspace restart checks passed: unsaved text, encodings, conflicts, discard, write failure and independent windows.");
    }
    struct DocumentView {
        std::uint64_t id;
        ScintillaEditBase* primary;
        ScintillaEditBase* clone;
        QSplitter* page;
        ScintillaEditBase* focused;
        QByteArray lexer;
        QString languageId;
        QByteArray udlXml;
        int udlProfile = 0;
        int udlDocument = 0;
        bool diskChanged = false;
        bool monitoring = false;
        bool previousReadOnly = false;
        QString functionKey;
        bool inlineImages = false;
    };
    std::optional<languages::LanguageCatalog> catalog;
    QMap<QString, std::optional<languages::CompletionData>> completionCache;
    QMap<QString, QString> completionErrors;
    int udlIdentities = 0;
    rust::Box<Controller> controller;
    std::vector<DocumentView> views;
    EditorTabs* tabs = nullptr;
    QSplitter* editorSplit = nullptr;
    QLabel* comparisonStatus = nullptr;
    QJsonObject comparisonResult;
    bool comparisonEnabled = false;
    QTimer* toolsTimer = nullptr;
    QTimer* inlineImageTimer = nullptr;
    std::set<std::uint64_t> pendingInlineImages;
    QDockWidget* jsonDock = nullptr;
    JsonPanel* jsonPanel = nullptr;
    std::uint64_t jsonDocument = 0;
    std::uint64_t jsonRevision = 0;
    std::uint64_t comparisonTarget = 0;
    std::uint64_t previousDocument = 0;
    std::uint64_t lastActiveDocument = 0;
    std::uint64_t comparisonLeft = 0;
    std::uint64_t comparisonLeftRevision = 0;
    std::uint64_t comparisonRightRevision = 0;
    bool comparisonCacheValid = false;
    bool synchronizingComparison = false;
    bool comparisonAligned = false;
    int comparisonHunk = -1;
    QDockWidget* dock = nullptr;
    QListWidget* documentList = nullptr;
    SearchDialog* search = nullptr;
    ToolGroupButton* compareButton = nullptr;
    ToolGroupButton* jsonButton = nullptr;
    bool bufferSearchRunning = false;
    bool bufferCountOnly = false;
    QList<std::uint64_t> bufferSearchQueue;
    QByteArray bufferSearchTemplate;
    std::size_t bufferSearchBytes = 0;
    std::uint64_t bufferMatchCount = 0;
    int bufferSearchWarnings = 0;
    int bufferSearched = 0;
    QLineEdit* query = nullptr;
    QLineEdit* replacement = nullptr;
    QCheckBox* matchCase = nullptr;
    QCheckBox* wholeWord = nullptr;
    QComboBox* searchMode = nullptr;
    QCheckBox* dotNewline = nullptr;
    QPointer<SearchTask> activeSearch;
    QJsonObject options;
    QString settingsPath;
    QString lastSessionPath;
    bool settingsReady = false;
    bool applyingOptions = false;
    QDockWidget* resultsDock = nullptr;
    QTreeWidget* searchResults = nullptr;
    QString lastSearchError;
    std::uint64_t lastZeroDocument = 0;
    std::uint64_t lastZeroRevision = 0;
    sptr_t lastZeroPosition = -1;
    QString lastZeroQuery;
    QJsonArray macroSteps;
    QString macroName = "Recorded macro";
    bool macroRecording = false;
    bool macroPendingIndent = false;
    bool macroPlaybackActive = false;
    QPointer<QDialog> macroPlaybackDialog;
    bool closingWindow = false;
    QMenu* recentMenu = nullptr;
    std::vector<RecentEntry> closedFiles;
    bool historyWarningShown = false;
    QSet<ScintillaEditBase*> composingEditors;
    std::uint64_t macroDocument = 0;
    QPointer<QProcess> runningProgram;
    QStringList fileQueue;
    QByteArray fileSearchRequest;
    bool fileSearchRunning = false;
    int fileSearchCount = 0;
    int fileResultCount = 0;
    QString fileOriginalMode = "normal";
    bool replacementPreview = false;
    bool replacementPreviewFailed = false;
    rust::Vec<DiskProposal> diskProposals;
    std::size_t proposalBytes = 0;
    QTimer* recoveryTimer = nullptr;
    std::unique_ptr<QTemporaryDir> testDirectory;
    std::map<QString, QMenu*> menus;
    QDockWidget* workspaceDock = nullptr;
    bool recoveryReady = false;
    bool recoveryPending = false;
    std::map<QString, QAction*> commands;
    bool dark = false;
    bool wrap = false;
    bool tearingDown = false;
    bool testMode = false;
    bool noSession = false;
    QString testFailure;
    QPointer<SearchTask> managementTask;
    QDockWidget* extensionDock = nullptr;
    QListWidget* extensionList = nullptr;
    QPlainTextEdit* extensionDetails = nullptr;
    QLabel* extensionStatus = nullptr;
    QList<QPushButton*> extensionButtons;
    QString extensionRoot;
    bool extensionRootReady = false;
    QFileSystemWatcher* diskWatcher = nullptr;
    QTimer* diskTimer = nullptr;
    QSet<QString> pendingDiskChecks;
    QDockWidget* mapDock = nullptr;
    DocumentMapView* documentMap = nullptr;
    sptr_t mappedDocument = 0;
    bool synchronizeVertical = false;
    bool synchronizeHorizontal = false;
    bool scrolling = false;
    bool orderingTabs = false;
    bool synchronizeZoom = false;
    bool changingZoom = false;
    bool distraction = false;
    QList<QPointer<QWidget>> hiddenChrome;
    std::array<ProjectPanel*, 3> projectPanels{};
    QDockWidget* functionDock = nullptr;
    QTreeWidget* functionTree = nullptr;
    QComboBox* functionParser = nullptr;
    QTimer* functionTimer = nullptr;
    QPointer<SearchTask> outlineTask;
    QStringList functionKeys;
    std::uint64_t outlineDocument = 0;
    std::uint64_t outlineRevision = 0;
    QString outlineParserKey;
    QString outlineError;

    DocumentView& current() {
        auto* page = tabs->currentWidget();
        const auto it = std::find_if(views.begin(), views.end(), [page](const auto& view) { return view.page == page; });
        if (it == views.end()) throw std::runtime_error("No active document.");
        return *it;
    }
    ScintillaEditBase* activeEditor() {
        auto& view = current();
        return view.focused;
    }
    static QByteArray text(ScintillaEditBase* editor) {
        const auto length = editor->send(SCI_GETLENGTH);
        check(length <= 32 * 1024 * 1024, "Document exceeds the 32 MiB preview snapshot/save limit.");
        QByteArray result(length + 1, '\0');
        editor->sends(SCI_GETTEXT, static_cast<uptr_t>(result.size()), result.data());
        result.resize(length);
        return result;
    }
    void guarded(const std::function<void()>& action) noexcept {
        try {
            action();
        } catch (const std::exception& error) {
            qCritical("Notepad Star operation failed: %s", error.what());
            if (testMode) testFailure = QString::fromUtf8(error.what());
            else QMessageBox::critical(this, "Notepad Star - operation failed", QString::fromUtf8(error.what()));
        }
    }
    void invoke(const QString& id) {
        guarded([&] { dispatch(id); });
        // Checkable tool actions must mirror real panel state even when a command fails.
        if (commands.count("toggle_comparison")) guarded([&] { syncToolStates(); });
    }
    void waitForSearch(bool expectSuccess = true) {
        QElapsedTimer elapsed;
        elapsed.start();
        while ((activeSearch || fileSearchRunning || bufferSearchRunning) && elapsed.elapsed() < 15000) {
            QApplication::processEvents();
            QTest::qWait(10);
        }
        check(!activeSearch && !fileSearchRunning && !bufferSearchRunning, "Search worker did not finish within the test deadline.");
        if (expectSuccess && !lastSearchError.isEmpty()) throw std::runtime_error(lastSearchError.toStdString());
    }
    void waitForManagement() {
        QElapsedTimer elapsed; elapsed.start();
        while (managementTask && elapsed.elapsed() < 12000) {
            QApplication::processEvents();
            QTest::qWait(10);
        }
        check(!managementTask, "Extension management worker exceeded its test deadline.");
        if (!testFailure.isEmpty()) throw std::runtime_error(testFailure.toStdString());
    }
    void applyPalette() {
        QPalette palette = QApplication::style()->standardPalette();
        if (dark) {
            palette.setColor(QPalette::Window, QColor("#292d35"));
            palette.setColor(QPalette::WindowText, QColor("#e0e4ed"));
            palette.setColor(QPalette::Base, QColor("#1e2128"));
            palette.setColor(QPalette::Text, QColor("#e0e4ed"));
            palette.setColor(QPalette::Button, QColor("#353b45"));
            palette.setColor(QPalette::ButtonText, QColor("#e0e4ed"));
            palette.setColor(QPalette::Highlight, QColor("#426080"));
        }
        QApplication::setPalette(palette);
    }
    void textMeasurementChecks() {
        ScintillaEditBase reference(this);
        ScintillaEditBase optimized(this);
        for (auto* pane : {&reference, &optimized}) {
            pane->resize(1200, 200);
            pane->send(SCI_SETCODEPAGE, SC_CP_UTF8);
            pane->send(SCI_SETWRAPMODE, SC_WRAP_NONE);
            pane->send(SCI_SETLAYOUTCACHE, SC_CACHE_DOCUMENT);
            pane->send(SCI_SETHSCROLLBAR, false);
        }
        QStringList families{QFontDatabase::systemFont(QFontDatabase::FixedFont).family(),
            QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()};
        const auto installed = QFontDatabase::families();
        for (const auto* family : {"Consolas", "Cascadia Mono", "Arial", "Times New Roman", "Menlo"})
            if (installed.contains(QLatin1String(family))) families.append(QLatin1String(family));
        families.removeDuplicates();
        QByteArray alphabet;
        for (int repeat = 0; repeat < 3; ++repeat)
            for (int ch = 32; ch < 127; ++ch) alphabet.append(static_cast<char>(ch));
        const std::vector<QByteArray> samples{
            QByteArray(1024, 'a'), alphabet,
            QByteArray("AVATAR office ffi ffl != === => ").repeated(4),
            QByteArray("  leading and trailing spaces    "),
            QByteArray("\xce\xb2 \xe6\x97\xa5 e\xcc\x81 \xf0\x9f\x9a\x80 "),
            QByteArray("abc \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 xyz"),
            QByteArray("a\tb\nsecond line\r\nthird")};
        int cases = 0;
        for (const auto& family : families) for (const auto size : {3, 11, 17}) for (int style = 0; style < 3; ++style) {
            for (auto* pane : {&reference, &optimized}) {
                const auto name = family.toUtf8();
                pane->sends(SCI_STYLESETFONT, STYLE_DEFAULT, name.constData());
                pane->send(SCI_STYLESETSIZE, STYLE_DEFAULT, size);
                pane->send(SCI_STYLESETBOLD, STYLE_DEFAULT, style == 1);
                pane->send(SCI_STYLESETITALIC, STYLE_DEFAULT, style == 2);
                pane->send(SCI_STYLESETCHECKMONOSPACED, STYLE_DEFAULT, pane == &optimized);
                pane->send(SCI_STYLECLEARALL);
            }
            for (const auto& sample : samples) {
                for (auto* pane : {&reference, &optimized}) {
                    pane->sends(SCI_SETTEXT, 0, sample.constData());
                    pane->send(SCI_SETXOFFSET, 0);
                }
                for (sptr_t position = 0; position <= sample.size();) {
                    const auto before = reference.send(SCI_POINTXFROMPOSITION, 0, position);
                    const auto after = optimized.send(SCI_POINTXFROMPOSITION, 0, position);
                    if (before != after)
                        throw std::runtime_error(QString("Checked-monospace positions differ: %1, size %2, style %3, byte %4")
                            .arg(family).arg(size).arg(style).arg(position).toStdString());
                    if (position == sample.size()) break;
                    position = reference.send(SCI_POSITIONAFTER, position);
                }
                if (sample == samples.front())
                    check(reference.send(SCI_POINTXFROMPOSITION, 0, sample.size()) > reference.send(SCI_POINTXFROMPOSITION, 0, 0),
                        "Text measurement fixture produced no actual glyph widths.");
                ++cases;
            }
        }
        std::fprintf(stderr, "Public Scintilla checked-monospace equivalence: %d font/style/text cases, scale factor %s\n",
            cases, qgetenv("QT_SCALE_FACTOR").constData());
    }
    void queueToolsRefresh() {
        if (!toolsTimer || tearingDown || closingWindow || tabs->count() == 0) return;
        if ((jsonDock && !jsonDock->isHidden()) || comparisonEnabled) toolsTimer->start();
    }
    void openJson(JsonView requested) {
        const bool created = jsonDock == nullptr;
        if (created) {
            jsonDock = new QDockWidget("JSON Inspector", this);
            jsonDock->setObjectName("json-inspector-dock");
            jsonDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
            jsonPanel = new JsonPanel(jsonDock);
            jsonDock->setWidget(jsonPanel);
            addDockWidget(Qt::RightDockWidgetArea, jsonDock);
            jsonPanel->applyTheme(dark);
            jsonPanel->selectSource = [this](qint64 start, qint64 end) { guarded([&] {
                check(!macroPlaybackActive && tabs->count() && current().id == jsonDocument &&
                    document_revision(*controller, jsonDocument) == jsonRevision,
                    "The JSON source changed. Wait for the preview to refresh and select the node again.");
                auto* pane = current().primary;
                check(start >= 0 && end >= start && end <= pane->send(SCI_GETLENGTH), "Invalid JSON source selection.");
                pane->send(SCI_SETSEL, start, end);
                pane->send(SCI_SCROLLCARET);
            }); };
            connect(jsonDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
                if (visible) queueToolsRefresh();
                syncToolStates();
            });
        }
        jsonDock->show();
        jsonDock->raise();
        // The inspector is a side panel; it never disturbs the left/right document groups.
        if (created) resizeDocks({jsonDock}, {std::max(380, width() / 3)}, Qt::Horizontal);
        refreshJson(true);
        jsonPanel->setView(requested);
        syncToolStates();
    }
    void syncToolStates() {
        const bool inspecting = jsonDock && !jsonDock->isHidden();
        commands.at("json_inspector")->setChecked(inspecting);
        commands.at("toggle_comparison")->setChecked(comparisonEnabled);
        for (const auto* id : {"json_view_tree", "json_view_graph", "json_view_pretty"})
            commands.at(id)->setEnabled(inspecting);
        if (!inspecting || !jsonPanel) return;
        commands.at("json_view_tree")->setChecked(jsonPanel->view() == JsonView::Tree);
        commands.at("json_view_graph")->setChecked(jsonPanel->view() == JsonView::Graph);
        commands.at("json_view_pretty")->setChecked(jsonPanel->view() == JsonView::Pretty);
    }
    void refreshJson(bool force = false) {
        if (!jsonDock || jsonDock->isHidden() || tabs->count() == 0 || macroPlaybackActive || tearingDown) return;
        try {
            const auto id = current().id;
            const auto revision = document_revision(*controller, id);
            if (!force && id == jsonDocument && revision == jsonRevision) return;
            check(current().primary->send(SCI_GETLENGTH) <= 2 * 1024 * 1024, "JSON preview supports at most 2 MiB.");
            const auto contents = text(current().primary);
            const auto model = QJsonDocument::fromJson(qs(json_preview(rs(contents))).toUtf8()).object();
            jsonPanel->setPreview(model, qs(document_title(*controller, id)), contents, id != jsonDocument);
            jsonDocument = id;
            jsonRevision = revision;
        } catch (const std::exception& error) {
            jsonDocument = 0;
            jsonPanel->setError(QString::fromUtf8(error.what()));
            qWarning("JSON preview: %s", error.what());
        }
    }
    DocumentView* viewInGroup(int group) {
        auto* page = tabs->currentInGroup(group);
        for (auto& view : views) if (view.page == page) return &view;
        return nullptr;
    }
    QJsonObject comparisonPreferences() const { return options.value("comparison").toObject(); }
    void moveDocumentToGroup(std::uint64_t id, int group) {
        check(!macroPlaybackActive && !macroRecording, "Finish macro playback or recording before moving tabs.");
        check(views.size() >= 2, "Open a second tab before splitting document views.");
        const auto found = std::find_if(views.begin(), views.end(), [id](const auto& view) { return view.id == id; });
        check(found != views.end(), "The document is no longer open.");
        tabs->movePage(found->page, group);
        found->focused->setFocus();
        comparisonCacheValid = false;
        recoveryPending = true;
        checkpointNow();
    }
    void moveAllToOtherGroup() {
        check(views.size() >= 2, "Open another tab before moving document views.");
        const int source = tabs->activeGroup();
        std::vector<QWidget*> pages;
        for (const auto& view : views) if (tabs->groupOf(view.page) == source) pages.push_back(view.page);
        for (auto* page : pages) tabs->movePage(page, 1 - source);
        comparisonCacheValid = false;
        checkpointNow();
    }
    void clearComparisonMarkers() {
        const QScopedValueRollback<bool> syncing(synchronizingComparison, true);
        for (const auto& view : views) {
            for (const auto marker : {12, 13, 14}) view.primary->send(SCI_MARKERDELETEALL, marker);
            const auto indicator = view.primary->send(SCI_GETINDICATORCURRENT);
            view.primary->send(SCI_SETINDICATORCURRENT, 24);
            view.primary->send(SCI_INDICATORCLEARRANGE, 0, view.primary->send(SCI_GETLENGTH));
            view.primary->send(SCI_SETINDICATORCURRENT, indicator);
            for (auto* pane : {view.primary, view.clone}) {
                const auto line = pane->send(SCI_DOCLINEFROMVISIBLE, pane->send(SCI_GETFIRSTVISIBLELINE));
                pane->send(SCI_ANNOTATIONCLEARALL);
                auto* editor = static_cast<WorkspaceEditor*>(pane);
                editor->leadingRows = 0;
                editor->showLeadingRows(0);
                if (pane->property("comparison-wrap").isValid()) {
                    pane->send(SCI_SETWRAPMODE, pane->property("comparison-wrap").toInt());
                    pane->send(SCI_SETZOOM, pane->property("comparison-zoom").toInt());
                    pane->setProperty("comparison-wrap", QVariant());
                    pane->setProperty("comparison-zoom", QVariant());
                    pane->send(SCI_SETFIRSTVISIBLELINE, pane->send(SCI_VISIBLEFROMDOCLINE, line));
                }
            }
        }
        comparisonAligned = false;
    }
    void stopComparison() {
        comparisonEnabled = false;
        comparisonTarget = comparisonLeft = 0;
        comparisonCacheValid = false;
        comparisonResult = {};
        comparisonHunk = -1;
        clearComparisonMarkers();
        comparisonStatus->hide();
        for (const auto* id : {"stop_comparison", "next_difference", "previous_difference"})
            if (commands.count(id)) commands.at(id)->setEnabled(false);
        if (commands.count("toggle_comparison")) syncToolStates();
    }
    void invalidateComparison() {
        if (!comparisonEnabled) return;
        if (comparisonCacheValid) {
            auto* left = viewInGroup(0);
            auto* right = viewInGroup(1);
            if (left && right && left->id == comparisonLeft && right->id == comparisonTarget &&
                document_revision(*controller, left->id) == comparisonLeftRevision &&
                document_revision(*controller, right->id) == comparisonRightRevision) return;
        }
        comparisonCacheValid = false;
        comparisonResult = {};
        clearComparisonMarkers();
        comparisonStatus->setText("Comparison changed - run Compare These Two Tabs again.");
    }
    void beginComparison(std::uint64_t right) {
        moveDocumentToGroup(right, 1);
        if (!tabs->groupCount(0)) {
            for (const auto& view : views) if (view.id != right) { moveDocumentToGroup(view.id, 0); break; }
        }
        chooseComparison();
    }
    void chooseComparison() {
        compareWithAdjacentTab();
        check(viewInGroup(0) && viewInGroup(1), "Move a tab to Other View, then select one tab in each view.");
        comparisonEnabled = true;
        comparisonCacheValid = false;
        comparisonStatus->show();
        for (const auto* id : {"stop_comparison", "next_difference", "previous_difference"}) commands.at(id)->setEnabled(true);
        syncToolStates();
        refreshComparison();
    }
    // With only one populated view, compare the active tab against its neighbour, as the reference plugin does.
    void compareWithAdjacentTab() {
        if (viewInGroup(0) && viewInGroup(1)) return;
        check(views.size() >= 2, "Open a second document before comparing.");
        const int source = tabs->activeGroup();
        const int start = tabs->groupStart(source);
        const int count = tabs->groupCount(source);
        check(count >= 2, "Open a second document in this view before comparing.");
        const int active = tabs->currentIndex();
        const int neighbour = active + 1 < start + count ? active + 1 : active - 1;
        auto* keep = tabs->currentWidget();
        auto* page = tabs->widget(neighbour);
        check(std::any_of(views.begin(), views.end(), [page](const auto& view) { return view.page == page; }),
            "The adjacent tab is no longer open.");
        tabs->movePage(page, 1 - source);
        tabs->setCurrentWidget(keep);
        comparisonCacheValid = false;
        recoveryPending = true;
        checkpointNow();
    }
    void swapComparedTabs() {
        auto* left = viewInGroup(0);
        auto* right = viewInGroup(1);
        check(left && right, "Select a tab in each view.");
        auto* leftPage = left->page;
        auto* rightPage = right->page;
        tabs->movePage(leftPage, 1);
        tabs->movePage(rightPage, 0);
        tabs->selectInGroup(1, leftPage);
        comparisonCacheValid = false;
        checkpointNow();
        if (comparisonEnabled) refreshComparison();
    }
    void comparisonOptions() {
        QDialog dialog(this);
        dialog.setWindowTitle("Comparison Options");
        auto* layout = new QFormLayout(&dialog);
        auto preferences = comparisonPreferences();
        auto* detail = new QComboBox;
        detail->addItem("Line differences", "lines");
        detail->addItem("Word differences within changed lines", "words");
        detail->addItem("Character differences within changed lines", "characters");
        detail->setCurrentIndex(detail->findData(preferences.value("detail").toString("characters")));
        layout->addRow("Comparison detail", detail);
        std::map<QString, QCheckBox*> checks;
        for (const auto& entry : {
            std::make_pair("ignore_whitespace", "Ignore whitespace within lines"),
            std::make_pair("ignore_case", "Ignore letter case"),
            std::make_pair("ignore_eol", "Ignore line-ending differences"),
            std::make_pair("ignore_blank_lines", "Ignore blank lines"),
            std::make_pair("auto_recompare", "Automatically recompare after edits / tab changes"),
            std::make_pair("align_matches", "Align matching lines with non-editable blank rows"),
            std::make_pair("sync_scroll", "Synchronize compared views")}) {
            auto* checkBox = new QCheckBox;
            const bool defaultValue = QString(entry.first) == "auto_recompare" || QString(entry.first) == "align_matches" || QString(entry.first) == "sync_scroll";
            checkBox->setChecked(preferences.value(entry.first).toBool(defaultValue));
            checks[entry.first] = checkBox;
            layout->addRow(entry.second, checkBox);
        }
        auto* explanation = new QLabel("Alignment temporarily turns off wrapping and links zoom. Expand folded/hidden lines to align them.\n"
            "Text, undo history, bookmarks and syntax highlighting remain in the original editable tabs.");
        explanation->setWordWrap(true);
        layout->addRow(explanation);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        preferences["detail"] = detail->currentData().toString();
        for (const auto& entry : checks) preferences[entry.first] = entry.second->isChecked();
        auto updated = options;
        updated["comparison"] = preferences;
        store_settings(*controller, rs(QJsonDocument(updated).toJson(QJsonDocument::Compact)));
        options = updated;
        comparisonCacheValid = false;
        if (comparisonEnabled) refreshComparison();
    }
    void refreshComparison() {
        if (!comparisonEnabled || macroPlaybackActive || tearingDown) return;
        const QScopedValueRollback<bool> syncing(synchronizingComparison, true);
        try {
            auto* left = viewInGroup(0);
            auto* right = viewInGroup(1);
            if (!left || !right) {
                clearComparisonMarkers();
                comparisonCacheValid = false;
                comparisonResult = {};
                comparisonStatus->setText("Comparison paused - select a tab in each view.");
                return;
            }
            const auto leftRevision = document_revision(*controller, left->id);
            const auto rightRevision = document_revision(*controller, right->id);
            if (comparisonCacheValid && comparisonLeft == left->id && comparisonTarget == right->id &&
                comparisonLeftRevision == leftRevision && comparisonRightRevision == rightRevision) return;
            const auto sharedZoom = left->primary->send(SCI_GETZOOM);
            clearComparisonMarkers();
            check(left->primary->send(SCI_GETLENGTH) <= 2 * 1024 * 1024 &&
                right->primary->send(SCI_GETLENGTH) <= 2 * 1024 * 1024, "Comparison supports at most 2 MiB per document.");
            const auto preferences = comparisonPreferences();
            const auto result = QJsonDocument::fromJson(qs(compare_documents(rs(text(left->primary)), rs(text(right->primary)),
                rs(QJsonDocument(preferences).toJson(QJsonDocument::Compact)))).toUtf8()).object();
            comparisonAligned = preferences.value("align_matches").toBool(true) &&
                left->primary->send(SCI_GETALLLINESVISIBLE) && right->primary->send(SCI_GETALLLINESVISIBLE);
            for (auto* view : {left, right}) for (auto* pane : {view->primary, view->clone}) {
                if (comparisonAligned) {
                    pane->setProperty("comparison-wrap", static_cast<int>(pane->send(SCI_GETWRAPMODE)));
                    pane->setProperty("comparison-zoom", static_cast<int>(pane->send(SCI_GETZOOM)));
                    pane->send(SCI_SETWRAPMODE, SC_WRAP_NONE);
                    pane->send(SCI_SETZOOM, sharedZoom);
                    pane->send(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_STANDARD);
                    pane->send(SCI_STYLESETBACK, 252, dark ? 0x383430 : 0xeeeeee);
                }
                const auto colors = dark ? std::array<int, 3>{0x554583, 0x5b7041, 0x396776} :
                    std::array<int, 3>{0xe1dcff, 0xddeedb, 0xc4edff};
                for (int marker = 12; marker <= 14; ++marker) {
                    pane->send(SCI_MARKERDEFINE, marker, SC_MARK_BACKGROUND);
                    pane->send(SCI_MARKERSETBACK, marker, colors[static_cast<std::size_t>(marker - 12)]);
                }
                pane->send(SCI_INDICSETSTYLE, 24, INDIC_STRAIGHTBOX);
                pane->send(SCI_INDICSETFORE, 24, dark ? 0x70ceff : 0x008acc);
                pane->send(SCI_INDICSETALPHA, 24, 85);
                pane->send(SCI_INDICSETOUTLINEALPHA, 24, 160);
            }
            for (const auto& value : result["marks"].toArray()) {
                const auto mark = value.toObject();
                auto* pane = mark["side"].toString() == "left" ? left->primary : right->primary;
                pane->send(SCI_MARKERADD, mark["line"].toInt(),
                    mark["kind"].toString() == "changed" ? 14 : mark["side"].toString() == "left" ? 12 : 13);
            }
            for (const auto& value : result["spans"].toArray()) {
                const auto span = value.toObject();
                auto* pane = span["side"].toString() == "left" ? left->primary : right->primary;
                const auto previous = pane->send(SCI_GETINDICATORCURRENT);
                const auto previousValue = pane->send(SCI_GETINDICATORVALUE);
                pane->send(SCI_SETINDICATORCURRENT, 24);
                pane->send(SCI_SETINDICATORVALUE, 1);
                pane->send(SCI_INDICATORFILLRANGE, span["start"].toInteger(), span["end"].toInteger() - span["start"].toInteger());
                pane->send(SCI_SETINDICATORCURRENT, previous);
                pane->send(SCI_SETINDICATORVALUE, previousValue);
            }
            if (comparisonAligned) for (const auto& value : result["gaps"].toArray()) {
                const auto gap = value.toObject();
                auto* view = gap["side"].toString() == "left" ? left : right;
                const int line = gap["before_line"].toInt();
                const int rows = gap["rows"].toInt();
                if (line == 0) {
                    for (auto* pane : {view->primary, view->clone}) {
                        auto* editor = static_cast<WorkspaceEditor*>(pane);
                        editor->leadingRows = rows;
                        editor->showLeadingRows(rows);
                    }
                } else {
                    const QByteArray blank = QByteArray(" ") + QByteArray(rows - 1, '\n');
                    view->primary->sends(SCI_ANNOTATIONSETTEXT, line - 1, blank.constData());
                    view->primary->send(SCI_ANNOTATIONSETSTYLE, line - 1, 252);
                }
            }
            comparisonResult = result;
            comparisonLeft = left->id;
            comparisonTarget = right->id;
            comparisonLeftRevision = leftRevision;
            comparisonRightRevision = rightRevision;
            comparisonCacheValid = true;
            comparisonHunk = -1;
            const auto hunks = result["hunks"].toArray();
            comparisonStatus->setText(hunks.isEmpty() ?
                (result["ignored_differences"].toBool() ? "Compare: equal with ignored differences" : "Compare: identical") :
                QString("Compare: %1 blocks | -%2 +%3").arg(hunks.size()).arg(result["removed"].toInt()).arg(result["added"].toInt()));
            if (result["detail_limited"].toBool()) comparisonStatus->setText(comparisonStatus->text() + " | detail limited");
            if (preferences.value("align_matches").toBool(true) && !comparisonAligned)
                comparisonStatus->setText(comparisonStatus->text() + " | expand folds to align");
        } catch (const std::exception& error) {
            comparisonCacheValid = false;
            comparisonResult = {};
            clearComparisonMarkers();
            comparisonStatus->setText("Compare: " + QString::fromUtf8(error.what()));
            qWarning("Comparison: %s", error.what());
        }
    }
    void navigateComparison(int direction) {
        refreshComparison();
        auto* left = viewInGroup(0);
        auto* right = viewInGroup(1);
        const auto hunks = comparisonResult["hunks"].toArray();
        if (!left || !right || hunks.isEmpty()) { statusBar()->showMessage("No comparison differences to navigate."); return; }
        const int count = static_cast<int>(hunks.size());
        comparisonHunk = comparisonHunk < 0 ? (direction > 0 ? 0 : count - 1) : (comparisonHunk + direction + count) % count;
        const auto hunk = hunks[comparisonHunk].toObject();
        {
            const QScopedValueRollback<bool> syncing(synchronizingComparison, true);
            for (const auto& side : {std::make_pair(left->primary, hunk["left_start"].toInt()),
                std::make_pair(right->primary, hunk["right_start"].toInt())}) {
                const auto line = std::min<sptr_t>(side.second, side.first->send(SCI_GETLINECOUNT) - 1);
                side.first->send(SCI_ENSUREVISIBLE, line);
                side.first->send(SCI_GOTOLINE, line);
                side.first->send(SCI_SETFIRSTVISIBLELINE, side.first->send(SCI_VISIBLEFROMDOCLINE, std::max<sptr_t>(0, line - 3)));
            }
        }
        syncComparisonScroll(false);
        statusBar()->showMessage(QString("Comparison difference %1 of %2").arg(comparisonHunk + 1).arg(count));
    }
    void syncComparisonScroll(bool fromRight) {
        if (!comparisonEnabled || !comparisonCacheValid || synchronizingComparison ||
            !comparisonPreferences().value("sync_scroll").toBool(true)) return;
        auto* left = viewInGroup(0);
        auto* right = viewInGroup(1);
        if (!left || !right || left->id != comparisonLeft || right->id != comparisonTarget) return;
        const QScopedValueRollback<bool> syncing(synchronizingComparison, true);
        auto* from = static_cast<WorkspaceEditor*>(fromRight ? right->primary : left->primary);
        auto* to = static_cast<WorkspaceEditor*>(fromRight ? left->primary : right->primary);
        if (comparisonAligned) {
            const auto first = from->send(SCI_GETFIRSTVISIBLELINE);
            auto row = first + from->leadingRows - from->remainingLeadingRows;
            if (first > 0 && from->remainingLeadingRows > 0) {
                from->showLeadingRows(0);
                row = first + from->leadingRows;
            }
            to->showLeadingRows(static_cast<int>(std::max<sptr_t>(0, to->leadingRows - row)));
            to->send(SCI_SETFIRSTVISIBLELINE, std::max<sptr_t>(0, row - to->leadingRows));
        } else {
            const auto line = from->send(SCI_DOCLINEFROMVISIBLE, from->send(SCI_GETFIRSTVISIBLELINE));
            sptr_t mapped = line;
            sptr_t offset = 0;
            for (const auto& value : comparisonResult["hunks"].toArray()) {
                const auto hunk = value.toObject();
                const auto start = hunk[fromRight ? "right_start" : "left_start"].toInt();
                const auto length = hunk[fromRight ? "right_len" : "left_len"].toInt();
                const auto otherStart = hunk[fromRight ? "left_start" : "right_start"].toInt();
                const auto otherLength = hunk[fromRight ? "left_len" : "right_len"].toInt();
                if (line < start) break;
                if (line < start + length) { mapped = otherStart + std::min<sptr_t>(line - start, std::max(0, otherLength - 1)); offset = 0; break; }
                offset += otherLength - length;
            }
            mapped = std::clamp<sptr_t>(mapped + offset, 0, to->send(SCI_GETLINECOUNT) - 1);
            to->send(SCI_SETFIRSTVISIBLELINE, to->send(SCI_VISIBLEFROMDOCLINE, mapped));
        }
    }
    void applyEditorPreferences(ScintillaEditBase* pane) {
        const auto display = options.value("view").toObject();
        pane->send(SCI_SETWRAPMODE, wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
        pane->send(SCI_SETTABWIDTH, options["editor"].toObject().value("tab_width").toInt(4));
        pane->send(SCI_SETUSETABS, options.value("use_tabs").toBool());
        updateGutter(pane);
        pane->send(SCI_SETINDENTATIONGUIDES, display.value("indent_guides").toBool(true) ? SC_IV_LOOKBOTH : SC_IV_NONE);
        pane->send(SCI_SETVIEWWS, display.value("show_whitespace").toBool() ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
        pane->send(SCI_SETVIEWEOL, display.value("show_eol").toBool());
        pane->send(SCI_SETENDATLASTLINE, !display.value("scroll_past_end").toBool());
        pane->send(SCI_SETVIRTUALSPACEOPTIONS, SCVS_RECTANGULARSELECTION |
            (display.value("virtual_space").toBool() ? SCVS_USERACCESSIBLE : 0));
        pane->send(SCI_SETMULTIPLESELECTION, display.value("multi_selection").toBool(true));
        pane->send(SCI_SETADDITIONALSELECTIONTYPING, display.value("additional_typing").toBool(true));
        pane->send(SCI_SETBACKSPACEUNINDENTS, display.value("backspace_unindent").toBool());
        const auto caret = display.value("caret_width").toInt(1);
        pane->send(SCI_SETCARETSTYLE, caret < 4 ? CARETSTYLE_LINE :
            CARETSTYLE_BLOCK | (caret == 5 ? CARETSTYLE_BLOCK_AFTER : 0));
        pane->send(SCI_SETCARETWIDTH, caret < 4 ? caret : 1);
        pane->send(SCI_SETCARETPERIOD, display.value("caret_period").toInt(500));
        const auto indent = display.value("wrap_indent").toString("fixed");
        pane->send(SCI_SETWRAPINDENTMODE, indent == "same" ? SC_WRAPINDENT_SAME : indent == "indent" ?
            SC_WRAPINDENT_INDENT : indent == "deep_indent" ? SC_WRAPINDENT_DEEPINDENT : SC_WRAPINDENT_FIXED);
        if (!options.value("completion").toBool(true)) pane->send(SCI_AUTOCCANCEL);
        if (!options.value("calltips").toBool(true)) pane->send(SCI_CALLTIPCANCEL);
    }
    void applyOptions() {
        const QScopedValueRollback<bool> applying(applyingOptions, true);
        dark = options["editor"].toObject().value("dark").toBool();
        wrap = options["editor"].toObject().value("wrap").toBool();
        applyPalette();
        const auto shortcuts = options.value("shortcuts").toObject();
        for (const auto& entry : commands) {
            const auto sequence = shortcuts.contains(entry.first) ? shortcuts.value(entry.first).toString() :
                entry.second->property("defaultShortcut").toString();
            entry.second->setShortcut(QKeySequence(sequence, QKeySequence::PortableText));
        }
        for (auto& view : views) {
            for (auto* pane : {view.primary, view.clone}) {
                applyEditorPreferences(pane);
            }
            restyle(view);
        }
        mappedDocument = 0;
        updateMap();
        commands.at("word_wrap")->setChecked(wrap);
        commands.at("dark_theme")->setChecked(dark);
        const auto display = options.value("view").toObject();
        commands.at("show_whitespace")->setChecked(display.value("show_whitespace").toBool() || display.value("show_eol").toBool());
        comparisonCacheValid = false;
        jsonDocument = 0;
        if (jsonPanel) jsonPanel->applyTheme(dark);
        queueToolsRefresh();
    }
    void updateGutter(ScintillaEditBase* pane) {
        const auto display = options.value("view").toObject();
        compactGutter(pane, display.value("line_numbers").toBool(true), display.value("bookmark_margin").toBool(true));
    }
    void persistFlags() {
        if (!settingsReady || applyingOptions) return;
        auto updated = options;
        auto editorOptions = updated["editor"].toObject();
        editorOptions["dark"] = dark;
        editorOptions["wrap"] = wrap;
        updated["editor"] = editorOptions;
        const auto bytes = QJsonDocument(updated).toJson(QJsonDocument::Compact);
        store_settings(*controller, rs(bytes));
        options = updated;
    }
    void preferencesDialog() {
        check(settingsReady, "Settings could not be loaded. Correct the retained settings file first.");
        QDialog dialog(this);
        dialog.setWindowTitle("Preferences");
        dialog.resize(620, 610);
        auto* outer = new QVBoxLayout(&dialog);
        auto* pages = new QTabWidget(&dialog);
        outer->addWidget(pages);
        auto page = [pages](const QString& title) {
            auto* content = new QWidget;
            auto* form = new QFormLayout(content);
            auto* scroll = new QScrollArea;
            scroll->setWidgetResizable(true); scroll->setWidget(content);
            pages->addTab(scroll, title);
            return form;
        };
        auto* layout = page("General");
        auto* font = new QFontComboBox;
        font->setCurrentFont(QFont(options.value("font_family").toString()));
        auto* fontSize = new QSpinBox;
        fontSize->setRange(6, 48);
        fontSize->setValue(options.value("font_size").toInt(11));
        auto* tabWidth = new QSpinBox;
        tabWidth->setRange(1, 16);
        tabWidth->setValue(options["editor"].toObject().value("tab_width").toInt(4));
        auto* tabsOption = new QCheckBox;
        tabsOption->setChecked(options.value("use_tabs").toBool());
        auto* indent = new QCheckBox;
        indent->setChecked(options.value("auto_indent").toBool(true));
        auto* restore = new QCheckBox;
        restore->setChecked(options.value("restore_session").toBool(true));
        layout->addRow("Editor font", font);
        layout->addRow("Font size", fontSize);
        layout->addRow("Tab width", tabWidth);
        layout->addRow("Use tabs instead of spaces", tabsOption);
        layout->addRow("Automatic indentation", indent);
        restore->setToolTip("Keep all open tabs, including unsaved text, when exiting. Explicit tab closes still ask to save or discard.");
        layout->addRow("Remember open tabs and unsaved text", restore);
        auto* maximized = new QCheckBox;
        maximized->setChecked(options.value("start_maximized").toBool(true));
        layout->addRow("Start maximized (not full screen)", maximized);
        auto* editing = page("Editing");
        auto* completion = new QCheckBox;
        completion->setChecked(options.value("completion").toBool(true));
        editing->addRow("Automatic completion", completion);
        auto* completionMode = new QComboBox;
        completionMode->addItems({"Disabled", "Functions", "Document words", "Functions and words"});
        completionMode->setCurrentIndex(options.value("completion_mode").toInt(1));
        editing->addRow("Automatic completion source", completionMode);
        auto* trigger = new QSpinBox;
        trigger->setRange(1, 9); trigger->setValue(options.value("completion_min_chars").toInt(2));
        editing->addRow("Start completion after characters", trigger);
        auto* ignoreNumbers = new QCheckBox;
        ignoreNumbers->setChecked(options.value("completion_ignore_numbers").toBool());
        editing->addRow("Ignore numeric prefixes", ignoreNumbers);
        auto* calltips = new QCheckBox;
        calltips->setChecked(options.value("calltips").toBool(true));
        editing->addRow("Automatic function parameter hints", calltips);
        auto* display = page("Display");
        const auto displayOptions = options.value("view").toObject();
        QMap<QString, QCheckBox*> flags;
        for (const auto& definition : std::vector<std::pair<QString, QString>>{
            {"line_numbers", "Line number margin"}, {"bookmark_margin", "Bookmark margin"},
            {"indent_guides", "Indentation guides"}, {"show_whitespace", "Show spaces and tabs"},
            {"show_eol", "Show line endings"}, {"scroll_past_end", "Scroll beyond the last line"},
            {"virtual_space", "Virtual space in stream selections"}, {"multi_selection", "Multiple selections"},
            {"additional_typing", "Type into additional selections"}, {"backspace_unindent", "Backspace unindents"}}) {
            auto* flag = new QCheckBox;
            flag->setChecked(displayOptions.value(definition.first).toBool());
            flags.insert(definition.first, flag);
            display->addRow(definition.second, flag);
        }
        auto* caretWidth = new QComboBox;
        caretWidth->addItems({"Invisible", "1 pixel", "2 pixels", "3 pixels", "Block", "Block after caret"});
        caretWidth->setCurrentIndex(displayOptions.value("caret_width").toInt(1));
        display->addRow("Caret appearance", caretWidth);
        auto* caretPeriod = new QSpinBox;
        caretPeriod->setRange(0, 1000); caretPeriod->setSingleStep(100);
        caretPeriod->setValue(displayOptions.value("caret_period").toInt(500));
        display->addRow("Caret blink milliseconds (0 disables)", caretPeriod);
        auto* wrapIndent = new QComboBox;
        wrapIndent->addItems({"fixed", "same", "indent", "deep_indent"});
        wrapIndent->setCurrentText(displayOptions.value("wrap_indent").toString("fixed"));
        display->addRow("Wrapped-line indentation", wrapIndent);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        outer->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        auto updated = options;
        updated["font_family"] = font->currentFont().family();
        updated["font_size"] = fontSize->value();
        updated["use_tabs"] = tabsOption->isChecked();
        updated["auto_indent"] = indent->isChecked();
        updated["restore_session"] = restore->isChecked();
        updated["start_maximized"] = maximized->isChecked();
        updated["completion"] = completion->isChecked() && completionMode->currentIndex() != 0;
        updated["completion_mode"] = completionMode->currentIndex();
        updated["completion_min_chars"] = trigger->value();
        updated["completion_ignore_numbers"] = ignoreNumbers->isChecked();
        updated["calltips"] = calltips->isChecked();
        auto viewOptions = displayOptions;
        for (auto it = flags.begin(); it != flags.end(); ++it) viewOptions[it.key()] = it.value()->isChecked();
        viewOptions["caret_width"] = caretWidth->currentIndex();
        viewOptions["caret_period"] = caretPeriod->value();
        viewOptions["wrap_indent"] = wrapIndent->currentText();
        updated["view"] = viewOptions;
        auto editorOptions = updated["editor"].toObject();
        editorOptions["tab_width"] = tabWidth->value();
        updated["editor"] = editorOptions;
        const auto bytes = QJsonDocument(updated).toJson(QJsonDocument::Compact);
        store_settings(*controller, rs(bytes));
        options = updated;
        applyOptions();
    }
    void importPreferencesDialog() {
        check(settingsReady, "Correct the retained settings file before importing preferences.");
        const auto path = QFileDialog::getOpenFileName(this, "Import Preferences", {},
            "Preferences (*.xml *.json);;Notepad++ config or stylers XML (*.xml);;Notepad Star preferences (*.json)");
        if (path.isEmpty()) return;
        QFile file(path);
        check(file.open(QIODevice::ReadOnly), "Cannot open configuration XML.");
        const auto xml = file.read(2 * 1024 * 1024 + 1);
        check(file.error() == QFileDevice::NoError, "Cannot read configuration XML.");
        const auto before = qs(current_settings(*controller)).toUtf8();
        ConfigImport imported;
        QJsonObject candidate;
        if (QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) == 0) {
            candidate = QJsonDocument::fromJson(qs(normalize_settings_json(rs(xml))).toUtf8()).object();
            imported.patch = candidate;
        } else {
            imported = importNotepadConfig(xml);
            candidate = mergeConfigPatch(QJsonDocument::fromJson(before).object(), imported.patch);
        }
        const auto encoded = QJsonDocument(candidate).toJson(QJsonDocument::Compact);
        validate_settings_json(rs(encoded));
        QDialog dialog(this);
        dialog.setWindowTitle("Review Preference Import");
        dialog.resize(720, 580);
        auto* layout = new QVBoxLayout(&dialog);
        auto* report = new QPlainTextEdit(&dialog);
        report->setReadOnly(true);
        report->setPlainText(path + "\nSHA-256: " + QString::fromLatin1(QCryptographicHash::hash(xml, QCryptographicHash::Sha256).toHex()) +
            "\n\nSupported changes:\n" + QString::fromUtf8(QJsonDocument(imported.patch).toJson(QJsonDocument::Indented)) +
            "\nNot imported:\n" + (imported.unsupported.isEmpty() ? "None." : imported.unsupported.join('\n')) +
            "\n\nThe current profile is backed up before saving. The source is never changed and nothing is executed."
            "\nXML import does not migrate paths or shortcuts. Notepad Star JSON restores its validated profile, including shortcuts and recents.");
        layout->addWidget(report);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        check(qs(current_settings(*controller)).toUtf8() == before,
            "Preferences changed while the import was being reviewed. Review the import again.");
        const auto backup = import_settings(*controller, rs(encoded));
        options = candidate;
        applyOptions();
        QMessageBox message(this);
        message.setTextFormat(Qt::PlainText);
        message.setWindowTitle("Preferences Imported");
        message.setText("Supported preferences were applied.\nPrevious profile: " + pathText(backup) +
            "\nThe source file was not modified.");
        message.exec();
    }
    void resetPreferencesDialog() {
        if (!confirmOperation("Restore Default Preferences", settingsPath +
            "\n\nBack up this profile and restore Notepad Star defaults? This can repair malformed settings."
            "\nDocuments, sessions and extension packages are not removed.")) return;
        const auto backup = reset_settings(*controller, filePath(settingsPath));
        options = QJsonDocument::fromJson(qs(current_settings(*controller)).toUtf8()).object();
        settingsReady = true;
        applyOptions();
        QMessageBox message(this);
        message.setTextFormat(Qt::PlainText);
        message.setWindowTitle("Default Preferences Restored");
        message.setText("Previous settings: " + pathText(backup));
        message.exec();
    }
    void shortcutsDialog() {
        check(settingsReady, "Settings could not be loaded.");
        QDialog dialog(this);
        dialog.setWindowTitle("Shortcut Mapper");
        dialog.resize(580, 540);
        auto* layout = new QVBoxLayout(&dialog);
        auto* table = new QTableWidget(static_cast<int>(commands.size()), 2);
        table->setHorizontalHeaderLabels({"Command", "Shortcut (empty disables)"});
        int row = 0;
        for (const auto& entry : commands) {
            auto* item = new QTableWidgetItem(entry.second->text());
            item->setData(Qt::UserRole, entry.first);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table->setItem(row, 0, item);
            table->setCellWidget(row, 1, new QKeySequenceEdit(entry.second->shortcut()));
            ++row;
        }
        table->setColumnWidth(0, 260);
        table->setColumnWidth(1, 250);
        layout->addWidget(table);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        auto updated = options;
        QJsonObject shortcuts;
        for (int index = 0; index < table->rowCount(); ++index)
            shortcuts[table->item(index, 0)->data(Qt::UserRole).toString()] =
                qobject_cast<QKeySequenceEdit*>(table->cellWidget(index, 1))->keySequence().toString(QKeySequence::PortableText);
        updated["shortcuts"] = shortcuts;
        const auto bytes = QJsonDocument(updated).toJson(QJsonDocument::Compact);
        store_settings(*controller, rs(bytes));
        options = updated;
        applyOptions();
    }
    void reportSearch(const QString& message) {
        search->report(message);
        statusBar()->showMessage(message);
    }
    void openSearch(bool replacing) {
        auto* pane = activeEditor();
        const auto start = pane->send(SCI_GETSELECTIONSTART);
        const auto end = pane->send(SCI_GETSELECTIONEND);
        if (end > start && end - start <= 4096 && pane->send(SCI_GETSELECTIONS) == 1) {
            const auto selected = text(pane).mid(start, end - start);
            if (!selected.contains('\n') && !selected.contains('\r') && !selected.contains('\0'))
                query->setText(QString::fromUtf8(selected));
        }
        search->openPage(replacing);
    }
    void startBufferSearch(bool allOpened, bool countOnly) {
        check(!macroPlaybackActive && !macroRecording, "Finish the macro before searching.");
        check(!activeSearch && !fileSearchRunning && !bufferSearchRunning, "A search is already running. Cancel it first.");
        if (query->text().isEmpty()) { search->openPage(false); return; }
        const auto request = search_request(rust::Str(), rs(query->text().toUtf8()), rust::Str(), searchOptions(false));
        bufferSearchTemplate = qs(request).toUtf8();
        bufferSearchQueue.clear();
        if (allOpened) {
            for (int index = 0; index < tabs->count(); ++index)
                for (const auto& view : views) if (view.page == tabs->widget(index)) bufferSearchQueue.append(view.id);
        } else bufferSearchQueue.append(current().id);
        bufferSearchBytes = 0;
        bufferMatchCount = 0;
        bufferSearchWarnings = bufferSearched = 0;
        bufferCountOnly = countOnly;
        lastSearchError.clear();
        if (!countOnly) { ensureResults(); searchResults->clear(); }
        bufferSearchRunning = true;
        reportSearch(allOpened ? "Finding all matches in both tab groups..." : countOnly ? "Counting matches..." : "Finding all matches...");
        nextBufferSearch();
    }
    void nextBufferSearch() {
        if (!bufferSearchRunning) return;
        while (!bufferSearchQueue.isEmpty()) {
            const auto id = bufferSearchQueue.takeFirst();
            const auto found = std::find_if(views.begin(), views.end(), [id](const auto& view) { return view.id == id; });
            if (found == views.end()) {
                ++bufferSearchWarnings;
                if (!bufferCountOnly) new QTreeWidgetItem(searchResults, {"Closed tab", "", "Skipped: this tab was closed before it could be searched."});
                continue;
            }
            try {
                const auto source = text(found->primary);
                bufferSearchBytes += static_cast<std::size_t>(source.size());
                check(bufferSearchBytes <= 128 * 1024 * 1024, "Find All exceeded the 128 MiB aggregate snapshot budget. Narrow the search.");
                const auto revision = document_revision(*controller, id);
                const auto name = QString(tabs->groupOf(found->page) == 0 ? "[Left] " : "[Right] ") + qs(document_title(*controller, id));
                auto request = QJsonDocument::fromJson(bufferSearchTemplate).object();
                request["text"] = QString::fromUtf8(source);
                auto* task = new SearchTask(this);
                activeSearch = task;
                task->start(QJsonDocument(request).toJson(QJsonDocument::Compact),
                    [this, task, id, revision, source, name](QByteArray response) {
                        activeSearch = nullptr;
                        task->deleteLater();
                        if (!bufferSearchRunning) return;
                        try {
                            const auto currentView = std::find_if(views.begin(), views.end(), [id](const auto& view) { return view.id == id; });
                            if (currentView == views.end() || document_revision(*controller, id) != revision) {
                                ++bufferSearchWarnings;
                                if (!bufferCountOnly) new QTreeWidgetItem(searchResults, {name, "", "Skipped: the document changed while searching."});
                            } else {
                                const auto result = search_response(rs(response), rs(source));
                                check(!result.replaced, "Find All returned a replacement instead of matches.");
                                check(bufferMatchCount + result.count <= 10000, "Find All exceeded 10,000 total matches. Narrow the search; earlier results remain listed.");
                                bufferMatchCount += result.count;
                                ++bufferSearched;
                                if (!bufferCountOnly && !result.hits.empty()) {
                                    auto* group = new QTreeWidgetItem(searchResults, {name, "", QString("%1 match(es)").arg(result.count)});
                                    group->setExpanded(true);
                                    for (const auto& hit : result.hits) {
                                        auto* item = new QTreeWidgetItem(group, {"", QString::number(hit.line), qs(hit.preview)});
                                        item->setData(0, Qt::UserRole + 10, QVariant::fromValue<qulonglong>(id));
                                        item->setData(0, Qt::UserRole + 11, QVariant::fromValue<qulonglong>(revision));
                                        item->setData(0, Qt::UserRole + 2, QVariant::fromValue<qulonglong>(hit.start));
                                        item->setData(0, Qt::UserRole + 3, QVariant::fromValue<qlonglong>(static_cast<qlonglong>(hit.end)));
                                    }
                                }
                            }
                            nextBufferSearch();
                        } catch (const std::exception& error) {
                            bufferSearchRunning = false;
                            bufferSearchQueue.clear();
                            lastSearchError = QString::fromUtf8(error.what());
                            reportSearch("Find All stopped: " + lastSearchError);
                        }
                    },
                    [this, task](const QString& message) {
                        activeSearch = nullptr;
                        task->deleteLater();
                        bufferSearchRunning = false;
                        bufferSearchQueue.clear();
                        lastSearchError = message;
                        reportSearch("Find All stopped: " + message);
                    });
                return;
            } catch (const std::exception& error) {
                bufferSearchRunning = false;
                bufferSearchQueue.clear();
                lastSearchError = QString::fromUtf8(error.what());
                reportSearch("Find All stopped: " + lastSearchError);
                return;
            }
        }
        bufferSearchRunning = false;
        reportSearch(QString("%1 match(es) in %2 document(s)%3.").arg(bufferMatchCount).arg(bufferSearched)
            .arg(bufferSearchWarnings ? QString("; %1 closed/changed tab(s) skipped").arg(bufferSearchWarnings) : QString()));
    }
    SearchOptions searchOptions(bool replacing) const {
        SearchOptions parameters;
        parameters.mode = searchMode->currentData().toString().toStdString();
        parameters.match_case = matchCase->isChecked();
        parameters.whole_word = wholeWord->isChecked();
        parameters.dot_newline = dotNewline->isChecked();
        parameters.replace = replacing;
        return parameters;
    }
    void startSearch(bool replacing, bool all, bool reverse) {
        check(!activeSearch && !fileSearchRunning && !bufferSearchRunning, "A search is already running. Cancel it first.");
        auto* pane = activeEditor();
        check(!replacing || !pane->send(SCI_GETREADONLY), "Document is read-only.");
        const auto source = text(pane);
        const auto pattern = query->text().toUtf8();
        const auto replacementBytes = replacement->text().toUtf8();
        const auto request = search_request(rs(source), rs(pattern), rs(replacementBytes), searchOptions(replacing));
        auto object = QJsonDocument::fromJson(qs(request).toUtf8()).object();
        if (replacing && !all) {
            object["start"] = static_cast<qint64>(pane->send(SCI_GETSELECTIONSTART));
            object["end"] = static_cast<qint64>(pane->send(SCI_GETSELECTIONEND));
            object["replace_limit"] = 1;
            object["require_full_range"] = true;
        }
        const auto position = pane->send(reverse ? SCI_GETSELECTIONSTART : SCI_GETSELECTIONEND);
        const auto id = current().id;
        const auto revision = document_revision(*controller, id);
        const auto queryKey = query->text() + searchMode->currentData().toString() +
            QString::number(matchCase->isChecked()) + QString::number(dotNewline->isChecked());
        const bool skipZero = lastZeroDocument == id && lastZeroRevision == revision &&
            lastZeroPosition == position && lastZeroQuery == queryKey;
        if (!replacing) {
            auto navigationStart = position;
            const auto length = pane->send(SCI_GETLENGTH);
            if (skipZero) {
                if (!search->wrapAround->isChecked() && ((reverse && position == 0) || (!reverse && position == length))) {
                    reportSearch("Text not found. Wrap around is off.");
                    return;
                }
                navigationStart = reverse ? (position > 0 ? pane->send(SCI_POSITIONBEFORE, position) : length) :
                    (position < length ? pane->send(SCI_POSITIONAFTER, position) : 0);
            }
            object["first_only"] = true;
            object["reverse"] = reverse;
            object["wrap"] = search->wrapAround->isChecked();
            object["start"] = static_cast<qint64>(navigationStart);
            object["end"] = static_cast<qint64>(reverse ? 0 : length);
        }
        QPointer<ScintillaEditBase> target(pane);
        auto* task = new SearchTask(this);
        activeSearch = task;
        lastSearchError.clear();
        reportSearch("Searching in isolated worker...");
        task->start(QJsonDocument(object).toJson(QJsonDocument::Compact),
            [this, task, target, id, revision, source, position, reverse, replacing, all, skipZero, queryKey](QByteArray response) {
                activeSearch = nullptr;
                task->deleteLater();
                guarded([&] {
                    if (!target || document_revision(*controller, id) != revision) {
                        reportSearch("Document changed while searching. Results were discarded."); return;
                    }
                    const auto result = search_response(rs(response), rs(source));
                    if (replacing) {
                        check(result.replaced, "Worker returned a navigation result instead of a replacement.");
                        check(!target->send(SCI_GETREADONLY), "Document became read-only.");
                        if (result.count > 0) {
                            target->send(SCI_BEGINUNDOACTION);
                            target->send(SCI_SETTARGETRANGE, 0, target->send(SCI_GETLENGTH));
                            target->sends(SCI_REPLACETARGET, result.text.size(), result.text.data());
                            target->send(SCI_ENDUNDOACTION);
                            target->send(SCI_SETSEL, static_cast<uptr_t>(result.next_position), static_cast<sptr_t>(result.next_position));
                        }
                        reportSearch(QString("Replaced %1 occurrence(s).").arg(result.count));
                        if (!all && result.count == 0) find(false);
                        return;
                    }
                    if (result.hits.empty()) { reportSearch("Text not found."); return; }
                    const SearchHit* selected = reverse ? &result.hits.back() : &result.hits.front();
                    if (reverse) {
                        for (const auto& hit : result.hits) if (hit.end <= static_cast<std::uint64_t>(position) && hit.start < static_cast<std::uint64_t>(position)) selected = &hit;
                    } else {
                        for (const auto& hit : result.hits) if (hit.start >= static_cast<std::uint64_t>(position) &&
                            (!skipZero || hit.start > static_cast<std::uint64_t>(position))) { selected = &hit; break; }
                    }
                    lastZeroPosition = selected->start == selected->end ? static_cast<sptr_t>(selected->start) : -1;
                    lastZeroDocument = id; lastZeroRevision = revision; lastZeroQuery = queryKey;
                    target->send(SCI_SETSEL, static_cast<uptr_t>(selected->start), static_cast<sptr_t>(selected->end));
                    target->send(SCI_SCROLLCARET);
                    reportSearch(QString("Match on line %1.").arg(selected->line));
                });
            },
            [this, task](const QString& message) {
                activeSearch = nullptr; task->deleteLater();
                lastSearchError = message;
                reportSearch(message);
            });
    }
    void ensureResults() {
        if (!resultsDock) {
            resultsDock = new QDockWidget("Search Results", this);
            resultsDock->setObjectName("search-results");
            searchResults = new QTreeWidget;
            searchResults->setHeaderLabels({"File", "Line", "Match / warning"});
            resultsDock->setWidget(searchResults);
            addDockWidget(Qt::BottomDockWidgetArea, resultsDock);
            connect(searchResults, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
                guarded([&] {
                    if (item->data(0, Qt::UserRole + 10).isValid()) {
                        const auto id = item->data(0, Qt::UserRole + 10).toULongLong();
                        const auto revision = item->data(0, Qt::UserRole + 11).toULongLong();
                        const auto found = std::find_if(views.begin(), views.end(), [id](const auto& view) { return view.id == id; });
                        if (found == views.end() || document_revision(*controller, id) != revision) {
                            reportSearch("This tab was closed or changed after Find All. Search again before navigating.");
                            return;
                        }
                        tabs->setCurrentWidget(found->page);
                        found->focused->send(SCI_SETSEL, item->data(0, Qt::UserRole + 2).toULongLong(),
                            item->data(0, Qt::UserRole + 3).toLongLong());
                        found->focused->send(SCI_SCROLLCARET);
                        return;
                    }
                    const auto path = item->data(0, Qt::UserRole).toString();
                    if (path.isEmpty()) return;
                    const auto encoding = item->data(0, Qt::UserRole + 4).toString().toUtf8();
                    const auto currentFile = read_search_file(filePath(path), rs(encoding));
                    if (qs(currentFile.stamp) != item->data(0, Qt::UserRole + 1).toString()) {
                        statusBar()->showMessage("File changed after the search. Search again before navigating."); return;
                    }
                    openFile(path, encoding);
                    if (document_dirty(*controller, current().id)) {
                        statusBar()->showMessage("This tab has unsaved edits; disk-result navigation was not applied."); return;
                    }
                    check(text(activeEditor()) == qs(currentFile.text).toUtf8(),
                        "This open tab decodes the file differently. Reopen using the result's encoding before navigating.");
                    activeEditor()->send(SCI_SETSEL, item->data(0, Qt::UserRole + 2).toULongLong(),
                        item->data(0, Qt::UserRole + 3).toLongLong());
                    activeEditor()->send(SCI_SCROLLCARET);
                });
            });
        }
        resultsDock->show();
    }
    void findInFiles() {
        check(!activeSearch && !fileSearchRunning && !bufferSearchRunning, "A search is already running.");
        if (query->text().isEmpty()) {
            bool accepted = false;
            query->setText(QInputDialog::getText(this, "Find in Files", "Search text", QLineEdit::Normal, {}, &accepted));
            if (!accepted || query->text().isEmpty()) return;
        }
        const auto directory = QFileDialog::getExistingDirectory(this, "Search disk files (unsaved buffers are not included)");
        if (directory.isEmpty()) return;
        bool accepted = false;
        const auto filters = QInputDialog::getText(this, "File filters", "Filename patterns separated by spaces", QLineEdit::Normal, "*", &accepted);
        if (!accepted) return;
        QStringList codecLabels{"Auto (UTF-8 / BOM only)"};
        for (const auto& label : encoding_labels()) codecLabels.append(qs(label));
        const auto codec = QInputDialog::getItem(this, "Search file encoding",
            "Choose a known encoding for this file set; invalid files are reported, never guessed.", codecLabels, 0, false, &accepted);
        if (!accepted) return;
        const auto pattern = query->text().toUtf8();
        fileSearchRequest = qs(search_request(rust::Str(), rs(pattern), rust::Str(), searchOptions(false))).toUtf8();
        fileSearchRequest = qs(search_file_encoding(rs(fileSearchRequest),
            rs(codec == codecLabels.first() ? QByteArray() : codec.toUtf8()))).toUtf8();
        fileOriginalMode = searchMode->currentData().toString();
        replacementPreview = false;
        ensureResults();
        searchResults->clear();
        fileSearchCount = fileResultCount = 0;
        fileSearchRunning = true;
        auto* task = new SearchTask(this);
        activeSearch = task;
        const auto filterBytes = filters.toUtf8();
        const auto request = qs(scan_request(filePath(directory), rs(filterBytes))).toUtf8();
        task->start(request, [this, task](QByteArray response) {
            activeSearch = nullptr; task->deleteLater();
            try {
                const auto list = scan_response(rs(response));
                for (const auto& path : list.files) fileQueue.push_back(pathText(path));
                for (const auto& warning : list.warnings) new QTreeWidgetItem(searchResults, {"", "", qs(warning)});
                nextSearchFile();
            } catch (const std::exception& error) {
                fileSearchRunning = false;
                statusBar()->showMessage(QString::fromUtf8(error.what()));
            }
        }, [this, task](const QString& error) {
            activeSearch = nullptr; task->deleteLater(); fileSearchRunning = false;
            statusBar()->showMessage(error);
        }, "--list-files-worker");
    }
    void nextSearchFile() {
        if (!fileSearchRunning) return;
        if (fileQueue.isEmpty() || fileResultCount >= 10000) {
            fileSearchRunning = false;
            fileQueue.clear();
            if (replacementPreview) {
                replacementPreview = false;
                if (replacementPreviewFailed) {
                    diskProposals = rust::Vec<DiskProposal>();
                    statusBar()->showMessage("Replacement preview failed. No files were changed.");
                } else guarded([&] { commitDiskPreview(); });
                return;
            }
            statusBar()->showMessage(QString("Searched %1 disk files; %2 results%3.").arg(fileSearchCount).arg(fileResultCount)
                .arg(fileResultCount >= 10000 ? " (result limit reached)" : ""));
            return;
        }
        const auto path = fileQueue.takeFirst();
        const auto request = qs(search_file_input(rs(fileSearchRequest), filePath(path))).toUtf8();
        auto* task = new SearchTask(this);
        activeSearch = task;
        task->start(request, [this, task, path](QByteArray response) {
            activeSearch = nullptr; task->deleteLater(); ++fileSearchCount;
            try {
                const auto result = search_response(rs(response), rust::Str());
                if (replacementPreview && result.count > 0) {
                    check(result.replaced, "Worker did not produce a replacement proposal.");
                    proposalBytes += result.text.size();
                    if (proposalBytes > 64 * 1024 * 1024 || diskProposals.size() >= 128)
                        throw std::runtime_error("Replacement preview exceeds its 128-file/64 MiB budget. Narrow the result set.");
                    DiskProposal proposal;
                    proposal.path = filePath(path);
                    proposal.stamp = result.stamp;
                    proposal.encoding = result.encoding;
                    proposal.text = result.text;
                    diskProposals.push_back(std::move(proposal));
                    new QTreeWidgetItem(searchResults, {path, "", QString("Preview: replace %1 occurrence(s)").arg(result.count)});
                }
                for (const auto& hit : result.hits) {
                    if (fileResultCount++ >= 10000) break;
                    auto* item = new QTreeWidgetItem(searchResults, {path, QString::number(hit.line), qs(hit.preview)});
                    item->setData(0, Qt::UserRole, path);
                    item->setData(0, Qt::UserRole + 1, qs(result.stamp));
                    item->setData(0, Qt::UserRole + 2, static_cast<qulonglong>(hit.start));
                    item->setData(0, Qt::UserRole + 3, static_cast<qulonglong>(hit.end));
                    item->setData(0, Qt::UserRole + 4, qs(result.encoding));
                }
            } catch (const std::exception& error) {
                new QTreeWidgetItem(searchResults, {path, "", QString::fromUtf8(error.what())});
                if (replacementPreview) { replacementPreviewFailed = true; fileQueue.clear(); }
            }
            QTimer::singleShot(0, this, [this] { nextSearchFile(); });
        }, [this, task, path](const QString& error) {
            activeSearch = nullptr; task->deleteLater(); ++fileSearchCount;
            new QTreeWidgetItem(searchResults, {path, "", error});
            if (replacementPreview) { replacementPreviewFailed = true; fileQueue.clear(); }
            QTimer::singleShot(0, this, [this] { nextSearchFile(); });
        });
    }
    void previewDiskReplace() {
        check(!activeSearch && !fileSearchRunning, "A background operation is already running.");
        check(searchResults && !fileSearchRequest.isEmpty(), "Run Find in Files first.");
        QStringList targets;
        for (int i = 0; i < searchResults->topLevelItemCount(); ++i) {
            const auto path = searchResults->topLevelItem(i)->data(0, Qt::UserRole).toString();
            if (!path.isEmpty() && !targets.contains(path)) targets.push_back(path);
        }
        check(!targets.isEmpty() && targets.size() <= 128, "Choose a result set containing 1-128 files.");
        auto request = QJsonDocument::fromJson(fileSearchRequest).object();
        const auto mode = fileOriginalMode.toUtf8();
        const auto value = replacement->text().toUtf8();
        request["replacement"] = qs(normalize_replacement(rs(mode), rs(value)));
        request["replace"] = true;
        fileSearchRequest = QJsonDocument(request).toJson(QJsonDocument::Compact);
        fileQueue = targets;
        fileSearchCount = fileResultCount = 0;
        diskProposals = rust::Vec<DiskProposal>();
        proposalBytes = 0;
        replacementPreviewFailed = false;
        replacementPreview = fileSearchRunning = true;
        searchResults->clear();
        nextSearchFile();
    }
    void commitDiskPreview() {
        if (diskProposals.empty()) { statusBar()->showMessage("No replacement matches remain in these files."); return; }
        const auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/replace-backups";
        const auto directory = root + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QMessageBox confirm(this);
        confirm.setWindowTitle("Replace disk files");
        confirm.setTextFormat(Qt::PlainText);
        confirm.setText(QString("Apply the preview to %1 disk file(s)?\n\nOriginal bytes will be backed up to:\n%2\n\n"
            "Files are individually checked and replaced. A later failure can leave a partial batch; the report identifies each outcome.")
            .arg(diskProposals.size()).arg(directory));
        const auto preview = QJsonDocument::fromJson(fileSearchRequest).object();
        confirm.setDetailedText("Pattern from the last file search:\n" + preview["query"].toString() +
            "\n\nReplacement:\n" + preview["replacement"].toString());
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        confirm.setDefaultButton(QMessageBox::Cancel);
        if (confirm.exec() != QMessageBox::Yes) { diskProposals = rust::Vec<DiskProposal>(); return; }
        const auto encoded = qs(disk_request(*controller, std::move(diskProposals), filePath(directory))).toUtf8();
        executeDiskOperation(encoded, directory, false);
    }
    void executeDiskOperation(const QByteArray& encoded, const QString& directory, bool restoring) {
        auto* task = new SearchTask(this);
        activeSearch = task;
        setEnabled(false);
        statusBar()->showMessage("Applying checked disk writes; editing is temporarily paused. Backup: " + directory);
        task->start(encoded, [this, task, directory](QByteArray response) {
            activeSearch = nullptr; task->deleteLater(); setEnabled(true);
            guarded([&] {
                const auto outcomes = disk_response(rs(response));
                ensureResults(); searchResults->clear();
                for (const auto& outcome : outcomes) {
                    const auto path = pathText(outcome.path);
                    new QTreeWidgetItem(searchResults, {path, outcome.applied ? "OK" : "FAILED", qs(outcome.message)});
                    if (!outcome.applied) continue;
                    for (const auto id : disk_updated_documents(*controller, outcome.path)) {
                        for (auto& view : views) if (view.id == id) {
                            const auto opened = reload_document(*controller, id);
                            applyReload(view, opened);
                        }
                    }
                }
                refresh(); checkpointNow();
                statusBar()->showMessage("Disk operation finished. Review per-file outcomes. Backup: " + directory);
            });
        }, [this, task, directory](const QString& error) {
            activeSearch = nullptr; task->deleteLater(); setEnabled(true);
            if (testMode) { testFailure = error; lastSearchError = error; return; }
            QMessageBox message(this);
            message.setTextFormat(Qt::PlainText);
            message.setIcon(QMessageBox::Warning);
            message.setText(error + "\n\nDo not assume that every file was updated. Check the backup directory before retrying:\n" + directory);
            message.exec();
        }, restoring ? "--disk-restore-worker" : "--disk-apply-worker", 8 * 1024 * 1024);
    }
    void restoreDiskBackup() {
        check(!activeSearch && !fileSearchRunning, "A background operation is already running.");
        for (const auto& view : views)
            check(!document_dirty(*controller, view.id), "Save or close unsaved documents before restoring disk backups.");
        const auto directory = QFileDialog::getExistingDirectory(this, "Choose a replacement-backup directory");
        if (directory.isEmpty()) return;
        const auto encoded = qs(extension_inspection_request(filePath(directory))).toUtf8();
        auto* task = new SearchTask(this);
        activeSearch = task;
        task->start(encoded, [this, task, encoded, directory](QByteArray response) {
            activeSearch = nullptr; task->deleteLater();
            guarded([&] {
                const auto targets = scan_response(rs(response));
                QStringList names;
                for (const auto& path : targets.files) names << pathText(path);
                QMessageBox confirm(this);
                confirm.setTextFormat(Qt::PlainText);
                confirm.setWindowTitle("Restore original file bytes");
                confirm.setText("Restore the listed files only if they still match the replacement results?\n\n" + names.join('\n'));
                confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
                confirm.setDefaultButton(QMessageBox::Cancel);
                if (confirm.exec() == QMessageBox::Yes) {
                    for (const auto& view : views)
                        check(!document_dirty(*controller, view.id), "Documents changed during backup review. Save them before retrying.");
                    executeDiskOperation(encoded, directory, true);
                }
            });
        }, [this, task](const QString& error) {
            activeSearch = nullptr; task->deleteLater(); statusBar()->showMessage(error);
        }, "--disk-inspect-worker", 8 * 1024 * 1024);
    }
    void runLocalExtension() {
        check(!activeSearch && !fileSearchRunning, "Another background operation is running.");
        const auto directory = QFileDialog::getExistingDirectory(this, "Choose a local Wasm extension package");
        if (directory.isEmpty()) return;
        const auto id = current().id;
        const auto revision = document_revision(*controller, id);
        const auto encoded = qs(extension_inspection_request(filePath(directory))).toUtf8();
        auto* task = new SearchTask(this);
        activeSearch = task;
        task->start(encoded, [this, task, id, revision, directory](QByteArray metadata) {
            activeSearch = nullptr; task->deleteLater();
            guarded([&] {
                if (current().id != id || document_revision(*controller, id) != revision) {
                    statusBar()->showMessage("Document changed during extension review. Run the command again."); return;
                }
                const auto package = QJsonDocument::fromJson(metadata).object();
                const auto manifest = package["manifest"].toObject();
                QMessageBox permission(this);
                permission.setWindowTitle("Extension permission");
                permission.setTextFormat(Qt::PlainText);
                permission.setText("Run " + manifest["name"].toString() + "?\n\nPackage: " + directory +
                    "\nModule SHA-256: " + manifest["sha256"].toString() +
                    "\n\nCapability: read only the selected text and propose replacement text.\n"
                    "No guest filesystem, network, process or clipboard access.\n"
                    "The hash checks integrity; it is not a publisher signature.");
                permission.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
                permission.setDefaultButton(QMessageBox::Cancel);
                if (permission.exec() == QMessageBox::Yes) executeExtension(metadata);
            });
        }, [this, task](const QString& error) {
            activeSearch = nullptr; task->deleteLater();
            lastSearchError = error; statusBar()->showMessage(error);
        }, "--inspect-extension-worker", 2 * 1024 * 1024);
    }
    static QJsonObject nativePathJson(const QString& path) {
        return QJsonDocument::fromJson(qs(extension_inspection_request(filePath(path))).toUtf8()).object();
    }
    QJsonObject managementAction(const QString& operation) const {
        return QJsonObject{{"operation", operation}, {"root", nativePathJson(extensionRoot)}};
    }
    QJsonObject selectedExtension() const {
        check(extensionList && extensionList->currentItem(), "Select an installed package first.");
        const auto entry = extensionList->currentItem()->data(Qt::UserRole).toJsonObject();
        check(entry["status"] == "ready", "This entry is incomplete. Inspect its error and files manually; it cannot run or be automatically removed.");
        return entry["package"].toObject();
    }
    void updateExtensionButtons() {
        if (!extensionDock) return;
        const bool busy = managementTask;
        const auto* item = extensionList->currentItem();
        const auto entry = item ? item->data(Qt::UserRole).toJsonObject() : QJsonObject();
        const bool ready = entry["status"] == "ready";
        for (auto* button : extensionButtons) {
            const auto operation = button->objectName();
            bool enabled = !busy && extensionRootReady;
            if (operation == "extensions-refresh") enabled = !busy;
            else if (operation != "extensions-install") enabled = enabled && ready;
            if (operation == "extensions-run") enabled = enabled && entry["package"].toObject()["enabled"].toBool();
            button->setEnabled(enabled);
            if (operation == "extensions-toggle")
                button->setText(entry["package"].toObject()["enabled"].toBool() ? "Disable" : "Enable");
        }
    }
    void managementJob(const QJsonObject& action, const QString& expectedKind,
        std::function<void(QJsonObject)> complete) {
        check(!managementTask, "An extension management operation is already running.");
        const auto json = QJsonDocument(action).toJson(QJsonDocument::Compact);
        const auto input = qs(management_request(rs(json))).toUtf8();
        auto* task = new SearchTask(this);
        managementTask = task;
        extensionStatus->setText("Working... Package operations have a five-second deadline.");
        updateExtensionButtons();
        task->start(input, [this, task, expectedKind, complete = std::move(complete)](QByteArray response) {
            managementTask = nullptr; task->deleteLater();
            bool handled = false;
            guarded([&] {
                const auto result = QJsonDocument::fromJson(qs(management_response(rs(response))).toUtf8()).object();
                check(result["kind"] == expectedKind, "Unexpected extension management response.");
                complete(result);
                handled = true;
            });
            if (!handled) extensionStatus->setText("Operation failed. Refresh to inspect current state; incomplete packages require manual inspection.");
            updateExtensionButtons();
        }, [this, task](const QString& error) {
            managementTask = nullptr; task->deleteLater();
            extensionStatus->setText(error + "\nRefresh before retrying. Interrupted writes may leave incomplete packages.");
            qWarning("Extension management failed: %s", qPrintable(error));
            if (testMode) testFailure = error;
            updateExtensionButtons();
        }, "--manage-extensions-worker", 2 * 1024 * 1024);
    }
    void refreshExtensions(const QString& message = {}) {
        if (!extensionRootReady) {
            managementJob(managementAction("initialize"), "root", [this, message](const QJsonObject&) {
                extensionRootReady = true;
                refreshExtensions(message);
            });
            return;
        }
        QString selectedId;
        if (extensionList->currentItem())
            selectedId = extensionList->currentItem()->data(Qt::UserRole).toJsonObject()["package"].toObject()["id"].toString();
        managementJob(managementAction("list"), "packages", [this, selectedId, message](const QJsonObject& result) {
            extensionList->clear();
            for (const auto& value : result["entries"].toArray()) {
                const auto entry = value.toObject();
                const bool ready = entry["status"] == "ready";
                const auto package = entry["package"].toObject();
                const auto id = ready ? package["id"].toString() : entry["id"].toString();
                const auto label = ready ? package["manifest"].toObject()["name"].toString() +
                    (package["enabled"].toBool() ? " [enabled]" : " [disabled]") : "Incomplete package";
                auto* item = new QListWidgetItem(label + " - " + id.left(12), extensionList);
                item->setData(Qt::UserRole, QVariant::fromValue(entry));
                if (id == selectedId) extensionList->setCurrentItem(item);
            }
            if (!extensionList->currentItem() && extensionList->count()) extensionList->setCurrentRow(0);
            if (extensionList->count() == 0) extensionDetails->clear();
            extensionStatus->setText(message.isEmpty() ?
                QString("%1 version(s). New versions are disabled; enabling is not permission to execute.").arg(extensionList->count()) : message);
        });
    }
    bool confirmOperation(const QString& title, const QString& details) {
        QMessageBox dialog(this);
        dialog.setTextFormat(Qt::PlainText);
        dialog.setWindowTitle(title);
        dialog.setText(details);
        dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        dialog.setDefaultButton(QMessageBox::Cancel);
        return dialog.exec() == QMessageBox::Yes;
    }
    QJsonObject installationAction(const QJsonObject& review, const QString& previous = {}) const {
        const auto metadata = review["package"].toObject();
        auto action = managementAction(previous.isEmpty() ? "install" : "update");
        action["source"] = metadata["directory"];
        action["expected_manifest_sha256"] = metadata["manifest_sha256"];
        action["expected_snapshot_sha256"] = review["snapshot_sha256"];
        if (!previous.isEmpty()) action["previous"] = previous;
        return action;
    }
    void installExtension(bool update) {
        const auto previous = update ? selectedExtension()["id"].toString() : QString();
        const auto source = QFileDialog::getExistingDirectory(this, "Choose a local extension package folder");
        if (source.isEmpty()) return;
        managementJob(QJsonObject{{"operation", "inspect"}, {"source", nativePathJson(source)}}, "review",
            [this, source, previous](const QJsonObject& result) {
                const auto review = result["review"].toObject();
                const auto metadata = review["package"].toObject();
                const auto manifest = metadata["manifest"].toObject();
                QString description = source + "\n\n" + QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) +
                    "\nManifest SHA-256: " + metadata["manifest_sha256"].toString() +
                    "\nSnapshot SHA-256: " + review["snapshot_sha256"].toString() + "\n\nFiles (including notices):";
                for (const auto& value : review["files"].toArray()) {
                    const auto file = value.toObject();
                    description += "\n" + file["name"].toString() + " (" + QString::number(file["size"].toInteger()) +
                        " bytes), SHA-256 " + file["sha256"].toString();
                }
                description += "\n\nHashes bind these exact bytes, not a publisher identity. Installation never executes code or grants permission."
                    "\nNew versions are disabled. Existing versions are retained unchanged.";
                if (!previous.isEmpty()) description += "\nPrevious manifest SHA-256: " + previous;
                if (!confirmOperation(previous.isEmpty() ? "Install Local Extension" : "Install Updated Version", description)) {
                    extensionStatus->setText("Installation cancelled. No package was copied.");
                    return;
                }
                managementJob(installationAction(review, previous), "installation", [this](const QJsonObject& installed) {
                    const auto installation = installed["result"].toObject();
                    refreshExtensions(installation["already_installed"].toBool() ?
                        "Identical version already installed; its enabled state was retained." :
                        "Version installed disabled. Enable it explicitly, then approve each selected-text execution.");
                });
            });
    }
    void toggleExtension() {
        const auto package = selectedExtension();
        const bool enabled = !package["enabled"].toBool();
        if (!confirmOperation(enabled ? "Enable Extension" : "Disable Extension",
            package["manifest"].toObject()["name"].toString() + "\n" + package["id"].toString() +
            (enabled ? "\n\nAllow this version to be selected for execution? Each run still requires separate permission." :
                "\n\nDisable future managed executions of this version?"))) return;
        auto action = managementAction("set_enabled");
        action["id"] = package["id"]; action["enabled"] = enabled;
        managementJob(action, "enabled", [this](const QJsonObject&) { refreshExtensions("Enabled state updated. No guest code was executed."); });
    }
    void uninstallExtension() {
        const auto package = selectedExtension();
        if (!confirmOperation("Uninstall Extension Version",
            package["manifest"].toObject()["name"].toString() + "\n" + package["id"].toString() +
            "\n\nRemove only this validated installed version? Other versions, source packages and documents are retained.")) return;
        auto action = managementAction("uninstall"); action["id"] = package["id"];
        managementJob(action, "uninstalled", [this](const QJsonObject&) { refreshExtensions("Selected version uninstalled; other files were retained."); });
    }
    void runInstalledExtension() {
        check(!managementTask && !activeSearch && !fileSearchRunning, "Wait for the current background operation first.");
        const auto package = selectedExtension();
        check(package["enabled"].toBool(), "Enable this version explicitly before running it.");
        auto* pane = activeEditor();
        const auto id = current().id;
        const auto revision = document_revision(*controller, id);
        const auto start = pane->send(SCI_GETSELECTIONSTART);
        const auto end = pane->send(SCI_GETSELECTIONEND);
        check(end > start && end - start <= 256 * 1024, "Select between 1 byte and 256 KiB of text.");
        if (!confirmOperation("Allow Selected-Text Extension",
            package["manifest"].toObject()["name"].toString() + "\nManifest SHA-256: " + package["id"].toString() +
            QString("\n\nRead the selected %1 byte(s) and propose replacement text?").arg(end - start) +
            "\nNo guest filesystem, network, process or clipboard access. This is not publisher verification.")) return;
        check(current().id == id && activeEditor() == pane && document_revision(*controller, id) == revision &&
            pane->send(SCI_GETSELECTIONSTART) == start && pane->send(SCI_GETSELECTIONEND) == end,
            "Document or selection changed during confirmation. No extension was run.");
        executeExtension({}, package["id"].toString());
    }
    void ensureExtensionManager() {
        if (extensionDock) return;
        const auto parent = QFileInfo(settingsPath).dir();
        check(QDir().mkpath(parent.absolutePath()), "Cannot create the private application data directory.");
        const auto canonicalParent = parent.canonicalPath();
        check(!canonicalParent.isEmpty(), "Cannot resolve the application data directory.");
        extensionRoot = QDir(canonicalParent).filePath("managed-extensions");
        extensionDock = new QDockWidget("Local Extensions", this);
        extensionDock->setObjectName("extension-manager");
        auto* panel = new QWidget(extensionDock);
        auto* layout = new QVBoxLayout(panel);
        auto* location = new QLabel(extensionRoot, panel);
        location->setTextFormat(Qt::PlainText); location->setWordWrap(true);
        layout->addWidget(location);
        extensionList = new QListWidget(panel);
        extensionList->setObjectName("installed-extensions");
        layout->addWidget(extensionList);
        extensionDetails = new QPlainTextEdit(panel);
        extensionDetails->setReadOnly(true); extensionDetails->setMaximumHeight(180);
        layout->addWidget(extensionDetails);
        auto* buttons = new QHBoxLayout();
        const std::vector<std::tuple<QString, QString, std::function<void()>>> actions{
            {"extensions-refresh", "Refresh", [this] { refreshExtensions(); }},
            {"extensions-install", "Install...", [this] { installExtension(false); }},
            {"extensions-update", "Update...", [this] { installExtension(true); }},
            {"extensions-toggle", "Enable", [this] { toggleExtension(); }},
            {"extensions-run", "Run...", [this] { runInstalledExtension(); }},
            {"extensions-uninstall", "Uninstall", [this] { uninstallExtension(); }}};
        for (const auto& action : actions) {
            auto* button = new QPushButton(std::get<1>(action), panel);
            button->setObjectName(std::get<0>(action));
            connect(button, &QPushButton::clicked, this, [this, callback = std::get<2>(action)] { guarded(callback); });
            extensionButtons.push_back(button);
            buttons->addWidget(button);
        }
        layout->addLayout(buttons);
        extensionStatus = new QLabel(panel);
        extensionStatus->setTextFormat(Qt::PlainText); extensionStatus->setWordWrap(true);
        layout->addWidget(extensionStatus);
        extensionDock->setWidget(panel);
        addDockWidget(Qt::BottomDockWidgetArea, extensionDock);
        connect(extensionList, &QListWidget::currentRowChanged, this, [this](int) {
            const auto* item = extensionList->currentItem();
            extensionDetails->setPlainText(item ? QString::fromUtf8(
                QJsonDocument(item->data(Qt::UserRole).toJsonObject()).toJson(QJsonDocument::Indented)) : QString());
            updateExtensionButtons();
        });
        connect(extensionDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
            commands.at("manage_extensions")->setChecked(visible);
        });
        extensionDock->show();
        refreshExtensions();
    }
    void executeExtension(const QByteArray& metadata, const QString& managedId = {}) {
        check(!activeSearch && !fileSearchRunning, "Another background editing operation is already running.");
        check(!macroRecording && !macroPlaybackActive, "Stop macro recording/playback before running an extension.");
        auto* editor = activeEditor();
        check(!editor->send(SCI_GETREADONLY), "Document is read-only.");
        const auto start = editor->send(SCI_GETSELECTIONSTART);
        const auto end = editor->send(SCI_GETSELECTIONEND);
        check(end > start && end - start <= 256 * 1024, "Select between 1 byte and 256 KiB of text.");
        const auto source = text(editor);
        const auto selected = source.mid(start, end - start);
        const auto input = qs(managedId.isEmpty() ? extension_request(rs(metadata), rs(selected)) :
            managed_extension_request(filePath(extensionRoot), rs(managedId.toUtf8()), rs(selected))).toUtf8();
        const auto id = current().id;
        const auto revision = document_revision(*controller, id);
        QPointer<ScintillaEditBase> target(editor);
        auto* task = new SearchTask(this);
        activeSearch = task;
        lastSearchError.clear();
        task->start(input, [this, task, target, id, revision, start, end](QByteArray response) {
            activeSearch = nullptr; task->deleteLater();
            guarded([&] {
                if (!target || document_revision(*controller, id) != revision ||
                    target->send(SCI_GETSELECTIONSTART) != start || target->send(SCI_GETSELECTIONEND) != end) {
                    statusBar()->showMessage("Document or selection changed; extension proposal was discarded."); return;
                }
                check(!target->send(SCI_GETREADONLY), "Document became read-only.");
                const auto replacementText = extension_response(rs(response));
                check(target->send(SCI_GETLENGTH) - (end - start) + replacementText.size() <= 32 * 1024 * 1024,
                    "Extension proposal exceeds the document limit.");
                target->send(SCI_BEGINUNDOACTION);
                target->send(SCI_SETTARGETRANGE, start, end);
                target->sends(SCI_REPLACETARGET, replacementText.size(), replacementText.data());
                target->send(SCI_ENDUNDOACTION);
                target->send(SCI_SETSEL, start, start + static_cast<sptr_t>(replacementText.size()));
                statusBar()->showMessage("Extension proposal applied. Undo restores the previous text.");
            });
        }, [this, task](const QString& error) {
            activeSearch = nullptr; task->deleteLater(); lastSearchError = error;
            statusBar()->showMessage(error);
        }, "--extension-worker", 2 * 1024 * 1024);
    }
    void configure(ScintillaEditBase* editor) {
        editor->send(SCI_SETCODEPAGE, SC_CP_UTF8);
        editor->send(SCI_SETTABWIDTH, options["editor"].toObject().value("tab_width").toInt(4));
        editor->send(SCI_SETUSETABS, options.value("use_tabs").toBool());
        editor->send(SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
        editor->send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
        editor->send(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
        editor->send(SCI_SETMARGINMASKN, 1, 1);
        editor->send(SCI_SETMARGINSENSITIVEN, 1, true);
        editor->send(SCI_MARKERDEFINE, 0, SC_MARK_BOOKMARK);
        editor->send(SCI_MARKERSETBACK, 0, 0xd08030);
        editor->send(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
        editor->send(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
        editor->send(SCI_SETMARGINSENSITIVEN, 2, true);
        const std::pair<int, int> foldMarkers[] = {
            {SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS},
            {SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS},
            {SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE},
            {SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER},
            {SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED},
            {SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED},
            {SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER}
        };
        for (const auto& marker : foldMarkers) {
            editor->send(SCI_MARKERDEFINE, marker.first, marker.second);
            editor->send(SCI_MARKERSETFORE, marker.first, 0xffffff);
            editor->send(SCI_MARKERSETBACK, marker.first, 0x888888);
        }
        editor->send(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CHANGE | SC_AUTOMATICFOLD_CLICK);
        editor->send(SCI_SETMULTIPLESELECTION, true);
        editor->send(SCI_SETADDITIONALSELECTIONTYPING, true);
        editor->send(SCI_SETVIRTUALSPACEOPTIONS, SCVS_RECTANGULARSELECTION);
        editor->send(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
        editor->send(SCI_SETSCROLLWIDTHTRACKING, true);
        editor->send(SCI_SETWRAPMODE, wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
        theme(editor);
        applyEditorPreferences(editor);
    }
        static const std::map<QString, unsigned int>& macroCommands() {
            static const std::map<QString, unsigned int> values{
                {"left", SCI_CHARLEFT}, {"right", SCI_CHARRIGHT}, {"up", SCI_LINEUP}, {"down", SCI_LINEDOWN},
                {"home", SCI_HOME}, {"end", SCI_LINEEND}, {"document_start", SCI_DOCUMENTSTART}, {"document_end", SCI_DOCUMENTEND},
                {"backspace", SCI_DELETEBACK}, {"delete", SCI_CLEAR}, {"tab", SCI_TAB}, {"back_tab", SCI_BACKTAB},
                {"select_all", SCI_SELECTALL}, {"duplicate_line", SCI_LINEDUPLICATE}, {"delete_line", SCI_LINEDELETE},
                {"duplicate_selection", SCI_SELECTIONDUPLICATE},
                {"word_left", SCI_WORDLEFT}, {"word_right", SCI_WORDRIGHT}, {"extend_left", SCI_CHARLEFTEXTEND},
                {"extend_right", SCI_CHARRIGHTEXTEND}, {"extend_up", SCI_LINEUPEXTEND}, {"extend_down", SCI_LINEDOWNEXTEND}};
            return values;
        }
        void stopMacro() {
            macroRecording = false;
            macroPendingIndent = false;
            for (const auto& view : views) { view.primary->send(SCI_STOPRECORD); view.clone->send(SCI_STOPRECORD); }
        }
        static void autoIndent(ScintillaEditBase* pane) {
            const auto line = pane->send(SCI_LINEFROMPOSITION, pane->send(SCI_GETCURRENTPOS));
            if (line > 0) {
                pane->send(SCI_SETLINEINDENTATION, line, pane->send(SCI_GETLINEINDENTATION, line - 1));
                pane->send(SCI_GOTOPOS, pane->send(SCI_GETLINEINDENTPOSITION, line));
            }
        }
        void recordMacro(Scintilla::Message message, Scintilla::uptr_t, Scintilla::sptr_t parameter) {
            if (!macroRecording) return;
            if (current().id != macroDocument) { stopMacro(); return; }
            if (activeEditor()->send(SCI_GETSELECTIONS) != 1 || activeEditor()->send(SCI_SELECTIONISRECTANGLE)) {
                stopMacro();
                throw std::runtime_error("Recording stopped: basic macros require one stream selection.");
            }
            const auto id = static_cast<unsigned int>(message);
            if (id == SCI_REPLACESEL) {
                const auto inserted = QString::fromUtf8(reinterpret_cast<const char*>(parameter));
                if (!macroSteps.isEmpty() && macroSteps.last().toObject()["kind"] == "Insert") {
                    auto previous = macroSteps.last().toObject();
                    previous["text"] = previous["text"].toString() + inserted;
                    macroSteps[macroSteps.size() - 1] = previous;
                } else macroSteps.append(QJsonObject{{"kind", "Insert"}, {"text", inserted}});
                if (macroPendingIndent) {
                    macroSteps.append(QJsonObject{{"kind", "Command"}, {"command", "auto_indent"}});
                    macroPendingIndent = false;
                }
            } else {
                bool supported = false;
                for (const auto& entry : macroCommands()) if (entry.second == id) {
                    macroSteps.append(QJsonObject{{"kind", "Command"}, {"command", entry.first}});
                    supported = true; break;
                }
                if (!supported) {
                    stopMacro();
                    statusBar()->showMessage("Recording stopped: that operation is outside the basic-edit macro format.");
                    return;
                }
            }
            if (macroSteps.size() > 4096 || QJsonDocument(macroSteps).toJson(QJsonDocument::Compact).size() > 1024 * 1024) {
                stopMacro();
                throw std::runtime_error("Macro recording exceeded its size limit.");
            }
        }
        QByteArray macroJson() const {
            return QJsonDocument(QJsonObject{{"schema", 1}, {"name", macroName}, {"steps", macroSteps}})
                .toJson(QJsonDocument::Compact);
        }
        static QList<ImportedMacro> importMacros(const QByteArray& xml) {
            std::map<unsigned int, QString> allowed;
            for (const auto& entry : macroCommands()) allowed.emplace(entry.second, entry.first);
            return importNotepadMacros(xml, allowed);
        }
        void importMacroDialog() {
            const auto path = QFileDialog::getOpenFileName(this, "Import Notepad++ Basic Macro", {}, "Notepad++ shortcuts (*.xml)");
            if (path.isEmpty()) return;
            QFile file(path);
            check(file.open(QIODevice::ReadOnly), "Cannot open shortcut XML.");
            const auto contents = file.read(2 * 1024 * 1024 + 1);
            check(file.error() == QFileDevice::NoError, "Cannot read shortcut XML.");
            const auto imported = importMacros(contents);
            QStringList choices;
            for (const auto& item : imported)
                choices.append(QString("%1. %2%3").arg(choices.size() + 1).arg(item.name,
                    item.error.isEmpty() ? "" : " [unsupported]"));
            bool accepted = false;
            const auto selected = QInputDialog::getItem(this, "Import Basic Macro",
                "Choose a macro. Import never executes it or imports its shortcut.", choices, 0, false, &accepted);
            if (!accepted) return;
            const auto& item = imported.at(choices.indexOf(selected));
            check(item.error.isEmpty(), item.error.toUtf8().constData());
            const auto encoded = QJsonDocument(QJsonObject{{"schema", 1}, {"name", item.name}, {"steps", item.steps}})
                .toJson(QJsonDocument::Compact);
            validate_macro(rs(encoded));
            stopMacro(); macroName = item.name; macroSteps = item.steps;
            statusBar()->showMessage("Macro imported without executing it. Use Play or Save Macro explicitly.", 10000);
        }
        void macroStep(ScintillaEditBase* pane, const QJsonObject& step, qint64& editBudget) {
            const auto command = step["command"].toString();
            const auto inserted = step["text"].toString().toUtf8();
            const auto length = pane->send(SCI_GETLENGTH);
            qint64 growth = 0;
            if (step["kind"] == "Insert") {
                growth = inserted.size();
            } else if (command == "duplicate_selection" && pane->send(SCI_GETSELECTIONSTART) != pane->send(SCI_GETSELECTIONEND)) {
                growth = pane->send(SCI_GETSELECTIONEND) - pane->send(SCI_GETSELECTIONSTART);
            } else if (command == "duplicate_line" || command == "duplicate_selection") {
                const auto line = pane->send(SCI_LINEFROMPOSITION, pane->send(SCI_GETCURRENTPOS));
                growth = pane->send(SCI_LINELENGTH, line) + 2;
            } else if (command == "auto_indent" || command == "tab" || command == "back_tab") {
                const auto first = pane->send(SCI_LINEFROMPOSITION, pane->send(SCI_GETSELECTIONSTART));
                const auto last = pane->send(SCI_LINEFROMPOSITION, pane->send(SCI_GETSELECTIONEND));
                check(last - first < 10000, "Macro indentation exceeds 10,000 lines. Undo restores prior steps.");
                const auto width = std::max(pane->send(SCI_GETINDENT), pane->send(SCI_GETTABWIDTH));
                for (auto line = first; line <= last; ++line) {
                    const auto oldBytes = pane->send(SCI_GETLINEINDENTPOSITION, line) - pane->send(SCI_POSITIONFROMLINE, line);
                    const auto columns = pane->send(SCI_GETLINEINDENTATION,
                        command == "auto_indent" && line > 0 ? line - 1 : line);
                    growth += std::max<sptr_t>(width, columns + width - oldBytes);
                }
            }
            const auto removed = step["kind"] == "Insert" ?
                pane->send(SCI_GETSELECTIONEND) - pane->send(SCI_GETSELECTIONSTART) : 0;
            check(length - removed + growth <= 32 * 1024 * 1024 && growth <= editBudget,
                "Macro reached its document/edit budget. Undo restores edits already applied.");
            editBudget -= growth;
            const auto message = step["kind"] == "Insert" || command == "auto_indent" ? 0 : macroCommands().at(command);
            const QSignalBlocker primarySignals(current().primary);
            const QSignalBlocker cloneSignals(current().clone);
            pane->send(SCI_SETSTATUS, SC_STATUS_OK);
            if (step["kind"] == "Insert") pane->sends(SCI_REPLACESEL, 0, inserted.constData());
            else if (command == "auto_indent") autoIndent(pane);
            else pane->send(message);
        }
        bool playMacro(std::uint32_t repeats = 1) {
            check(!macroPlaybackActive && !activeSearch && !fileSearchRunning && !managementTask,
                "Finish or cancel the current background operation before playing a macro.");
            check(composingEditors.isEmpty(), "Finish input-method composition before playing a macro.");
            stopMacro();
            const auto encoded = macroJson();
            const auto total = macro_repeat_steps(rs(encoded), repeats);
            const auto tape = macroSteps;
            QPointer<ScintillaEditBase> pane(activeEditor());
            const auto document = current().id;
            auto expectedRevision = document_revision(*controller, document);
            auto lastUndo = pane->send(SCI_GETUNDOCURRENT);
            check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
            check(pane->send(SCI_GETSELECTIONS) == 1 && !pane->send(SCI_SELECTIONISRECTANGLE),
                "Basic macro playback requires one stream selection.");
            pane->send(SCI_AUTOCCANCEL); pane->send(SCI_CALLTIPCANCEL);
            const QScopedValueRollback<bool> playing(macroPlaybackActive, true);
            struct UndoScope {
                QPointer<ScintillaEditBase> editor;
                explicit UndoScope(ScintillaEditBase* value) : editor(value) { editor->send(SCI_BEGINUNDOACTION); }
                ~UndoScope() { if (editor) editor->send(SCI_ENDUNDOACTION); }
            } undo(pane);
            QDialog progress(this);
            progress.setWindowTitle("Macro Playback");
            progress.setWindowModality(Qt::ApplicationModal);
            progress.setObjectName("macro-playback");
            auto* layout = new QVBoxLayout(&progress);
            auto* label = new QLabel("Playback is bounded and undoable. Cancel stops after the current command.", &progress);
            label->setWordWrap(true);
            layout->addWidget(label);
            auto* bar = new QProgressBar(&progress);
            bar->setRange(0, static_cast<int>(total));
            layout->addWidget(bar);
            auto* cancel = new QPushButton("Cancel", &progress);
            cancel->setObjectName("cancel-macro-playback");
            layout->addWidget(cancel);
            connect(cancel, &QPushButton::clicked, &progress, &QDialog::reject);
            macroPlaybackDialog = &progress;
            const auto clearDialog = qScopeGuard([this] { macroPlaybackDialog = nullptr; });
            QElapsedTimer elapsed; elapsed.start();
            qint64 editBudget = 64 * 1024 * 1024;
            std::uint32_t completed = 0;
            bool finished = false;
            QString failure;
            QTimer tick(&progress);
            tick.setInterval(0);
            auto synchronize = [&] {
                if (pane && pane->send(SCI_GETUNDOCURRENT) != lastUndo) {
                    document_changed(*controller, document, pane->send(SCI_GETMODIFY) != 0);
                    expectedRevision = document_revision(*controller, document);
                    lastUndo = pane->send(SCI_GETUNDOCURRENT);
                    recoveryPending = true;
                }
            };
            auto advance = [&] {
                try {
                    QElapsedTimer slice; slice.start();
                    for (int batch = 0; batch < 64 && completed < total && slice.elapsed() < 8; ++batch) {
                        check(elapsed.elapsed() < 5000, "Macro exceeded five seconds. Undo restores edits already applied.");
                        check(pane, "Macro target was closed.");
                        const auto revision = document_revision(*controller, document);
                        if (current().id != document || revision != expectedRevision || pane->send(SCI_GETREADONLY))
                            throw std::runtime_error(QString("Macro target changed at step %1: document %2/%3, revision %4/%5, read-only %6, undo %7/%8, bytes %9. Undo restores prior edits.")
                                .arg(completed).arg(current().id).arg(document).arg(revision).arg(expectedRevision)
                                .arg(pane->send(SCI_GETREADONLY)).arg(pane->send(SCI_GETUNDOCURRENT)).arg(lastUndo)
                                .arg(pane->send(SCI_GETLENGTH)).toStdString());
                        macroStep(pane, tape[static_cast<qsizetype>(completed % tape.size())].toObject(), editBudget);
                        synchronize();
                        check(pane->send(SCI_GETSTATUS) == SC_STATUS_OK, "Macro command failed. Undo restores prior edits.");
                        check(elapsed.elapsed() < 5000, "Macro exceeded five seconds. Undo restores edits already applied.");
                        ++completed;
                    }
                    bar->setValue(static_cast<int>(completed));
                    if (completed == total) { finished = true; tick.stop(); progress.accept(); }
                } catch (const std::exception& error) {
                    failure = QString::fromUtf8(error.what());
                    tick.stop(); progress.reject();
                }
            };
            connect(&tick, &QTimer::timeout, &progress, advance);
            advance();
            if (!finished && failure.isEmpty()) { tick.start(); progress.exec(); }
            tick.stop();
            synchronize();
            refresh(); refreshWatches();
            if (!failure.isEmpty()) throw std::runtime_error(failure.toStdString());
            statusBar()->showMessage(finished ? QString("Macro completed: %1 step(s).").arg(completed) :
                QString("Macro cancelled after %1 step(s). Undo restores the partial playback.").arg(completed), 10000);
            return finished;
        }
        void styledExport(const QString& operation) {
            auto* pane = activeEditor();
            auto start = pane->send(SCI_GETSELECTIONSTART);
            auto end = pane->send(SCI_GETSELECTIONEND);
            if (start == end) { start = 0; end = pane->send(SCI_GETLENGTH); }
            check(end - start <= 2 * 1024 * 1024, "Styled export is limited to 2 MiB of input.");
            pane->send(SCI_COLOURISE, start, end);
            QByteArray styled((end - start) * 2 + 2, '\0');
            Sci_TextRangeFull range{{start, end}, styled.data()};
            pane->send(SCI_GETSTYLEDTEXTFULL, 0, reinterpret_cast<sptr_t>(&range));
            QList<StyledRun> runs;
            for (sptr_t position = 0; position < end - start;) {
                const auto style = static_cast<unsigned char>(styled[position * 2 + 1]);
                QByteArray characters;
                do {
                    characters.push_back(styled[position * 2]); ++position;
                } while (position < end - start && static_cast<unsigned char>(styled[position * 2 + 1]) == style);
                QStringDecoder decoder(QStringDecoder::Utf8);
                const QString content = decoder.decode(characters);
                check(!decoder.hasError(), "Lexer styling split a UTF-8 character; export was not produced.");
                const auto foreground = pane->send(SCI_STYLEGETFORE, style);
                const auto background = pane->send(SCI_STYLEGETBACK, style);
                check(runs.size() < 100000, "Styled export exceeds its style-run limit.");
                runs.push_back({content, QColor(static_cast<int>(foreground & 255), static_cast<int>((foreground >> 8) & 255), static_cast<int>((foreground >> 16) & 255)),
                    QColor(static_cast<int>(background & 255), static_cast<int>((background >> 8) & 255), static_cast<int>((background >> 16) & 255)),
                    pane->send(SCI_STYLEGETBOLD, style) != 0, pane->send(SCI_STYLEGETITALIC, style) != 0,
                    pane->send(SCI_STYLEGETUNDERLINE, style) != 0});
            }
            const int fontSize = options.value("font_size").toInt(11);
            const int tabWidth = options["editor"].toObject().value("tab_width").toInt(4);
            if (operation == "export_html") {
                const auto path = QFileDialog::getSaveFileName(this, "Export Styled HTML", {}, "HTML (*.html)");
                if (path.isEmpty()) return;
                check(disk_updated_documents(*controller, filePath(path)).empty(), "Export cannot overwrite an open document.");
                const auto output = exportHtml(runs, fontSize, tabWidth).toUtf8();
                QSaveFile file(path);
                check(file.open(QIODevice::WriteOnly) && file.write(output) == output.size() && file.commit(), "Cannot write HTML export.");
            } else {
                auto mime = std::make_unique<QMimeData>();
                QString plain;
                for (const auto& run : runs) plain += run.text;
                mime->setText(plain);
                if (operation == "copy_html") mime->setHtml(exportHtml(runs, fontSize, tabWidth));
                else {
                    const auto rtf = exportRtf(runs, fontSize, tabWidth);
                    mime->setData("text/rtf", rtf);
#ifdef Q_OS_WIN
                    mime->setData("application/x-qt-windows-mime;value=\"Rich Text Format\"", rtf);
#endif
                }
                QApplication::clipboard()->setMimeData(mime.release());
            }
            statusBar()->showMessage("Styled text exported.");
        }
        void printDocument(bool preview) {
            const auto contents = text(activeEditor());
            check(contents.size() <= 8 * 1024 * 1024, "Print preview is limited to 8 MiB in this release.");
            QPrinter printer(QPrinter::HighResolution);
            QTextDocument document;
            document.setPlainText(QString::fromUtf8(contents));
            document.setDefaultFont(QFont(options.value("font_family").toString("Monospace"), options.value("font_size").toInt(11)));
            if (preview) {
                QPrintPreviewDialog dialog(&printer, this);
                connect(&dialog, &QPrintPreviewDialog::paintRequested, &dialog, [&](QPrinter* target) { document.print(target); });
                dialog.exec();
            } else {
                QPrintDialog dialog(&printer, this);
                if (dialog.exec() == QDialog::Accepted) document.print(&printer);
            }
        }
        void utility(const QString& id) {
            auto* pane = activeEditor();
            const auto contents = text(pane);
            auto start = pane->send(SCI_GETSELECTIONSTART);
            auto end = pane->send(SCI_GETSELECTIONEND);
            if (start == end) { start = 0; end = contents.size(); }
            const auto selected = contents.mid(start, end - start);
            QByteArray output;
            if (id == "hash") {
                bool accepted = false;
                const auto algorithm = QInputDialog::getItem(this, "Generate Hash", "MD5/SHA-1 are legacy checksum tools, not secure signatures.",
                    {"SHA-256", "SHA-512", "SHA-1", "MD5"}, 0, false, &accepted);
                if (!accepted) return;
                const auto hash = algorithm == "SHA-256" ? QCryptographicHash::Sha256 : algorithm == "SHA-512" ?
                    QCryptographicHash::Sha512 : algorithm == "SHA-1" ? QCryptographicHash::Sha1 : QCryptographicHash::Md5;
                const auto digest = QCryptographicHash::hash(selected, hash);
                check(!digest.isEmpty(), "The selected hash algorithm is unavailable.");
                QApplication::clipboard()->setText(QString::fromLatin1(digest.toHex()));
                statusBar()->showMessage(algorithm + " copied to clipboard."); return;
            }
            check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
            if (id == "base64_encode") output = selected.toBase64();
            else if (id == "base64_decode") {
                const auto decoded = QByteArray::fromBase64Encoding(selected, QByteArray::AbortOnBase64DecodingErrors);
                check(static_cast<bool>(decoded), "Invalid Base64 input.");
                output = decoded.decoded;
            } else if (id == "url_encode") output = QUrl::toPercentEncoding(QString::fromUtf8(selected));
            else if (id == "url_decode") {
                for (qsizetype index = 0; index < selected.size(); ++index) if (selected[index] == '%') {
                    check(index + 2 < selected.size() && std::isxdigit(static_cast<unsigned char>(selected[index + 1])) &&
                        std::isxdigit(static_cast<unsigned char>(selected[index + 2])), "Invalid percent escape.");
                    index += 2;
                }
                output = QByteArray::fromPercentEncoding(selected);
            } else if (id == "json_format" || id == "json_minify") {
                output = qs(id == "json_format" ? format_json(rs(selected)) : minify_json(rs(selected))).toUtf8();
            }
            QStringDecoder decoder(QStringDecoder::Utf8);
            const QString decodedText = decoder.decode(output);
            Q_UNUSED(decodedText);
            check(!decoder.hasError() && !output.contains('\0'), "Decoded data is not supported UTF-8 text; it was not inserted.");
            check(contents.size() - selected.size() + output.size() <= 32 * 1024 * 1024, "Output exceeds the document limit.");
            pane->send(SCI_BEGINUNDOACTION);
            pane->send(SCI_SETTARGETRANGE, start, end);
            pane->sends(SCI_REPLACETARGET, output.size(), output.constData());
            pane->send(SCI_ENDUNDOACTION);
            pane->send(SCI_SETSEL, start, start + output.size());
        }
        void runProgram() {
            check(!runningProgram, "A program is already running.");
            const auto program = QFileDialog::getOpenFileName(this, "Choose a program to run");
            if (program.isEmpty()) return;
            bool accepted = false;
            const auto arguments = QInputDialog::getText(this, "Program arguments", "Arguments (quoted strings supported; no shell expansion)",
                QLineEdit::Normal, {}, &accepted);
            if (!accepted || QMessageBox::question(this, "Run program", program + "\n" + arguments) != QMessageBox::Yes) return;
            auto* outputDock = new QDockWidget("Program Output", this);
            auto* outputView = new QPlainTextEdit;
            outputView->setReadOnly(true);
            outputView->setMaximumBlockCount(2000);
            outputDock->setWidget(outputView);
            addDockWidget(Qt::BottomDockWidgetArea, outputDock);
            outputDock->show();
            auto* process = new QProcess(this);
            runningProgram = process;
            process->setProcessChannelMode(QProcess::MergedChannels);
            connect(process, &QProcess::readyReadStandardOutput, outputView, [process, outputView] {
                const auto bytes = process->readAllStandardOutput();
                outputView->appendPlainText(QString::fromUtf8(bytes.left(65536)));
                if (bytes.size() > 65536) outputView->appendPlainText("[output truncated]");
            });
            connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
                statusBar()->showMessage("Program failed: " + process->errorString());
                if (error == QProcess::FailedToStart) { runningProgram = nullptr; process->deleteLater(); }
            });
            connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process](int code, QProcess::ExitStatus) {
                statusBar()->showMessage(QString("Program finished with exit code %1").arg(code));
                runningProgram = nullptr; process->deleteLater();
            });
            const auto currentPath = pathText(document_path(*controller, current().id));
            if (!currentPath.isEmpty()) process->setWorkingDirectory(QFileInfo(currentPath).absolutePath());
            process->start(program, QProcess::splitCommand(arguments));
        }
    void theme(ScintillaEditBase* editor) {
        auto foreground = dark ? 0xe8e0dc : 0x2b2520;
        auto background = dark ? 0x28211e : 0xffffff;
        if (catalog) if (const auto defaults = catalog->defaultStyle(dark ? languages::Theme::Dark : languages::Theme::Light)) {
            if (defaults->foreground && (defaults->colorFlags & 1)) foreground = sciColor(*defaults->foreground);
            if (defaults->background && (defaults->colorFlags & 2)) background = sciColor(*defaults->background);
        }
        editor->send(SCI_STYLESETFORE, STYLE_DEFAULT, foreground);
        editor->send(SCI_STYLESETBACK, STYLE_DEFAULT, background);
        const auto family = [this] {
            const auto configured = options.value("font_family").toString();
            if (!configured.isEmpty()) return configured;
            const auto available = QFontDatabase::families();
            for (const auto& candidate : {QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("Menlo")})
                if (available.contains(candidate)) return candidate;
            return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        }().toUtf8();
        editor->sends(SCI_STYLESETFONT, STYLE_DEFAULT, family.constData());
        editor->send(SCI_STYLESETSIZE, STYLE_DEFAULT, options.value("font_size").toInt(11));
        editor->send(SCI_STYLESETCHECKMONOSPACED, STYLE_DEFAULT, true);
        editor->send(SCI_STYLECLEARALL);
        editor->send(SCI_STYLESETFORE, STYLE_LINENUMBER, dark ? 0xa4968f : 0x877c75);
        editor->send(SCI_STYLESETBACK, STYLE_LINENUMBER, dark ? 0x302824 : 0xf8f5f3);
        const auto gutter = dark ? 0x302824 : 0xf8f5f3;
        editor->send(SCI_SETFOLDMARGINCOLOUR, true, gutter);
        editor->send(SCI_SETFOLDMARGINHICOLOUR, true, gutter);
        updateGutter(editor);
        editor->send(SCI_SETCARETFORE, foreground);
        editor->send(SCI_SETSELBACK, true, dark ? 0x634a3e : 0xfadec8);
    }
    void addDocument(bool preview) {
        Opened opened;
        opened.id = create_document(*controller, preview);
        opened.text = preview ? welcome_text() : rust::String();
        opened.encoding = "UTF-8";
        opened.dirty = false;
        opened.existing = false;
        addView(opened, preview ? QByteArray("rust") : QByteArray("null"));
    }
    void applyLexer(DocumentView& view, const QByteArray& name) {
        const auto identifier = QString::fromUtf8(name);
        const auto* language = catalog->language(identifier == "null" ? "normal" : identifier);
        if (!language) for (const auto& candidate : catalog->languages())
            if (candidate.engine == name) { language = &candidate; break; }
        languages::Language raw;
        if (!language) {
            const auto engine = name.startsWith("raw:") ? name.mid(4) : name;
            check(engine != "user" && languages::LanguageCatalog::supportsEngine(engine), "No supported language profile for this lexer.");
            raw.id = "raw:" + QString::fromUtf8(engine);
            raw.engine = engine;
            language = &raw;
        }
        configureLanguage(view, *language, catalog->styles(language->id, dark ? languages::Theme::Dark : languages::Theme::Light));
        view.languageId = language->id;
        view.udlXml.clear();
        if (tabs->count() > 0 && current().id == view.id) queueFunctions();
    }
    const languages::CompletionData* completionData(const DocumentView& view, bool report) {
        if (!completionCache.contains(view.languageId)) {
            QString error;
            completionCache.insert(view.languageId, catalog->completions(view.languageId, &error));
            completionErrors.insert(view.languageId, error);
        }
        const auto& entry = completionCache[view.languageId];
        if (!entry) {
            if (report) statusBar()->showMessage(completionErrors.value(view.languageId));
            return nullptr;
        }
        return &*entry;
    }
    void completeWord(ScintillaEditBase* pane, bool manual) {
        const auto position = pane->send(SCI_GETCURRENTPOS);
        const auto start = pane->send(SCI_WORDSTARTPOSITION, position, true);
        const auto length = position - start;
        if (length > 128) return;
        const auto* pointer = reinterpret_cast<const char*>(pane->send(SCI_GETRANGEPOINTER, start, length));
        const auto prefix = QString::fromUtf8(pointer, length);
        if (!manual && (prefix.size() < options.value("completion_min_chars").toInt(2) ||
            (options.value("completion_ignore_numbers").toBool() && !prefix.isEmpty() && prefix.front().isDigit()))) return;
        const auto mode = options.value("completion_mode").toInt(1);
        const auto* api = completionData(current(), false);
        const auto casing = api && api->ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
        QStringList matches;
        QSet<QString> seen;
        auto addCandidate = [&](const QString& candidate) {
            const auto key = casing == Qt::CaseInsensitive ? candidate.toCaseFolded() : candidate;
            if (candidate.startsWith(prefix, casing) && candidate != prefix && !seen.contains(key)) {
                seen.insert(key);
                matches.push_back(candidate);
            }
        };
        if (api && (manual || mode == 1 || mode == 3)) for (const auto& entry : api->entries) {
            addCandidate(entry.name);
            if (matches.size() >= 200) break;
        }
        if ((manual || mode == 2 || mode == 3) && matches.size() < 200) {
            const auto documentLength = pane->send(SCI_GETLENGTH);
            auto begin = std::max<sptr_t>(0, position - 128 * 1024);
            auto end = std::min<sptr_t>(documentLength, begin + 256 * 1024);
            while (begin < end && (pane->send(SCI_GETCHARAT, begin) & 0xc0) == 0x80) ++begin;
            while (end > begin && end < documentLength && (pane->send(SCI_GETCHARAT, end) & 0xc0) == 0x80) --end;
            const auto* content = reinterpret_cast<const char*>(pane->send(SCI_GETRANGEPOINTER, begin, end - begin));
            const auto document = QString::fromUtf8(content, end - begin);
            const QRegularExpression words("[\\p{L}_][\\p{L}\\p{N}_]*");
            auto iterator = words.globalMatch(document);
            while (iterator.hasNext() && matches.size() < 200) {
                const auto match = iterator.next();
                if ((begin > 0 && match.capturedStart() == 0) ||
                    (end < documentLength && match.capturedEnd() == document.size())) continue;
                addCandidate(match.captured());
            }
        }
        matches.sort(casing);
        if (matches.isEmpty()) { if (manual) statusBar()->showMessage("No completion candidates for this prefix."); return; }
        const auto list = matches.join('\n').toUtf8();
        pane->send(SCI_AUTOCSETSEPARATOR, '\n');
        pane->send(SCI_AUTOCSETIGNORECASE, casing == Qt::CaseInsensitive);
        pane->sends(SCI_AUTOCSHOW, length, list.constData());
    }
    void showCalltip(ScintillaEditBase* pane, bool manual) {
        const auto* api = completionData(current(), manual);
        if (!api) return;
        const auto position = pane->send(SCI_GETCURRENTPOS);
        const auto contents = text(pane);
        const auto open = contents.lastIndexOf(api->startFunction.toLatin1(), position - 1);
        if (open < 0) { if (manual) statusBar()->showMessage("Place the caret after a function's opening delimiter."); return; }
        auto end = static_cast<sptr_t>(open);
        while (end > 0 && std::isspace(static_cast<unsigned char>(contents[end - 1]))) --end;
        const auto start = pane->send(SCI_WORDSTARTPOSITION, end, true);
        const auto name = QString::fromUtf8(contents.mid(start, end - start));
        for (const auto& entry : api->entries) {
            if (entry.name.compare(name, api->ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive) != 0) continue;
            QStringList signatures;
            for (const auto& overload : entry.overloads) {
                signatures << overload.signature;
                if (signatures.size() >= 4) break;
            }
            if (!signatures.isEmpty()) {
                const auto tip = signatures.join('\n').left(4096).toUtf8();
                pane->sends(SCI_CALLTIPSHOW, start, tip.constData());
                return;
            }
        }
        if (manual) statusBar()->showMessage("No bundled calltip for this function.");
    }
    static int sciColor(const QColor& color) {
        return color.red() | (color.green() << 8) | (color.blue() << 16);
    }
    void configureLanguage(DocumentView& view, const languages::Language& language, const QList<languages::Style>& styles) {
        auto* lexer = CreateLexer(language.engine.constData());
        check(lexer != nullptr, "Selected lexer is unavailable.");
        view.primary->send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lexer));
        view.lexer = language.engine;
        view.primary->sends(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), "1");
        for (const auto& property : language.properties)
            view.primary->sends(SCI_SETPROPERTY, reinterpret_cast<uptr_t>(property.name.constData()), property.value.constData());
        for (const auto& keywords : language.keywords)
            view.primary->sends(SCI_SETKEYWORDS, keywords.index, keywords.text.constData());
        for (auto* pane : {view.primary, view.clone}) {
            theme(pane);
            for (const auto& style : styles) {
                if (style.foreground && (style.colorFlags & 1)) pane->send(SCI_STYLESETFORE, style.id, sciColor(*style.foreground));
                if (style.background && (style.colorFlags & 2)) pane->send(SCI_STYLESETBACK, style.id, sciColor(*style.background));
                if (style.fontFlags >= 0) {
                    pane->send(SCI_STYLESETBOLD, style.id, (style.fontFlags & 1) != 0);
                    pane->send(SCI_STYLESETITALIC, style.id, (style.fontFlags & 2) != 0);
                    pane->send(SCI_STYLESETUNDERLINE, style.id, (style.fontFlags & 4) != 0);
                }
            }
        }
        view.primary->send(SCI_COLOURISE, 0, -1);
    }
    void applyUdl(DocumentView& view, const QByteArray& xml) {
        const bool fresh = view.udlXml != xml || view.udlProfile == 0;
        if (fresh) {
            check(udlIdentities < 128, "UDL cache lifetime limit reached. Restart before importing more profiles.");
        }
        const int profileId = fresh ? (udlIdentities + 1) * 2 : view.udlProfile;
        const int documentId = fresh ? (udlIdentities + 1) * 2 + 1 : view.udlDocument;
        QString error;
        const auto profile = languages::LanguageCatalog::parseUdlXml(xml, profileId, documentId, &error);
        if (!profile) throw std::runtime_error(error.toStdString());
        if (fresh) ++udlIdentities;
        view.udlProfile = profileId;
        view.udlDocument = documentId;
        configureLanguage(view, profile->language, profile->styles);
        view.udlXml = xml;
        view.languageId = "udl:" + profile->language.displayName;
        recoveryPending = true;
        if (tabs->count() > 0 && current().id == view.id) queueFunctions();
    }
    void restyle(DocumentView& view) {
        if (!view.udlXml.isEmpty()) applyUdl(view, view.udlXml);
        else applyLexer(view, view.languageId.toUtf8());
    }
    QByteArray detectedLexer(const QString& path) {
        const auto* language = catalog->detectFileName(path);
        return language ? language->id.toUtf8() : QByteArray("normal");
    }
    void historyWarning(const QString& message) {
        qWarning("Recent history: %s", qPrintable(message));
        if (testMode) { testFailure = message; return; }
        if (historyWarningShown) return;
        historyWarningShown = true;
        auto* warning = new QMessageBox(this);
        warning->setAttribute(Qt::WA_DeleteOnClose);
        warning->setTextFormat(Qt::PlainText);
        warning->setWindowTitle("Recent History Not Saved");
        warning->setText("The file operation succeeded, but its history could not be saved.\n\n" + message);
        warning->open();
    }
    void rememberFile(const FilePath& path, const rust::String& encoding, const FilePath& previous = {}) {
        try {
            remember_recent(*controller, path, rs(qs(encoding).toUtf8()), previous);
            options = QJsonDocument::fromJson(qs(current_settings(*controller)).toUtf8()).object();
            historyWarningShown = false;
        } catch (const std::exception& error) { historyWarning(QString::fromUtf8(error.what())); }
    }
    void populateRecentMenu() {
        recentMenu->clear();
        const auto recent = recent_files(*controller);
        options = QJsonDocument::fromJson(qs(current_settings(*controller)).toUtf8()).object();
        if (recent.empty()) {
            auto* empty = recentMenu->addAction("No recent files");
            empty->setEnabled(false);
            return;
        }
        int index = 0;
        for (const auto& entry : recent) {
            const auto label = pathText(entry.path).replace("&", "&&");
            auto* action = recentMenu->addAction(QString("%1 %2").arg(++index).arg(label));
            connect(action, &QAction::triggered, this, [this, entry] {
                guarded([&] {
                    check(!macroRecording && !macroPlaybackActive, "Finish macro recording/playback before opening a recent file.");
                    validate_command(*controller, "open_recent");
                    openNativeFile(entry.path, qs(entry.encoding).toUtf8());
                });
            });
        }
    }
    void openAllRecent() {
        const auto recent = recent_files(*controller);
        options = QJsonDocument::fromJson(qs(current_settings(*controller)).toUtf8()).object();
        QStringList errors;
        int opened = 0;
        for (const auto& entry : recent) {
            try { openNativeFile(entry.path, qs(entry.encoding).toUtf8()); ++opened; }
            catch (const std::exception& error) { errors.append(pathText(entry.path) + ": " + QString::fromUtf8(error.what())); }
        }
        if (!errors.isEmpty()) {
            QMessageBox message(this);
            message.setTextFormat(Qt::PlainText);
            message.setWindowTitle("Recent File Results");
            message.setText(QString("Opened %1 of %2 recent files.\n\n").arg(opened).arg(recent.size()) + errors.join('\n'));
            message.exec();
        }
    }
    void openFile(const QString& path, const QByteArray& encoding = {}) {
        openNativeFile(filePath(path), encoding);
    }
    void openNativeFile(const FilePath& path, const QByteArray& encoding = {}, bool allowLarge = true) {
        check(!macroPlaybackActive, "Finish or cancel macro playback before opening another file.");
        if (needs_large_preview(*controller, path)) {
            check(allowLarge, "Open large-file previews in an independent window.");
            auto* viewer = new LargeFileWindow(pathText(path), this);
            viewer->show();
            rememberFile(path, rust::String());
            return;
        }
        const auto opened = open_document(*controller, path, rs(encoding));
        if (opened.existing) {
            for (const auto& view : views) if (view.id == opened.id) {
                tabs->setCurrentWidget(view.page);
                rememberFile(path, document_saved_encoding(*controller, view.id));
                return;
            }
            throw std::runtime_error("An open document has no view.");
        }
        addView(opened, detectedLexer(pathText(path)));
        rememberFile(path, document_saved_encoding(*controller, opened.id));
    }
    void addView(const Opened& opened, const QByteArray& lexerName) {
        const auto id = opened.id;
        auto* page = new QSplitter(Qt::Horizontal);
        auto* editor = new WorkspaceEditor(page);
        auto* clone = new WorkspaceEditor(page);
        editor->setObjectName(QString("editor-%1").arg(id));
        clone->setObjectName(QString("clone-%1").arg(id));
        configure(editor);
        configure(clone);
        editor->sends(SCI_ADDTEXT, opened.text.size(), opened.text.data());
        editor->send(SCI_EMPTYUNDOBUFFER);
        if (!opened.dirty) editor->send(SCI_SETSAVEPOINT);
        const auto contents = qs(opened.text);
        if (contents.contains("\r\n")) editor->send(SCI_SETEOLMODE, SC_EOL_CRLF);
        else if (contents.contains('\n')) editor->send(SCI_SETEOLMODE, SC_EOL_LF);
        else if (contents.contains('\r')) editor->send(SCI_SETEOLMODE, SC_EOL_CR);
        clone->send(SCI_SETDOCPOINTER, 0, editor->send(SCI_GETDOCPOINTER));
        clone->hide();
        views.push_back({id, editor, clone, page, editor, lexerName, {}, {}, 0, 0, false, false, false, {}});
        applyLexer(views.back(), lexerName);
        connect(editor, &ScintillaEditBase::modified, this, [this, id, editor](Scintilla::ModificationFlags flags) {
            // notifyChange also fires for fold/marker metadata, not just text edits.
            if (!tearingDown && comparisonEnabled && !synchronizingComparison &&
                (static_cast<unsigned int>(flags) & SC_MOD_CHANGEFOLD)) {
                comparisonCacheValid = false;
                queueToolsRefresh();
            }
            if ((static_cast<unsigned int>(flags) & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) == 0) return;
            if (!tearingDown) guarded([&] {
                document_changed(*controller, id, editor->send(SCI_GETMODIFY) != 0);
                if (comparisonEnabled && (id == comparisonLeft || id == comparisonTarget)) comparisonCacheValid = false;
                for (const auto& view : views) if (view.id == id) {
                    updateGutter(view.primary);
                    updateGutter(view.clone);
                }
                recoveryPending = true;
                refresh();
                refreshWatches();
            });
            queueInlineImages(id);
        });
        connect(editor, &ScintillaEditBase::savePointChanged, this, [this, id](bool dirty) {
            if (!tearingDown) guarded([&] {
                document_savepoint_changed(*controller, id, dirty);
                refresh();
            });
        });
        connect(editor, &ScintillaEditBase::updateUi, this, [this](Scintilla::Update) {
            if (!tearingDown && tabs->count() > 0) guarded([&] { updateStatus(); });
        });
        for (auto* pane : {editor, clone}) {
            pane->installEventFilter(this);
            pane->images.commit = [this, id, pane](sptr_t line, InlineImageLine image) {
                guarded([&] { for (auto& view : views) if (view.id == id) resizeInlineImage(view, pane, line, image); });
            };
            connect(pane, &QObject::destroyed, this, [this, pane] { composingEditors.remove(pane); });
            connect(pane, &ScintillaEditBase::zoom, this, [this, pane, id](int zoom) {
                updateGutter(pane);
                queueInlineImages(id);
                if (comparisonEnabled && comparisonAligned && !synchronizingComparison &&
                    (id == comparisonLeft || id == comparisonTarget)) {
                    const QScopedValueRollback<bool> syncing(synchronizingComparison, true);
                    for (auto& view : views) if (view.id == comparisonLeft || view.id == comparisonTarget)
                        for (auto* other : {view.primary, view.clone}) if (other != pane) other->send(SCI_SETZOOM, zoom);
                    comparisonCacheValid = false;
                    queueToolsRefresh();
                }
                if (!synchronizeZoom || changingZoom) return;
                const QScopedValueRollback<bool> updatingZoom(changingZoom, true);
                for (const auto& view : views) if (view.id == id) {
                    auto* other = pane == view.primary ? view.clone : view.primary;
                    other->send(SCI_SETZOOM, zoom);
                }
            });
            connect(pane, &ScintillaEditBase::verticalScrolled, this, [this, pane, id](int) {
                if (!tearingDown && comparisonEnabled) {
                    auto* left = viewInGroup(0);
                    auto* right = viewInGroup(1);
                    if (left && left->id == id && left->primary == pane) syncComparisonScroll(false);
                    if (right && right->id == id && right->primary == pane) syncComparisonScroll(true);
                }
                if (!synchronizeVertical || scrolling) return;
                const QScopedValueRollback<bool> guard(scrolling, true);
                for (const auto& view : views) if (view.id == id) {
                    auto* other = pane == view.primary ? view.clone : view.primary;
                    other->send(SCI_SETFIRSTVISIBLELINE, pane->send(SCI_GETFIRSTVISIBLELINE));
                }
            });
            connect(pane, &ScintillaEditBase::horizontalScrolled, this, [this, pane, id](int) {
                if (!synchronizeHorizontal || scrolling) return;
                const QScopedValueRollback<bool> guard(scrolling, true);
                for (const auto& view : views) if (view.id == id) {
                    auto* other = pane == view.primary ? view.clone : view.primary;
                    other->send(SCI_SETXOFFSET, pane->send(SCI_GETXOFFSET));
                }
            });
            connect(pane, &ScintillaEditBase::macroRecord, this, [this](Scintilla::Message message, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam) {
                guarded([&] { recordMacro(message, wParam, lParam); });
            });
            connect(pane, &ScintillaEditBase::charAdded, this, [this, pane](int ch) {
                if (options.value("auto_indent").toBool(true) &&
                    (ch == '\n' || (ch == '\r' && pane->send(SCI_GETEOLMODE) == SC_EOL_CR))) {
                    if (macroRecording) pane->send(SCI_STOPRECORD);
                    autoIndent(pane);
                    if (macroRecording) {
                        macroPendingIndent = true;
                        pane->send(SCI_STARTRECORD);
                    }
                }
                if (!macroRecording && !macroPlaybackActive && !tearingDown && !composingEditors.contains(pane)) {
                    guarded([&] {
                        if (ch == '(' && options.value("calltips").toBool(true)) showCalltip(pane, false);
                        else if (ch == ')') pane->send(SCI_CALLTIPCANCEL);
                        else if (options.value("completion").toBool(true) && ch >= 0 &&
                            (QChar::isLetterOrNumber(static_cast<char32_t>(ch)) || ch == '_'))
                            completeWord(pane, false);
                    });
                }
            });
            connect(pane, &ScintillaEditBase::focusChanged, this, [this, pane, id](bool focused) {
                if (focused && !tearingDown) for (auto& view : views) if (view.id == id) {
                    view.focused = pane;
                    tabs->activatePage(view.page);
                }
            });
            connect(pane, &ScintillaEditBase::marginClicked, this, [this, pane](Scintilla::Position position, Scintilla::KeyMod, int margin) {
                if (margin != 1) return;
                const auto line = pane->send(SCI_LINEFROMPOSITION, position);
                if (pane->send(SCI_MARKERGET, line) & 1) pane->send(SCI_MARKERDELETE, line, 0);
                else pane->send(SCI_MARKERADD, line, 0);
                recoveryPending = true;
            });
        }
        const int index = tabs->addTab(page, qs(document_title(*controller, id)));
        tabs->setTabData(index, QVariant::fromValue<qulonglong>(id));
        tabs->setCurrentIndex(index);
        normalizePinnedOrder();
        editor->setFocus();
        recoveryPending = true;
        for (auto& view : views) if (view.id == id) refreshInlineImages(view);
        refresh();
    }
    void refresh() {
        const QSignalBlocker blocker(documentList);
        documentList->clear();
        for (int i = 0; i < tabs->count(); ++i) {
            const auto it = std::find_if(views.begin(), views.end(), [&](const auto& view) { return view.page == tabs->widget(i); });
            if (it == views.end()) continue;
            const auto tabStyle = document_tab(*controller, it->id);
            const auto title = (tabStyle.pinned ? QString("[pin] ") : QString()) +
                qs(document_title(*controller, it->id)) + (it->diskChanged ? " [disk changed]" : "");
            tabs->setTabText(i, title);
            const QColor colors[] = {palette().color(QPalette::WindowText), QColor("#b44141"), QColor("#347a45"),
                QColor("#3678b3"), QColor("#8963b5"), QColor("#b17a1d")};
            tabs->setTabTextColor(i, colors[tabStyle.color]);
            documentList->addItem(title);
        }
        if (tabs->count() > 0) {
            if (lastActiveDocument != current().id) {
                previousDocument = lastActiveDocument;
                lastActiveDocument = current().id;
            }
            documentList->setCurrentRow(tabs->currentIndex());
            setWindowTitle(qs(document_title(*controller, current().id)) + " - Notepad Star");
            commands.at("clone_view")->setChecked(current().clone->isVisible());
            commands.at("pin_tab")->setChecked(document_tab(*controller, current().id).pinned);
            updateStatus();
            updateMap();
            queueFunctions();
        }
        commands.at("compare_tabs")->setEnabled(views.size() >= 2);
        commands.at("compare_right")->setEnabled(views.size() >= 2);
        commands.at("stop_comparison")->setEnabled(comparisonEnabled);
        commands.at("next_difference")->setEnabled(comparisonEnabled);
        commands.at("previous_difference")->setEnabled(comparisonEnabled);
        commands.at("toggle_comparison")->setEnabled(views.size() >= 2 || comparisonEnabled);
        commands.at("swap_comparison")->setEnabled(tabs->groupCount(0) && tabs->groupCount(1));
        for (const auto* command : {"move_other_view", "move_all_other_view", "move_left_view", "move_right_view"})
            commands.at(command)->setEnabled(views.size() >= 2);
        commands.at("move_left_view")->setEnabled(views.size() >= 2 && tabs->activeGroup() == 1);
        commands.at("move_right_view")->setEnabled(views.size() >= 2 && tabs->activeGroup() == 0);
        syncToolStates();
        queueToolsRefresh();
    }
    void updateStatus() {
        auto* editor = activeEditor();
        const auto position = editor->send(SCI_GETCURRENTPOS);
        const auto line = editor->send(SCI_LINEFROMPOSITION, position) + 1;
        const auto column = editor->send(SCI_GETCOLUMN, position) + 1;
        const auto ending = editor->send(SCI_GETEOLMODE);
        const auto path = pathText(document_path(*controller, current().id));
        statusBar()->showMessage(QString("%1 | %2 lines   Ln %3, Col %4   %5   %6   %7")
            .arg(QString::fromUtf8(current().lexer)).arg(editor->send(SCI_GETLINECOUNT)).arg(line).arg(column)
            .arg(qs(document_encoding(*controller, current().id)))
            .arg(ending == SC_EOL_CRLF ? "CRLF" : ending == SC_EOL_LF ? "LF" : "CR")
            .arg(path.isEmpty() ? "Untitled" : path));
        commands.at("read_only")->setChecked(editor->send(SCI_GETREADONLY) != 0);
        commands.at("monitoring")->setChecked(current().monitoring);
    }
    rust::Vec<Buffer> snapshots() {
        rust::Vec<Buffer> buffers;
        for (int index = 0; index < tabs->count(); ++index) {
            for (const auto& view : views) if (view.page == tabs->widget(index)) {
                Buffer buffer;
                buffer.id = view.id;
                const auto bytes = text(view.primary);
                buffer.text = rust::String(bytes.constData(), static_cast<std::size_t>(bytes.size()));
                buffer.active = index == tabs->currentIndex();
                QJsonObject state{
                    {"caret", static_cast<qint64>(view.primary->send(SCI_GETCURRENTPOS))},
                    {"anchor", static_cast<qint64>(view.primary->send(SCI_GETANCHOR))},
                    {"first_visible", static_cast<qint64>(view.primary->send(SCI_GETFIRSTVISIBLELINE))},
                    {"x_offset", static_cast<qint64>(view.primary->send(SCI_GETXOFFSET))},
                    {"zoom", view.primary->property("comparison-zoom").isValid() ?
                        view.primary->property("comparison-zoom").toInt() : static_cast<int>(view.primary->send(SCI_GETZOOM))},
                    {"clone_visible", !view.clone->isHidden()},
                    {"clone_caret", static_cast<qint64>(view.clone->send(SCI_GETCURRENTPOS))},
                    {"clone_anchor", static_cast<qint64>(view.clone->send(SCI_GETANCHOR))},
                    {"clone_first_visible", static_cast<qint64>(view.clone->send(SCI_GETFIRSTVISIBLELINE))},
                    {"lexer", QString::fromUtf8(view.lexer)},
                    {"eol", static_cast<int>(view.primary->send(SCI_GETEOLMODE))},
                    {"read_only", view.primary->send(SCI_GETREADONLY) != 0}
                };
                state["language_id"] = view.languageId;
                const int group = tabs->groupOf(view.page);
                state["view_group"] = group;
                state["group_active"] = tabs->currentInGroup(group) == view.page;
                if (view.primary->property("comparison-wrap").isValid())
                    state["first_document_line"] = static_cast<qint64>(view.primary->send(SCI_DOCLINEFROMVISIBLE, view.primary->send(SCI_GETFIRSTVISIBLELINE)));
                if (view.clone->property("comparison-wrap").isValid())
                    state["clone_first_document_line"] = static_cast<qint64>(view.clone->send(SCI_DOCLINEFROMVISIBLE, view.clone->send(SCI_GETFIRSTVISIBLELINE)));
                if (!view.functionKey.isEmpty()) state["function_key"] = view.functionKey;
                if (!view.udlXml.isEmpty()) state["udl_xml_base64"] = QString::fromLatin1(view.udlXml.toBase64());
                QJsonArray bookmarks;
                for (auto line = view.primary->send(SCI_MARKERNEXT, 0, 1); line >= 0;
                    line = view.primary->send(SCI_MARKERNEXT, line + 1, 1)) {
                    check(bookmarks.size() < 100000, "Session bookmarks exceed their limit.");
                    bookmarks.append(static_cast<qint64>(line));
                }
                state["bookmarks"] = bookmarks;
                const auto encoded = QJsonDocument(state).toJson(QJsonDocument::Compact);
                buffer.view = rust::String(encoded.constData(), static_cast<std::size_t>(encoded.size()));
                buffers.push_back(std::move(buffer));
            }
        }
        return buffers;
    }
    void checkpointNow() {
        if (!recoveryReady) return;
        checkpoint(*controller, snapshots(), dark, wrap);
        recoveryPending = false;
    }
    void restoreViews(rust::Vec<Opened> documents) {
        std::array<QWidget*, 2> groupSelections{};
        std::vector<std::pair<ScintillaEditBase*, qint64>> sourceScroll;
        for (const auto& doc : documents) {
            const auto state = QJsonDocument::fromJson(qs(doc.view).toUtf8()).object();
            const auto lexer = state.value("language_id").toString(state.value("lexer").toString()).toUtf8();
            const auto udl = state.value("udl_xml_base64").toString().toLatin1();
            addView(doc, !udl.isEmpty() ? QByteArray("normal") :
                lexer.isEmpty() ? detectedLexer(pathText(document_path(*controller, doc.id))) : lexer);
            const auto group = state.value("view_group").toInt(0);
            auto* page = current().page;
            tabs->movePage(page, group);
            if (state.value("group_active").toBool() && !groupSelections[group]) groupSelections[group] = page;
            if (!udl.isEmpty()) {
                const auto xml = QByteArray::fromBase64Encoding(udl, QByteArray::AbortOnBase64DecodingErrors);
                check(static_cast<bool>(xml), "Stored UDL is not valid Base64.");
                applyUdl(current(), xml.decoded);
            }
            if (!state.isEmpty()) {
                auto& view = current();
                view.functionKey = state.value("function_key").toString();
                view.primary->send(SCI_SETSEL, state["anchor"].toInteger(), state["caret"].toInteger());
                view.primary->send(SCI_SETZOOM, state["zoom"].toInt());
                view.primary->send(SCI_SETXOFFSET, state["x_offset"].toInteger());
                view.primary->send(SCI_SETFIRSTVISIBLELINE, state["first_visible"].toInteger());
                if (state.contains("first_document_line")) sourceScroll.emplace_back(view.primary, state["first_document_line"].toInteger());
                view.primary->send(SCI_SETEOLMODE, state["eol"].toInt());
                view.primary->send(SCI_SETREADONLY, state["read_only"].toBool());
                view.clone->send(SCI_SETSEL, state["clone_anchor"].toInteger(), state["clone_caret"].toInteger());
                view.clone->send(SCI_SETFIRSTVISIBLELINE, state["clone_first_visible"].toInteger());
                if (state.contains("clone_first_document_line")) sourceScroll.emplace_back(view.clone, state["clone_first_document_line"].toInteger());
                view.clone->setVisible(state["clone_visible"].toBool());
                view.primary->send(SCI_MARKERDELETEALL, 0);
                for (const auto& line : state["bookmarks"].toArray())
                    view.primary->send(SCI_MARKERADD, line.toInteger(), 0);
            }
        }
        for (int group = 0; group < 2; ++group)
            if (groupSelections[group]) tabs->selectInGroup(group, groupSelections[group]);
        const auto imported = QJsonDocument::fromJson(qs(imported_layout(*controller)).toUtf8()).object();
        if (!imported.isEmpty()) {
            options["editor"] = imported["editor"];
            applyOptions();
            if (!documents.empty()) {
                const auto selected = documents[static_cast<std::size_t>(std::clamp(imported["active"].toInt(), 0, static_cast<int>(documents.size()) - 1))].id;
                for (const auto& view : views) if (view.id == selected) tabs->setCurrentWidget(view.page);
            }
        }
        for (const auto& entry : sourceScroll)
            entry.first->send(SCI_SETFIRSTVISIBLELINE, entry.first->send(SCI_VISIBLEFROMDOCLINE, entry.second));
        if (!documents.empty()) { refresh(); checkpointNow(); }
    }
    QString chooseEncoding() {
        bool accepted = false;
        QStringList labels;
        for (const auto& label : encoding_labels()) labels.append(qs(label));
        const auto selected = QInputDialog::getItem(this, "Text encoding", "Encoding",
            labels, 0, false, &accepted);
        return accepted ? selected : QString();
    }
    bool saveView(DocumentView& view, bool saveAs, bool copy = false) {
        QString destination;
        rust::String prepared;
        const auto oldPath = pathText(document_path(*controller, view.id));
        if (saveAs || copy || oldPath.isEmpty()) {
            destination = QFileDialog::getSaveFileName(this, copy ? "Save a Copy" : "Save As", oldPath,
                "All files (*)", nullptr, QFileDialog::DontConfirmOverwrite);
            if (destination.isEmpty()) return false;
            prepared = prepare_destination(filePath(destination));
            if (!prepared.empty() && QMessageBox::question(this, "Replace existing file",
                "Replace " + destination + "?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return false;
        }
        const auto contents = text(view.primary);
        save_document(*controller, view.id, rs(contents), filePath(destination),
            rust::Str(prepared.data(), prepared.size()), copy);
        if (!document_dirty(*controller, view.id)) view.primary->send(SCI_SETSAVEPOINT);
        if (!copy && oldPath.isEmpty() && view.udlXml.isEmpty()) applyLexer(view, detectedLexer(pathText(document_path(*controller, view.id))));
        if (!copy) view.diskChanged = false;
        refreshInlineImages(view);
        refresh();
        refreshWatches();
        checkpointNow();
        rememberFile(copy ? filePath(destination) : document_path(*controller, view.id),
            copy ? document_encoding(*controller, view.id) : document_saved_encoding(*controller, view.id));
        return true;
    }
    bool confirmDiscard(const DocumentView& view) {
        if (!document_dirty(*controller, view.id)) return true;
        tabs->setCurrentWidget(view.page);
        const auto answer = QMessageBox::warning(this, "Unsaved document",
            "Save changes to " + qs(document_title(*controller, view.id)) + "?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel) return false;
        return answer == QMessageBox::Discard || saveView(current(), false);
    }
    bool closeTab(int index, bool confirmed = false, bool ensureDocument = true, bool rememberClosed = true) {
        auto* page = tabs->widget(index);
        const auto it = std::find_if(views.begin(), views.end(), [page](const auto& view) { return view.page == page; });
        check(it != views.end(), "Tab no longer exists.");
        if (!confirmed && !confirmDiscard(*it)) return false;
        if (it->id == comparisonTarget || it->id == comparisonLeft) clearComparisonMarkers();
        RecentEntry closed;
        closed.path = document_path(*controller, it->id);
        closed.encoding = document_saved_encoding(*controller, it->id);
        close_document(*controller, it->id, true);
        if (rememberClosed && (!closed.path.windows.empty() || !closed.path.unix.empty())) {
            closedFiles.push_back(std::move(closed));
            if (closedFiles.size() > 20) closedFiles.erase(closedFiles.begin());
        }
        const QSignalBlocker editorSignals(it->primary);
        const QSignalBlocker cloneSignals(it->clone);
        tabs->removeTab(index);
        // Delete after signal blockers have gone out of scope.
        it->page->deleteLater();
        views.erase(it);
        if (views.empty() && ensureDocument) addDocument(false);
        refresh();
        recoveryPending = true;
        checkpointNow();
        refreshWatches();
        return true;
    }
    void refreshWatches() {
        QStringList paths;
        for (const auto& view : views) {
            const auto path = pathText(document_path(*controller, view.id));
            if (!path.isEmpty() && QFileInfo::exists(path)) paths.push_back(path);
        }
        const auto watched = diskWatcher->files();
        QStringList remove;
        for (const auto& path : watched) if (!paths.contains(path)) remove.push_back(path);
        if (!remove.isEmpty()) diskWatcher->removePaths(remove);
        QStringList add;
        for (const auto& path : paths) if (!watched.contains(path)) add.push_back(path);
        if (!add.isEmpty() && !diskWatcher->addPaths(add).isEmpty())
            statusBar()->showMessage("Some files cannot be watched. Save-time conflict checks remain enabled.");
    }
        void normalizePinnedOrder() {
            if (orderingTabs || tabs->count() == 0) return;
            const QScopedValueRollback<bool> ordering(orderingTabs, true);
            for (int group = 0; group < 2; ++group) {
                std::vector<QWidget*> ordered;
                const int start = tabs->groupStart(group);
                for (const bool pinned : {true, false})
                    for (int index = start; index < start + tabs->groupCount(group); ++index)
                        for (const auto& view : views)
                            if (view.page == tabs->widget(index) && document_tab(*controller, view.id).pinned == pinned)
                                ordered.push_back(view.page);
                for (int index = 0; index < static_cast<int>(ordered.size()); ++index) {
                    const int from = tabs->indexOf(ordered[static_cast<std::size_t>(index)]);
                    if (from != start + index) tabs->moveTab(from, start + index);
                }
            }
        }
        void closeCollection(const QString& mode) {
            std::vector<std::uint64_t> targets;
            const int active = tabs->currentIndex();
            const int group = tabs->activeGroup();
            auto* original = tabs->currentWidget();
            const std::array<QWidget*, 2> originalSelections{tabs->currentInGroup(0), tabs->currentInGroup(1)};
            for (int index = tabs->count() - 1; index >= 0; --index) {
                for (const auto& view : views) if (view.page == tabs->widget(index)) {
                    if (mode != "close_all" && tabs->groupOf(view.page) != group) continue;
                    const bool selected = mode == "close_all" || (mode == "close_others" && index != active) ||
                        (mode == "close_left" && index < active) || (mode == "close_right" && index > active) ||
                        (mode == "close_unpinned" && !document_tab(*controller, view.id).pinned) ||
                        (mode == "close_unchanged" && !document_dirty(*controller, view.id));
                    if (selected) targets.push_back(view.id);
                }
            }
            for (const auto id : targets)
                for (const auto& view : views) if (view.id == id && !confirmDiscard(view)) {
                    tabs->setCurrentWidget(original);
                    for (int side = 0; side < 2; ++side)
                        if (originalSelections[side]) tabs->selectInGroup(side, originalSelections[side]);
                    tabs->setCurrentWidget(original);
                    refresh();
                    return;
                }
            for (const auto id : targets) {
                const auto found = std::find_if(views.begin(), views.end(), [id](const auto& view) { return view.id == id; });
                if (found != views.end()) closeTab(tabs->indexOf(found->page), true);
            }
        }
        void moveTab(const QString& mode) {
            const auto style = document_tab(*controller, current().id);
            const auto group = tabs->activeGroup();
            const auto start = tabs->groupStart(group);
            int pinned = 0;
            for (const auto& view : views)
                if (tabs->groupOf(view.page) == group && document_tab(*controller, view.id).pinned) ++pinned;
            const int minimum = start + (style.pinned ? 0 : pinned);
            const int maximum = start + (style.pinned ? pinned - 1 : tabs->groupCount(group) - 1);
            const int from = tabs->currentIndex();
            int target = from;
            if (mode == "move_tab_left") --target;
            else if (mode == "move_tab_right") ++target;
            else if (mode == "move_tab_start") target = minimum;
            else target = maximum;
            tabs->moveTab(from, std::clamp(target, minimum, maximum));
            recoveryPending = true;
        }
        void sortTabs() {
            struct Item { QWidget* page; QString name; bool pinned; };
            std::vector<Item> sorted;
            const int group = tabs->activeGroup();
            const int start = tabs->groupStart(group);
            for (int index = start; index < start + tabs->groupCount(group); ++index)
                for (const auto& view : views) if (view.page == tabs->widget(index))
                    sorted.push_back({view.page, qs(document_title(*controller, view.id)), document_tab(*controller, view.id).pinned});
            std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
                if (a.pinned != b.pinned) return a.pinned;
                return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
            });
            const QScopedValueRollback<bool> ordering(orderingTabs, true);
            for (int index = 0; index < static_cast<int>(sorted.size()); ++index)
                tabs->moveTab(tabs->indexOf(sorted[static_cast<std::size_t>(index)].page), start + index);
            recoveryPending = true;
        }
    void applyReload(DocumentView& view, const Opened& loaded) {
        auto* pane = view.primary;
        const bool readOnly = pane->send(SCI_GETREADONLY) != 0;
        const auto position = pane->send(SCI_GETCURRENTPOS);
        pane->send(SCI_SETREADONLY, false);
        pane->send(SCI_SETSTATUS, SC_STATUS_OK);
        pane->send(SCI_CLEARALL);
        pane->sends(SCI_ADDTEXT, loaded.text.size(), loaded.text.data());
        const bool complete = pane->send(SCI_GETSTATUS) == SC_STATUS_OK &&
            pane->send(SCI_GETLENGTH) == static_cast<sptr_t>(loaded.text.size());
        if (complete) {
            pane->send(SCI_EMPTYUNDOBUFFER);
            pane->send(SCI_SETSAVEPOINT);
            pane->send(SCI_GOTOPOS, std::min(position, static_cast<sptr_t>(loaded.text.size())));
        }
        pane->send(SCI_SETREADONLY, readOnly);
        check(complete, "Editor could not load the entire file. The disk file was not changed.");
        view.diskChanged = false;
    }
    void find(bool reverse) {
        if (query->text().isEmpty()) {
            search->openPage(false);
            return;
        }
        if (searchMode->currentData().toString() != "normal") { startSearch(false, false, reverse); return; }
        auto* editor = activeEditor();
        const auto needle = query->text().toUtf8();
        const auto length = editor->send(SCI_GETLENGTH);
        const auto position = editor->send(reverse ? SCI_GETSELECTIONSTART : SCI_GETSELECTIONEND);
        editor->send(SCI_SETSEARCHFLAGS, (matchCase->isChecked() ? SCFIND_MATCHCASE : 0) |
            (wholeWord->isChecked() ? SCFIND_WHOLEWORD : 0));
        editor->send(SCI_SETTARGETRANGE, position, reverse ? 0 : length);
        auto found = editor->sends(SCI_SEARCHINTARGET, needle.size(), needle.constData());
        if (found < 0 && search->wrapAround->isChecked()) {
            editor->send(SCI_SETTARGETRANGE, reverse ? length : 0, position);
            found = editor->sends(SCI_SEARCHINTARGET, needle.size(), needle.constData());
        }
        if (found < 0) { reportSearch("Text not found."); return; }
        editor->send(SCI_SETSEL, editor->send(SCI_GETTARGETSTART), editor->send(SCI_GETTARGETEND));
        editor->send(SCI_SCROLLCARET);
        reportSearch(QString("Match on line %1.").arg(editor->send(SCI_LINEFROMPOSITION, found) + 1));
    }
    void replace(bool all) {
        const auto needle = query->text().toUtf8();
        if (needle.isEmpty()) { search->openPage(true); return; }
        if (searchMode->currentData().toString() != "normal") { startSearch(true, all, false); return; }
        auto* editor = activeEditor();
        check(!editor->send(SCI_GETREADONLY), "Document is read-only.");
        const auto value = replacement->text().toUtf8();
        editor->send(SCI_SETSEARCHFLAGS, (matchCase->isChecked() ? SCFIND_MATCHCASE : 0) |
            (wholeWord->isChecked() ? SCFIND_WHOLEWORD : 0));
        auto position = all ? 0 : editor->send(SCI_GETSELECTIONSTART);
        const auto selectionEnd = editor->send(SCI_GETSELECTIONEND);
        editor->send(SCI_BEGINUNDOACTION);
        std::size_t count = 0;
        while (true) {
            editor->send(SCI_SETTARGETRANGE, position, all ? editor->send(SCI_GETLENGTH) : selectionEnd);
            const auto found = editor->sends(SCI_SEARCHINTARGET, needle.size(), needle.constData());
            if (found < 0 || (!all && (found != position || editor->send(SCI_GETTARGETEND) != selectionEnd))) break;
            if (editor->send(SCI_GETLENGTH) - needle.size() + value.size() > 32 * 1024 * 1024) {
                editor->send(SCI_ENDUNDOACTION);
                throw std::runtime_error("Replacement exceeds the preview limit. Undo restores any replacements already made.");
            }
            editor->sends(SCI_REPLACETARGET, value.size(), value.constData());
            position = found + value.size();
            ++count;
            if (!all) { editor->send(SCI_SETSEL, position, position); break; }
        }
        editor->send(SCI_ENDUNDOACTION);
        if (!all) find(false);
        else reportSearch(QString("Replaced %1 occurrence(s).").arg(count));
    }
    void advancedTransform(const QString& operation) {
        auto* pane = activeEditor();
        check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
        const auto targetId = current().id;
        const auto revision = document_revision(*controller, targetId);
        const auto originalStart = pane->send(SCI_GETSELECTIONSTART);
        const auto originalEnd = pane->send(SCI_GETSELECTIONEND);
        TextOperationOptions parameters;
        parameters.tab_width = static_cast<std::uint32_t>(options["editor"].toObject().value("tab_width").toInt(4));
        parameters.first_column = 0;
        parameters.numeric_kind = "integer";
        parameters.descending = false;
        parameters.blanks = "reject";
        if (operation == "numeric_sort") {
            QDialog dialog(this);
            dialog.setWindowTitle("Strict Whole-Line Numeric Sort");
            auto* layout = new QFormLayout(&dialog);
            auto* kind = new QComboBox;
            kind->addItem("Integer", "integer"); kind->addItem("Decimal dot", "dot"); kind->addItem("Decimal comma", "comma");
            auto* order = new QComboBox;
            order->addItems({"Ascending", "Descending"});
            auto* blanks = new QComboBox;
            blanks->addItem("Reject blank lines", "reject"); blanks->addItem("Blanks first", "first"); blanks->addItem("Blanks last", "last");
            layout->addRow("Number grammar", kind); layout->addRow("Order", order); layout->addRow("Blank lines", blanks);
            layout->addRow(new QLabel("Whole lines must be valid numbers. This is not natural/locale sorting."));
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            layout->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted) return;
            parameters.numeric_kind = kind->currentData().toString().toStdString();
            parameters.descending = order->currentIndex() == 1;
            parameters.blanks = blanks->currentData().toString().toStdString();
        }
        check(current().id == targetId && document_revision(*controller, targetId) == revision &&
            pane->send(SCI_GETSELECTIONSTART) == originalStart && pane->send(SCI_GETSELECTIONEND) == originalEnd,
            "Document or selection changed while options were open. Retry the operation.");
        auto start = pane->send(SCI_GETSELECTIONSTART);
        auto end = pane->send(SCI_GETSELECTIONEND);
        const auto contents = text(pane);
        if (start == end) { start = 0; end = contents.size(); }
        const bool lines = operation == "numeric_sort" || operation == "reverse_lines" ||
            operation.startsWith("remove_") || operation == "trim_leading" || operation == "trim_both" ||
            operation == "leading_spaces_to_tabs";
        if (lines) {
            const auto first = pane->send(SCI_LINEFROMPOSITION, start);
            auto last = pane->send(SCI_LINEFROMPOSITION, end);
            if (end == pane->send(SCI_POSITIONFROMLINE, last) && last > first) --last;
            start = pane->send(SCI_POSITIONFROMLINE, first);
            end = last + 1 < pane->send(SCI_GETLINECOUNT) ? pane->send(SCI_POSITIONFROMLINE, last + 1) : contents.size();
        }
        parameters.first_column = static_cast<std::uint32_t>(pane->send(SCI_GETCOLUMN, start));
        const auto source = contents.mid(start, end - start);
        const auto name = operation.toUtf8();
        const auto result = advanced_transform(rs(name), rs(source), parameters);
        check(contents.size() - source.size() + result.size() <= 32 * 1024 * 1024, "Transformation exceeds the document limit.");
        pane->send(SCI_BEGINUNDOACTION);
        pane->send(SCI_SETTARGETRANGE, start, end);
        pane->sends(SCI_REPLACETARGET, result.size(), result.data());
        pane->send(SCI_ENDUNDOACTION);
        pane->send(SCI_SETSEL, start, start + static_cast<sptr_t>(result.size()));
    }
    void commentSelection(const QString& operation) {
        auto* pane = activeEditor();
        check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
        check(pane->send(SCI_GETSELECTIONS) == 1 && !pane->send(SCI_SELECTIONISRECTANGLE),
            "Comment editing currently requires one stream selection.");
        const auto& view = current();
        const languages::Language* language = catalog->language(view.languageId);
        std::optional<languages::UdlConfiguration> user;
        if (!view.udlXml.isEmpty()) {
            QString error;
            user = languages::LanguageCatalog::parseUdlXml(view.udlXml, view.udlProfile, view.udlDocument, &error);
            check(user.has_value(), error.toUtf8().constData());
            language = &user->language;
        }
        check(language, "Choose a supported language before commenting.");
        check(language->commentError.isEmpty(), language->commentError.toUtf8().constData());
        CommentOptions commentOptions;
        commentOptions.line = rust::String(language->commentLine.constData(), language->commentLine.size());
        commentOptions.open = rust::String(language->commentStart.constData(), language->commentStart.size());
        commentOptions.close = rust::String(language->commentEnd.constData(), language->commentEnd.size());
        commentOptions.column_zero = language->id == "fortran77" || language->id == "baanc";
        commentOptions.comment_empty = language->id == "baanc";
        commentOptions.space = language->id != "baanc";
        auto mode = operation == "toggle_line_comment" ? QByteArray("toggle") :
            operation == "add_line_comment" ? QByteArray("comment") :
            operation == "remove_line_comment" ? QByteArray("uncomment") :
            operation == "block_comment" ? QByteArray("block") : QByteArray("unblock");
        if (language->commentStart.isEmpty() || language->commentEnd.isEmpty()) {
            if (mode == "block") mode = "comment";
            else if (mode == "unblock") mode = "uncomment";
        }
        const auto contents = text(pane);
        auto start = pane->send(SCI_GETSELECTIONSTART);
        auto end = pane->send(SCI_GETSELECTIONEND);
        const auto anchor = pane->send(SCI_GETANCHOR);
        const auto caret = pane->send(SCI_GETCURRENTPOS);
        sptr_t base = 0;
        QByteArray source;
        bool emptyTail = false;
        if (mode == "unblock") {
            source = contents;
        } else if (mode == "block") {
            if (start == end) {
                const auto line = pane->send(SCI_LINEFROMPOSITION, start);
                start = pane->send(SCI_GETLINEINDENTPOSITION, line);
                end = pane->send(SCI_GETLINEENDPOSITION, line);
            }
            base = start;
            source = contents.mid(start, end - start);
        } else {
            const auto first = pane->send(SCI_LINEFROMPOSITION, start);
            auto last = pane->send(SCI_LINEFROMPOSITION, end);
            if (last > first && end == pane->send(SCI_POSITIONFROMLINE, last)) --last;
            check(last - first < 10000, "Comment editing is limited to 10,000 lines.");
            base = pane->send(SCI_POSITIONFROMLINE, first);
            source = contents.mid(base, pane->send(SCI_GETLINEENDPOSITION, last) - base);
            emptyTail = commentOptions.comment_empty && !source.isEmpty() &&
                pane->send(SCI_POSITIONFROMLINE, last) == pane->send(SCI_GETLINEENDPOSITION, last);
        }
        auto edits = comment_edits(rs(source), mode == "unblock" ? start : 0,
            mode == "unblock" ? end : source.size(), rs(mode), commentOptions);
        if (emptyTail) {
            auto tailEdits = comment_edits(rust::Str(), 0, 0, rs(mode), commentOptions);
            for (auto& edit : tailEdits) {
                edit.start += source.size(); edit.end += source.size();
                edits.push_back(std::move(edit));
            }
        }
        if (edits.empty()) { statusBar()->showMessage("No applicable comment markers or nonempty lines."); return; }
        sptr_t delta = 0;
        std::uint64_t previousEnd = 0;
        for (const auto& edit : edits) {
            check(edit.start >= previousEnd && edit.end >= edit.start && edit.end <= static_cast<std::uint64_t>(source.size()),
                "Invalid comment edit range.");
            previousEnd = edit.end;
            delta += static_cast<sptr_t>(edit.text.size()) - static_cast<sptr_t>(edit.end - edit.start);
        }
        check(contents.size() + delta <= 32 * 1024 * 1024, "Comment output exceeds the document limit.");
        auto mapPosition = [&](sptr_t position) {
            sptr_t shift = 0;
            for (const auto& edit : edits) {
                const auto left = base + static_cast<sptr_t>(edit.start);
                const auto right = base + static_cast<sptr_t>(edit.end);
                if (position < left) break;
                if (position <= right)
                    return left + shift + (position == right ? static_cast<sptr_t>(edit.text.size()) : 0);
                shift += static_cast<sptr_t>(edit.text.size()) - (right - left);
            }
            return position + shift;
        };
        const auto mappedAnchor = mapPosition(anchor);
        const auto mappedCaret = mapPosition(caret);
        pane->send(SCI_BEGINUNDOACTION);
        pane->send(SCI_SETSTATUS, SC_STATUS_OK);
        for (std::size_t i = edits.size(); i > 0; --i) {
            const auto& edit = edits[i - 1];
            pane->send(SCI_SETTARGETRANGE, base + edit.start, base + edit.end);
            pane->sends(SCI_REPLACETARGET, edit.text.size(), edit.text.data());
            if (pane->send(SCI_GETSTATUS) != SC_STATUS_OK) {
                pane->send(SCI_ENDUNDOACTION);
                throw std::runtime_error("Comment edit failed. Undo restores any changes already applied.");
            }
        }
        pane->send(SCI_ENDUNDOACTION);
        if (mode == "block") {
            const auto contentStart = start + static_cast<sptr_t>(language->commentStart.size()) + 1;
            const auto contentEnd = end + static_cast<sptr_t>(language->commentStart.size()) + 1;
            pane->send(SCI_SETSEL, caret < anchor ? contentEnd : contentStart, caret < anchor ? contentStart : contentEnd);
        } else pane->send(SCI_SETSEL, mappedAnchor, mappedCaret);
    }
    static QByteArray formattedDateTime(const QDateTime& value, const QString& mode, const QString& pattern = {}) {
        check(value.isValid(), "Cannot read a valid date and time.");
        QString formatted;
        if (mode == "date_time_short") formatted = QLocale().toString(value, QLocale::ShortFormat);
        else if (mode == "date_time_long") formatted = QLocale().toString(value, QLocale::LongFormat);
        else {
            check(mode == "date_time_custom" && !pattern.isEmpty() && pattern.size() <= 128 && !pattern.contains(QChar(0)),
                "Custom Qt date/time format must contain 1-128 characters without NUL.");
            formatted = value.toString(pattern);
        }
        const auto bytes = formatted.toUtf8();
        check(!bytes.isEmpty() && bytes.size() <= 4096 && !bytes.contains('\0'), "Formatted date/time exceeds its text limits.");
        return bytes;
    }
    void insertGeneratedText(const QByteArray& bytes) {
        check(!macroPlaybackActive, "Finish macro playback before inserting generated text.");
        auto* pane = activeEditor();
        check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
        check(pane->send(SCI_GETSELECTIONS) == 1 && !pane->send(SCI_SELECTIONISRECTANGLE),
            "Generated text insertion currently requires one stream selection.");
        check(bytes.size() <= 4096 && !bytes.contains('\0'), "Generated text exceeds its limits.");
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString decoded = decoder.decode(bytes);
        Q_UNUSED(decoded);
        check(!decoder.hasError(), "Generated text is not valid UTF-8.");
        const auto removed = pane->send(SCI_GETSELECTIONEND) - pane->send(SCI_GETSELECTIONSTART);
        check(pane->send(SCI_GETLENGTH) - removed + bytes.size() <= 32 * 1024 * 1024, "Generated text exceeds the document limit.");
        pane->send(SCI_BEGINUNDOACTION);
        pane->send(SCI_SETSTATUS, SC_STATUS_OK);
        pane->sends(SCI_REPLACESEL, 0, bytes.constData());
        pane->send(SCI_ENDUNDOACTION);
        check(pane->send(SCI_GETSTATUS) == SC_STATUS_OK, "Insertion failed. Undo restores any partial changes.");
    }
    void insertDateTime(const QString& mode) {
        check(!activeEditor()->send(SCI_GETREADONLY), "Document is read-only.");
        QString pattern;
        const auto document = current().id;
        const auto revision = document_revision(*controller, document);
        auto* pane = activeEditor();
        const auto anchor = pane->send(SCI_GETANCHOR);
        const auto caret = pane->send(SCI_GETCURRENTPOS);
        if (mode == "date_time_custom") {
            bool accepted = false;
            pattern = QInputDialog::getText(this, "Custom Date and Time",
                "Qt format (not strftime): yyyy-MM-dd HH:mm:ss; quote literal text with apostrophes",
                QLineEdit::Normal, "yyyy-MM-dd HH:mm:ss", &accepted);
            if (!accepted) return;
        }
        check(current().id == document && activeEditor() == pane && document_revision(*controller, document) == revision &&
            pane->send(SCI_GETANCHOR) == anchor && pane->send(SCI_GETCURRENTPOS) == caret,
            "The document or selection changed during date/time entry.");
        insertGeneratedText(formattedDateTime(QDateTime::currentDateTime(), mode, pattern));
    }
    // Candidate lines are found in the engine so untouched documents cost one scan, not one per line.
    std::set<sptr_t> inlineImageCandidateLines(ScintillaEditBase* pane) {
        std::set<sptr_t> lines;
        const auto length = pane->send(SCI_GETLENGTH);
        const auto flags = pane->send(SCI_GETSEARCHFLAGS);
        const auto targetStart = pane->send(SCI_GETTARGETSTART);
        const auto targetEnd = pane->send(SCI_GETTARGETEND);
        pane->send(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE);
        for (const char* needle : {"[[image:", "![", "<img "}) {
            sptr_t position = 0;
            while (position < length && lines.size() < 1024) {
                pane->send(SCI_SETTARGETRANGE, static_cast<uptr_t>(position), length);
                const auto found = pane->sends(SCI_SEARCHINTARGET, std::strlen(needle), needle);
                if (found < 0) break;
                const auto line = pane->send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(found));
                lines.insert(line);
                position = pane->send(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(line)) + 1;
            }
        }
        pane->send(SCI_SETSEARCHFLAGS, flags);
        pane->send(SCI_SETTARGETRANGE, static_cast<uptr_t>(targetStart), targetEnd);
        return lines;
    }
    // Blank annotations reserve the rows an image needs; aligned comparison owns annotations while it runs.
    void refreshInlineImages(DocumentView& view) {
        if (tearingDown) return;
        const auto documentPath = pathText(document_path(*controller, view.id));
        const auto folder = documentPath.isEmpty() ? QString() : QFileInfo(documentPath).absolutePath();
        auto* primary = static_cast<WorkspaceEditor*>(view.primary);
        auto* clone = static_cast<WorkspaceEditor*>(view.clone);
        for (auto* pane : {primary, clone}) pane->images.folder = folder;
        if (comparisonEnabled && comparisonAligned) return;
        const int textHeight = static_cast<int>(primary->send(SCI_TEXTHEIGHT, 0));
        std::vector<std::pair<sptr_t, int>> reserved;
        if (primary->send(SCI_GETLENGTH) <= 32 * 1024 * 1024)
            for (const auto line : inlineImageCandidateLines(primary)) {
                const auto image = parseInlineImageLine(inlineImageLineText(primary, line));
                if (!image) continue;
                const int wrapped = std::max(1, static_cast<int>(primary->send(SCI_WRAPCOUNT, static_cast<uptr_t>(line))));
                reserved.emplace_back(line, std::max(0, primary->images.rowsFor(*image, textHeight) - wrapped));
            }
        if (reserved.empty() && !view.inlineImages) return;
        view.inlineImages = !reserved.empty();
        for (auto* pane : {primary, clone}) pane->send(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_STANDARD);
        primary->send(SCI_STYLESETBACK, 251, primary->send(SCI_STYLEGETBACK, STYLE_DEFAULT));
        primary->send(SCI_ANNOTATIONCLEARALL);
        for (const auto& [line, rows] : reserved) {
            if (rows <= 0) continue;
            const QByteArray blank = QByteArray(" ") + QByteArray(rows - 1, '\n');
            primary->sends(SCI_ANNOTATIONSETTEXT, static_cast<uptr_t>(line), blank.constData());
            primary->send(SCI_ANNOTATIONSETSTYLE, static_cast<uptr_t>(line), 251);
        }
        for (auto* pane : {primary, clone}) pane->viewport()->update();
    }
    void queueInlineImages(std::uint64_t id) {
        pendingInlineImages.insert(id);
        if (inlineImageTimer) inlineImageTimer->start();
    }
    void flushInlineImages() {
        const auto pending = pendingInlineImages;
        pendingInlineImages.clear();
        for (auto& view : views) if (pending.count(view.id) > 0) refreshInlineImages(view);
    }
    // A resize only ever rewrites the size of a line that still holds the same image.
    void resizeInlineImage(DocumentView& view, ScintillaEditBase* pane, sptr_t line, const InlineImageLine& image) {
        if (tearingDown || macroPlaybackActive || macroRecording || pane->send(SCI_GETREADONLY)) return;
        if (line < 0 || line >= pane->send(SCI_GETLINECOUNT)) return;
        const auto existing = parseInlineImageLine(inlineImageLineText(pane, line));
        if (!existing || existing->flavor != image.flavor || existing->reference != image.reference) return;
        auto updated = *existing;
        updated.width = image.width;
        updated.height = image.height;
        if (updated.width == existing->width && updated.height == existing->height) return;
        if (!inlineImageLineRoundTrips(updated)) return;
        const auto sizedLine = formatInlineImageLine(updated).toUtf8();
        const auto targetStart = pane->send(SCI_GETTARGETSTART);
        const auto targetEnd = pane->send(SCI_GETTARGETEND);
        pane->send(SCI_BEGINUNDOACTION);
        pane->send(SCI_SETTARGETRANGE, pane->send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(line)),
            pane->send(SCI_GETLINEENDPOSITION, static_cast<uptr_t>(line)));
        pane->sends(SCI_REPLACETARGET, static_cast<uptr_t>(sizedLine.size()), sizedLine.constData());
        pane->send(SCI_ENDUNDOACTION);
        pane->send(SCI_SETTARGETRANGE, static_cast<uptr_t>(targetStart), targetEnd);
        refreshInlineImages(view);
    }
    static QImage clipboardImage() {
        const auto* mime = QApplication::clipboard()->mimeData();
        if (!mime || !mime->hasImage()) return {};
        return qvariant_cast<QImage>(mime->imageData());
    }
    // Ctrl+V keeps its text behavior whenever the clipboard also carries text or file URLs.
    static bool clipboardHoldsOnlyImage() {
        const auto* mime = QApplication::clipboard()->mimeData();
        return mime && mime->hasImage() && !mime->hasText() && !mime->hasUrls() && !clipboardImage().isNull();
    }
    // Pasted images become sibling files so the document stays plain text while the canvas shows the picture.
    QString inlineImageFolder() {
        const auto documentPath = pathText(document_path(*controller, current().id));
        if (documentPath.isEmpty()) {
            const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QDir root(base.isEmpty() ? QDir::tempPath() : base);
            check(root.mkpath("pasted-images"), "The folder for pasted images could not be created.");
            return root.absoluteFilePath("pasted-images");
        }
        const QFileInfo info(documentPath);
        QDir root(info.absolutePath());
        const auto name = info.completeBaseName() + ".images";
        check(root.mkpath(name), "The folder for pasted images could not be created beside the document.");
        return root.absoluteFilePath(name);
    }
    static QString unusedImagePath(const QString& folder) {
        const QDir root(folder);
        const auto stamp = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
        for (int attempt = 1; attempt <= 999; ++attempt) {
            const auto name = attempt == 1 ? "image-" + stamp + ".png" :
                "image-" + stamp + "-" + QString::number(attempt) + ".png";
            if (!QFileInfo::exists(root.filePath(name))) return root.absoluteFilePath(name);
        }
        throw std::runtime_error("This folder already holds every pasted-image name for the current second.");
    }
    static QString inlineImageReference(const QString& documentPath, const QString& imagePath) {
        QString target = imagePath;
        if (!documentPath.isEmpty()) {
            const auto relative = QFileInfo(documentPath).dir().relativeFilePath(imagePath);
            if (!relative.isEmpty() && !relative.startsWith("..")) target = relative;
        }
        return target.replace('\\', '/');
    }
    static QSize fittedImageSize(ScintillaEditBase* pane, QSize natural) {
        const int limit = std::clamp(pane->viewport()->width() - 48, 64, 720);
        if (natural.width() <= limit || natural.width() <= 0) return natural;
        return QSize(limit, std::max(1, static_cast<int>(static_cast<qint64>(natural.height()) * limit / natural.width())));
    }
    // An image owns its own line so the rows reserved for it never split a line of prose.
    static QByteArray inlineImagePayload(ScintillaEditBase* pane, const QString& line) {
        const auto mode = pane->send(SCI_GETEOLMODE);
        const QByteArray eol = mode == SC_EOL_CR ? "\r" : mode == SC_EOL_CRLF ? "\r\n" : "\n";
        const auto start = pane->send(SCI_GETSELECTIONSTART);
        QByteArray payload;
        if (start != pane->send(SCI_POSITIONFROMLINE, pane->send(SCI_LINEFROMPOSITION, start))) payload += eol;
        payload += line.toUtf8();
        payload += eol;
        return payload;
    }
    void pasteImageAs(const QImage& image, const QString& folder) {
        check(!macroPlaybackActive && !macroRecording, "Finish the macro before pasting an image.");
        check(!image.isNull() && image.width() > 0 && image.height() > 0, "The clipboard does not contain a readable image.");
        check(static_cast<qint64>(image.width()) * image.height() <= 64LL * 1024 * 1024,
            "The clipboard image exceeds the 64-megapixel paste limit.");
        auto* pane = activeEditor();
        check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
        check(QFileInfo(folder).isDir(), "The folder for pasted images does not exist.");
        const auto absolute = unusedImagePath(folder);
        check(disk_updated_documents(*controller, filePath(absolute)).empty(),
            "That image file is open in this editor; paste again.");
        const auto documentPath = pathText(document_path(*controller, current().id));
        InlineImageLine entry;
        entry.flavor = inlineImageFlavorFor(documentPath);
        entry.reference = inlineImageReference(documentPath, absolute);
        if (entry.flavor != InlineImageFlavor::Token) entry.alt = "pasted image";
        const auto size = fittedImageSize(pane, image.size());
        entry.width = size.width();
        entry.height = size.height();
        check(inlineImageLineRoundTrips(entry), "The pasted image path cannot be written on a single line.");
        QByteArray encoded;
        {
            QBuffer buffer(&encoded);
            check(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"),
                "The clipboard image could not be encoded as PNG.");
        }
        check(!encoded.isEmpty() && encoded.size() <= 64 * 1024 * 1024, "The encoded image exceeds the 64 MiB paste limit.");
        QSaveFile file(absolute);
        check(file.open(QIODevice::WriteOnly) && file.write(encoded) == encoded.size() && file.commit(),
            "The pasted image could not be written to disk.");
        insertGeneratedText(inlineImagePayload(pane, formatInlineImageLine(entry)));
        refreshInlineImages(current());
    }
    void pasteImage() {
        const auto image = clipboardImage();
        check(!image.isNull(), "The clipboard does not contain an image. Copy an image, then paste it here.");
        pasteImageAs(image, inlineImageFolder());
        statusBar()->showMessage("Pasted the image into the document. Click it, then drag its corner grip to resize.");
    }
    void documentSummary() {
        auto* pane = activeEditor();
        const auto statistics = QJsonDocument::fromJson(qs(document_statistics(rs(text(pane)))).toUtf8()).object();
        QString summary = pathText(document_path(*controller, current().id));
        if (summary.isEmpty()) summary = "Untitled document";
        summary += "\nEncoding: " + qs(document_encoding(*controller, current().id));
        for (const auto& field : std::vector<std::pair<QString, QString>>{
            {"utf8_bytes", "UTF-8 buffer bytes"}, {"unicode_scalars", "Unicode scalar values"},
            {"utf16_units", "UTF-16 code units"}, {"whitespace_delimited_words", "Whitespace-delimited words"},
            {"lines", "Lines"}, {"longest_line_scalars", "Longest line (Unicode scalars)"},
            {"crlf", "CRLF endings"}, {"lf", "Standalone LF endings"}, {"cr", "Standalone CR endings"}, {"tabs", "Tabs"}})
            summary += "\n" + field.second + ": " + QString::number(statistics[field.first].toInteger());
        summary += "\nPrimary selection bytes: " + QString::number(pane->send(SCI_GETSELECTIONEND) - pane->send(SCI_GETSELECTIONSTART));
        summary += "\nSelections: " + QString::number(pane->send(SCI_GETSELECTIONS));
        QMessageBox dialog(this);
        dialog.setWindowTitle("Document Summary");
        dialog.setTextFormat(Qt::PlainText);
        dialog.setText(summary);
        dialog.exec();
    }
    void duplicateText(bool selection) {
        auto* pane = activeEditor();
        check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
        const auto count = pane->send(SCI_GETSELECTIONS);
        check(count <= 10000, "Duplication supports at most 10,000 selections.");
        sptr_t growth = 0;
        for (sptr_t index = 0; index < count; ++index) {
            const auto length = pane->send(SCI_GETSELECTIONNEND, index) - pane->send(SCI_GETSELECTIONNSTART, index);
            const auto line = pane->send(SCI_LINEFROMPOSITION, pane->send(SCI_GETSELECTIONNCARET, index));
            growth += selection && length > 0 ? length : pane->send(SCI_LINELENGTH, line) + 2;
        }
        check(pane->send(SCI_GETLENGTH) + growth <= 32 * 1024 * 1024, "Duplication would exceed the document limit.");
        pane->send(SCI_SETSTATUS, SC_STATUS_OK);
        pane->send(selection ? SCI_SELECTIONDUPLICATE : SCI_LINEDUPLICATE);
        check(pane->send(SCI_GETSTATUS) == SC_STATUS_OK, "Duplication failed. Undo restores text changes already applied.");
    }
    void lineLayout(const QString& operation) {
        auto* pane = activeEditor();
        check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
        check(pane->send(SCI_GETSELECTIONS) == 1 && !pane->send(SCI_SELECTIONISRECTANGLE),
            "This line operation requires one stream selection.");
        const auto caret = pane->send(SCI_GETCURRENTPOS);
        const auto first = pane->send(SCI_LINEFROMPOSITION, pane->send(SCI_GETSELECTIONSTART));
        auto last = pane->send(SCI_LINEFROMPOSITION, pane->send(SCI_GETSELECTIONEND));
        if (last > first && pane->send(SCI_GETSELECTIONEND) == pane->send(SCI_POSITIONFROMLINE, last)) --last;
        if (operation == "blank_line_above" || operation == "blank_line_below") {
            const auto ending = pane->send(SCI_GETEOLMODE);
            const QByteArray eol = ending == SC_EOL_CRLF ? "\r\n" : ending == SC_EOL_CR ? "\r" : "\n";
            const auto line = pane->send(SCI_LINEFROMPOSITION, caret);
            const auto begin = pane->send(SCI_POSITIONFROMLINE, line);
            const auto end = begin + pane->send(SCI_LINELENGTH, line);
            const bool below = operation == "blank_line_below";
            const auto position = below ? end : begin;
            const bool lastWithoutEnding = below && end == pane->send(SCI_GETLINEENDPOSITION, line);
            check(pane->send(SCI_GETLENGTH) + eol.size() <= 32 * 1024 * 1024, "The new line exceeds the document limit.");
            pane->send(SCI_BEGINUNDOACTION);
            pane->send(SCI_SETSTATUS, SC_STATUS_OK);
            pane->sends(SCI_INSERTTEXT, position, eol.constData());
            pane->send(SCI_ENDUNDOACTION);
            check(pane->send(SCI_GETSTATUS) == SC_STATUS_OK, "Line insertion failed. Undo restores any changes.");
            pane->send(SCI_SETSEL, position + (lastWithoutEnding ? eol.size() : 0), position + (lastWithoutEnding ? eol.size() : 0));
            return;
        }
        const auto start = pane->send(SCI_POSITIONFROMLINE, first);
        const auto end = pane->send(SCI_GETLINEENDPOSITION, last);
        check(last - first < 10000 && end - start <= 4 * 1024 * 1024, "Line layout is limited to 10,000 lines / 4 MiB.");
        if (operation == "join_lines" && first == last) { statusBar()->showMessage("Select more than one line to join."); return; }
        if (operation == "split_lines")
            check(pane->send(SCI_GETLENGTH) + (end - start) * 2 <= 32 * 1024 * 1024, "Potential split output exceeds the document budget.");
        pane->send(SCI_SETSEL, start, end);
        pane->send(SCI_SETTARGETRANGE, start, end);
        pane->send(SCI_BEGINUNDOACTION);
        pane->send(SCI_SETSTATUS, SC_STATUS_OK);
        pane->send(operation == "join_lines" ? SCI_LINESJOIN : SCI_LINESSPLIT, 0);
        pane->send(SCI_ENDUNDOACTION);
        check(pane->send(SCI_GETSTATUS) == SC_STATUS_OK, "Line layout failed. Undo restores changes already applied.");
    }
    struct BookmarkLine {
        sptr_t line; sptr_t start; sptr_t contentEnd; sptr_t end;
        bool operator==(const BookmarkLine& other) const {
            return line == other.line && start == other.start && contentEnd == other.contentEnd && end == other.end;
        }
    };
    std::vector<BookmarkLine> bookmarkLines(bool marked) {
        auto* pane = activeEditor();
        const auto count = pane->send(SCI_GETLINECOUNT);
        check(count <= 100000, "Bookmark batch operations support at most 100,000 document lines.");
        std::vector<BookmarkLine> lines;
        for (sptr_t line = 0; line < count; ++line) {
            if (((pane->send(SCI_MARKERGET, line) & 1) != 0) != marked) continue;
            check(lines.size() < 10000, "Narrow the bookmarks: at most 10,000 lines can be processed per operation.");
            const auto start = pane->send(SCI_POSITIONFROMLINE, line);
            lines.push_back({line, start, pane->send(SCI_GETLINEENDPOSITION, line), start + pane->send(SCI_LINELENGTH, line)});
        }
        return lines;
    }
    QByteArray bookmarkedText() {
        const auto contents = text(activeEditor());
        QByteArray collected;
        for (const auto& line : bookmarkLines(true)) {
            check(collected.size() + line.end - line.start <= 16 * 1024 * 1024, "Bookmarked clipboard text exceeds 16 MiB.");
            collected += contents.mid(line.start, line.end - line.start);
        }
        return collected;
    }
    void editBookmarks(const QString& operation, const QByteArray& pasteText = {}) {
        check(operation == "cut_bookmarks" || operation == "delete_bookmarks" ||
            operation == "delete_unmarked" || operation == "paste_bookmarks", "Unknown bookmark edit operation.");
        auto* pane = activeEditor();
        check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
        const auto lines = bookmarkLines(operation != "delete_unmarked");
        if (lines.empty()) { statusBar()->showMessage("No matching lines."); return; }
        const bool paste = operation == "paste_bookmarks";
        check(!pasteText.contains('\0') && pasteText.size() <= 16 * 1024 * 1024, "Clipboard text contains NUL or exceeds 16 MiB.");
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString decodedPaste = decoder.decode(pasteText);
        Q_UNUSED(decodedPaste);
        check(!decoder.hasError(), "Bookmarked replacement text is not valid UTF-8.");
        sptr_t removed = 0;
        for (const auto& line : lines) removed += (paste ? line.contentEnd : line.end) - line.start;
        const auto inserted = paste ? static_cast<qint64>(pasteText.size()) * static_cast<qint64>(lines.size()) : 0;
        check(inserted <= 16 * 1024 * 1024 && pane->send(SCI_GETLENGTH) - removed + inserted <= 32 * 1024 * 1024,
            "Bookmarked replacement exceeds its document/edit budget.");
        const auto beforeUndo = pane->send(SCI_GETUNDOCURRENT);
        QSignalBlocker primarySignals(current().primary);
        QSignalBlocker cloneSignals(current().clone);
        pane->send(SCI_BEGINUNDOACTION);
        pane->send(SCI_SETSTATUS, SC_STATUS_OK);
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            if (!paste) pane->send(SCI_MARKERDELETE, it->line, 0);
            pane->send(SCI_SETTARGETRANGE, it->start, paste ? it->contentEnd : it->end);
            pane->sends(SCI_REPLACETARGET, paste ? pasteText.size() : 0, paste ? pasteText.constData() : "");
            if (pane->send(SCI_GETSTATUS) != SC_STATUS_OK) break;
        }
        pane->send(SCI_ENDUNDOACTION);
        const bool applied = pane->send(SCI_GETSTATUS) == SC_STATUS_OK;
        primarySignals.unblock(); cloneSignals.unblock();
        if (pane->send(SCI_GETUNDOCURRENT) != beforeUndo)
            document_changed(*controller, current().id, pane->send(SCI_GETMODIFY) != 0);
        recoveryPending = true;
        refresh(); refreshWatches();
        check(applied, "Bookmark edit failed. Undo restores text changes already applied.");
    }
    void bookmarkOperation(const QString& operation) {
        auto* pane = activeEditor();
        if (operation == "invert_bookmarks") {
            const auto count = pane->send(SCI_GETLINECOUNT);
            check(count <= 100000, "Bookmark inversion supports at most 100,000 lines.");
            for (sptr_t line = 0; line < count; ++line)
                pane->send(pane->send(SCI_MARKERGET, line) & 1 ? SCI_MARKERDELETE : SCI_MARKERADD, line, 0);
            recoveryPending = true;
            return;
        }
        if (operation == "copy_bookmarks" || operation == "cut_bookmarks") {
            if (operation == "cut_bookmarks") check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
            const auto originalLines = bookmarkLines(true);
            if (originalLines.empty()) { statusBar()->showMessage("No bookmarked lines to copy."); return; }
            const auto document = current().id;
            const auto revision = document_revision(*controller, document);
            const auto collected = QString::fromUtf8(bookmarkedText());
            QApplication::clipboard()->setText(collected);
            check(QApplication::clipboard()->text() == collected, "Clipboard ownership failed; no text was removed.");
            if (operation == "cut_bookmarks") {
                check(current().id == document && document_revision(*controller, document) == revision &&
                    bookmarkLines(true) == originalLines, "Document or bookmarks changed while copying; no text was removed.");
                editBookmarks(operation);
            }
            return;
        }
        QByteArray pasteText;
        if (operation == "paste_bookmarks") {
            const auto* mime = QApplication::clipboard()->mimeData();
            check(mime && mime->hasText(), "The clipboard contains no text.");
            pasteText = QApplication::clipboard()->text().toUtf8();
        }
        editBookmarks(operation, pasteText);
    }
    void matchingBrace(bool select) {
        auto* pane = activeEditor();
        auto position = pane->send(SCI_GETCURRENTPOS);
        const auto isBrace = [pane](sptr_t where) {
            return where >= 0 && where < pane->send(SCI_GETLENGTH) &&
                QByteArray("()[]{}").contains(static_cast<char>(pane->send(SCI_GETCHARAT, where)));
        };
        if (isBrace(position - 1)) --position;
        check(isBrace(position), "Place the caret beside a brace.");
        pane->send(SCI_COLOURISE, 0, -1);
        const auto opposite = pane->send(SCI_BRACEMATCH, position, 0);
        check(opposite >= 0, "No matching brace was found in the same lexer context.");
        if (select) pane->send(SCI_SETSEL, std::min(position, opposite), std::max(position, opposite) + 1);
        else pane->send(SCI_GOTOPOS, opposite);
        pane->send(SCI_CHOOSECARETX);
        pane->send(SCI_SCROLLCARET);
    }
    struct ColumnRange { sptr_t start; sptr_t end; sptr_t padding; };
    void applyColumnValues(ScintillaEditBase* pane, const std::vector<ColumnRange>& ranges, const std::vector<QByteArray>& values) {
        check(!pane->send(SCI_GETREADONLY) && ranges.size() == values.size(), "Invalid or read-only column operation.");
        sptr_t removed = 0;
        sptr_t inserted = 0;
        std::vector<QByteArray> replacements;
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            const auto& range = ranges[i];
            check(range.start >= 0 && range.end >= range.start && range.end <= pane->send(SCI_GETLENGTH) &&
                range.padding >= 0 && range.padding <= 4096, "Column range is invalid.");
            if (i > 0) check(range.start >= ranges[i - 1].end && range.start != ranges[i - 1].start, "Column selections overlap.");
            QByteArray columnText(range.padding, ' ');
            columnText += values[i];
            check(columnText.size() <= 4096 && !columnText.contains('\0') && !columnText.contains('\n') && !columnText.contains('\r'),
                "Column values must fit 4096 bytes and contain no line breaks or NUL.");
            removed += range.end - range.start; inserted += columnText.size();
            replacements.push_back(std::move(columnText));
        }
        check(inserted <= 16 * 1024 * 1024 && pane->send(SCI_GETLENGTH) - removed + inserted <= 32 * 1024 * 1024,
            "Column output exceeds its budget.");
        pane->send(SCI_BEGINUNDOACTION);
        pane->send(SCI_SETSTATUS, SC_STATUS_OK);
        for (std::size_t i = ranges.size(); i > 0; --i) {
            pane->send(SCI_SETTARGETRANGE, ranges[i - 1].start, ranges[i - 1].end);
            pane->sends(SCI_REPLACETARGET, replacements[i - 1].size(), replacements[i - 1].constData());
            if (pane->send(SCI_GETSTATUS) != SC_STATUS_OK) {
                pane->send(SCI_ENDUNDOACTION);
                throw std::runtime_error("Column insertion failed. Undo restores any partial changes.");
            }
        }
        pane->send(SCI_ENDUNDOACTION);
        pane->send(SCI_SETSELECTIONMODE, SC_SEL_STREAM);
        pane->send(SCI_CLEARSELECTIONS);
        sptr_t delta = 0;
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            const auto start = ranges[i].start + delta + ranges[i].padding;
            const auto end = start + values[i].size();
            pane->send(i == 0 ? SCI_SETSELECTION : SCI_ADDSELECTION, end, start);
            delta += replacements[i].size() - (ranges[i].end - ranges[i].start);
        }
    }
    void columnEditor() {
        auto* pane = activeEditor();
        check(!pane->send(SCI_GETREADONLY), "Document is read-only.");
        const auto selections = pane->send(SCI_GETSELECTIONS);
        check(selections <= 10000, "Column editing supports at most 10,000 rows.");
        const auto selectionStart = pane->send(SCI_GETSELECTIONSTART);
        const auto selectionEnd = pane->send(SCI_GETSELECTIONEND);
        const auto documentId = current().id;
        const auto revision = document_revision(*controller, documentId);
        const auto firstLine = pane->send(SCI_LINEFROMPOSITION, selectionStart);
        const bool caretOnly = selections == 1 && selectionStart == selectionEnd;
        QDialog dialog(this);
        dialog.setWindowTitle("Column Editor");
        auto* layout = new QFormLayout(&dialog);
        auto* mode = new QComboBox; mode->addItems({"Text", "Numbers"});
        auto* value = new QLineEdit;
        auto* start = new QLineEdit("0");
        auto* step = new QLineEdit("1");
        auto* repeat = new QSpinBox; repeat->setRange(1, 10000);
        auto* rows = new QSpinBox;
        rows->setRange(1, static_cast<int>(std::min<sptr_t>(10000, pane->send(SCI_GETLINECOUNT) - firstLine)));
        rows->setEnabled(caretOnly);
        auto* base = new QComboBox;
        for (const auto& entry : {std::pair{"Decimal", "decimal"}, {"Hex lowercase", "hex"}, {"Hex uppercase", "HEX"}, {"Octal", "octal"}, {"Binary", "binary"}})
            base->addItem(entry.first, entry.second);
        auto* padding = new QComboBox;
        for (const auto& entry : {std::pair{"None", "none"}, {"Zeros", "zero"}, {"Spaces", "space"}, {"Auto zeros", "auto-zero"}, {"Auto spaces", "auto-space"}})
            padding->addItem(entry.first, entry.second);
        auto* width = new QSpinBox; width->setRange(0, 256);
        layout->addRow("Mode", mode); layout->addRow("Text", value);
        layout->addRow("Starting number", start); layout->addRow("Increment", step);
        layout->addRow("Repeat each number", repeat); layout->addRow("Base", base);
        layout->addRow("Padding", padding); layout->addRow("Minimum width", width);
        layout->addRow("Existing rows (caret only)", rows);
        layout->addRow(new QLabel("Multiple/rectangular selections are replaced.\nA line selection inserts at its starting column."));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        check(current().id == documentId && document_revision(*controller, documentId) == revision &&
            pane->send(SCI_GETSELECTIONSTART) == selectionStart && pane->send(SCI_GETSELECTIONEND) == selectionEnd &&
            pane->send(SCI_GETSELECTIONS) == selections, "Document or selection changed. Reopen the Column Editor.");
        std::vector<ColumnRange> ranges;
        if (selections > 1 || pane->send(SCI_SELECTIONISRECTANGLE)) {
            for (sptr_t index = 0; index < selections; ++index) {
                const auto begin = pane->send(SCI_GETSELECTIONNSTART, index);
                const auto end = pane->send(SCI_GETSELECTIONNEND, index);
                check(pane->send(SCI_LINEFROMPOSITION, begin) == pane->send(SCI_LINEFROMPOSITION, end), "Each column selection must be on one line.");
                ranges.push_back({begin, end, pane->send(SCI_GETSELECTIONNSTARTVIRTUALSPACE, index)});
            }
        } else {
            auto lastLine = caretOnly ? firstLine + rows->value() - 1 : pane->send(SCI_LINEFROMPOSITION, selectionEnd);
            if (!caretOnly && selectionEnd == pane->send(SCI_POSITIONFROMLINE, lastLine) && lastLine > firstLine) --lastLine;
            check(lastLine - firstLine < 10000, "Column row limit exceeded.");
            const auto column = pane->send(SCI_GETCOLUMN, selectionStart);
            for (auto line = firstLine; line <= lastLine; ++line) {
                const auto position = pane->send(SCI_FINDCOLUMN, line, column);
                ranges.push_back({position, position, std::max<sptr_t>(0, column - pane->send(SCI_GETCOLUMN, position))});
            }
        }
        std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
        std::vector<QByteArray> values;
        if (mode->currentIndex() == 0) {
            const auto textValue = value->text().toUtf8();
            check(!textValue.isEmpty(), "Enter column text or choose Numbers.");
            values.assign(ranges.size(), textValue);
        } else {
            ColumnOptions parameters;
            parameters.start = start->text().toStdString(); parameters.step = step->text().toStdString();
            parameters.rows = static_cast<std::uint32_t>(ranges.size()); parameters.repeat = static_cast<std::uint32_t>(repeat->value());
            parameters.base = base->currentData().toString().toStdString(); parameters.padding = padding->currentData().toString().toStdString();
            parameters.width = static_cast<std::uint32_t>(width->value());
            for (const auto& generated : column_values(parameters)) values.push_back(qs(generated).toUtf8());
        }
        applyColumnValues(pane, ranges, values);
    }
    void transform(const QString& operation) {
        auto* editor = activeEditor();
        check(!editor->send(SCI_GETREADONLY), "Document is read-only.");
        const auto contents = text(editor);
        auto start = editor->send(SCI_GETSELECTIONSTART);
        auto end = editor->send(SCI_GETSELECTIONEND);
        if (start == end) { start = 0; end = contents.size(); }
        const auto selected = contents.mid(start, end - start);
        const auto op = operation.toUtf8();
        const auto result = transform_text(rs(op), rs(selected));
        check(contents.size() - selected.size() + result.size() <= 32 * 1024 * 1024, "Transformed document exceeds the preview limit.");
        editor->send(SCI_BEGINUNDOACTION);
        editor->send(SCI_SETTARGETRANGE, start, end);
        editor->sends(SCI_REPLACETARGET, result.size(), result.data());
        editor->send(SCI_ENDUNDOACTION);
        editor->send(SCI_SETSEL, start, start + static_cast<sptr_t>(result.size()));
    }
    void showWorkspace() {
        const auto directory = QFileDialog::getExistingDirectory(this, "Folder as Workspace");
        if (directory.isEmpty()) return;
        if (workspaceDock) delete workspaceDock;
        workspaceDock = new QDockWidget("Folder as Workspace", this);
        workspaceDock->setObjectName("workspace-dock");
        auto* tree = new QTreeView(workspaceDock);
        auto* model = new QFileSystemModel(tree);
        model->setReadOnly(true);
        model->setRootPath(directory);
        tree->setModel(model);
        tree->setRootIndex(model->index(directory));
        for (int column = 1; column < model->columnCount(); ++column) tree->hideColumn(column);
        workspaceDock->setWidget(tree);
        addDockWidget(Qt::LeftDockWidgetArea, workspaceDock);
        workspaceDock->show();
        connect(tree, &QTreeView::doubleClicked, this, [this, model](const QModelIndex& index) {
            if (!model->isDir(index)) guarded([&] { openFile(model->filePath(index)); });
        });
    }
    void updateMap() {
        if (!documentMap || !mapDock->isVisible() || tabs->count() == 0) return;
        auto* source = current().primary;
        const auto document = source->send(SCI_GETDOCPOINTER);
        if (document != mappedDocument) {
            documentMap->send(SCI_SETDOCPOINTER, 0, document);
            const auto fontLength = source->send(SCI_STYLEGETFONT, STYLE_DEFAULT);
            check(fontLength >= 0 && fontLength <= 4096, "Editor font metadata exceeds its limit.");
            std::string family(static_cast<std::size_t>(fontLength) + 1, '\0');
            source->sends(SCI_STYLEGETFONT, STYLE_DEFAULT, family.data());
            for (int style = 0; style < 256; ++style) {
                documentMap->send(SCI_STYLESETFORE, style, source->send(SCI_STYLEGETFORE, style));
                documentMap->send(SCI_STYLESETBACK, style, source->send(SCI_STYLEGETBACK, style));
                documentMap->sends(SCI_STYLESETFONT, style, family.c_str());
                documentMap->send(SCI_STYLESETSIZE, style, 3);
                documentMap->send(SCI_STYLESETCHECKMONOSPACED, style, true);
            }
            documentMap->send(SCI_SETMARGINWIDTHN, 0, 0);
            documentMap->send(SCI_SETMARGINWIDTHN, 1, 0);
            documentMap->send(SCI_SETMARGINWIDTHN, 2, 0);
            documentMap->send(SCI_SETHSCROLLBAR, false);
            documentMap->send(SCI_SETCARETWIDTH, 0);
            documentMap->send(SCI_HIDESELECTION, true);
            mappedDocument = document;
        }
    }
    void showFunctions() {
        const bool created = functionDock == nullptr;
        if (!functionDock) {
            functionDock = new QDockWidget("Function List", this);
            functionDock->setObjectName("function-list");
            auto* content = new QWidget(functionDock);
            auto* layout = new QVBoxLayout(content);
            layout->setContentsMargins(0, 0, 0, 0);
            auto* toolbar = new QToolBar(content);
            functionParser = new QComboBox;
            functionParser->addItem("Automatic", "");
            for (const auto& key : outline_keys()) {
                functionKeys.push_back(qs(key));
                functionParser->addItem(qs(key), qs(key));
            }
            check(!functionKeys.isEmpty(), "Embedded function-list definitions are missing.");
            toolbar->addWidget(functionParser);
            toolbar->addAction("Refresh", this, [this] { guarded([&] { requestFunctions(); }); });
            functionTree = new QTreeWidget;
            functionTree->setHeaderLabels({"Function / group", "Line"});
            functionTree->setUniformRowHeights(true);
            layout->addWidget(toolbar);
            layout->addWidget(functionTree);
            functionDock->setWidget(content);
            addDockWidget(Qt::RightDockWidgetArea, functionDock);
            functionTimer = new QTimer(this);
            functionTimer->setSingleShot(true);
            functionTimer->setInterval(400);
            connect(functionTimer, &QTimer::timeout, this, [this] { guarded([&] { requestFunctions(); }); });
            connect(functionParser, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
                if (tabs->count() == 0) return;
                current().functionKey = functionParser->currentData().toString();
                outlineDocument = 0;
                queueFunctions();
                recoveryPending = true;
            });
            connect(functionTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
                guarded([&] {
                    if (!item->data(0, Qt::UserRole).isValid()) return;
                    const auto id = item->data(0, Qt::UserRole + 1).toULongLong();
                    const auto revision = item->data(0, Qt::UserRole + 2).toULongLong();
                    if (current().id != id || document_revision(*controller, id) != revision) {
                        statusBar()->showMessage("Function list is stale; refreshing.");
                        outlineDocument = 0; queueFunctions(); return;
                    }
                    current().primary->send(SCI_GOTOPOS, item->data(0, Qt::UserRole).toULongLong());
                    current().primary->send(SCI_SCROLLCARET);
                    current().primary->setFocus();
                });
            });
            connect(functionDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
                commands.at("function_list")->setChecked(visible);
                if (visible) { outlineDocument = 0; queueFunctions(); }
                else {
                    functionTimer->stop();
                    if (outlineTask) outlineTask->cancel();
                }
            });
        } else functionDock->setVisible(!functionDock->isVisible());
        if (created) functionDock->show();
        commands.at("function_list")->setChecked(functionDock->isVisible());
        outlineDocument = 0;
        queueFunctions();
    }
    void queueFunctions() {
        if (!functionDock || !functionDock->isVisible() || tabs->count() == 0 || tearingDown) return;
        auto& view = current();
        const auto key = view.functionKey.isEmpty() ? view.languageId : view.functionKey;
        const auto revision = document_revision(*controller, view.id);
        if (outlineDocument == view.id && outlineRevision == revision && outlineParserKey == key) return;
        if (outlineTask) outlineTask->cancel();
        outlineDocument = view.id; outlineRevision = revision; outlineParserKey = key;
        {
            const QSignalBlocker blocked(functionParser);
            functionParser->setCurrentIndex(std::max(0, functionParser->findData(view.functionKey)));
        }
        functionTimer->start();
    }
    void requestFunctions() {
        if (!functionDock || !functionDock->isVisible() || tabs->count() == 0) return;
        functionTimer->stop();
        if (outlineTask) outlineTask->cancel();
        const auto id = current().id;
        const auto revision = document_revision(*controller, id);
        const auto key = current().functionKey.isEmpty() ? current().languageId : current().functionKey;
        functionTree->clear();
        if (!functionKeys.contains(key)) {
            new QTreeWidgetItem(functionTree, {"No parser for this language; choose a definition above.", ""});
            return;
        }
        if (current().primary->send(SCI_GETLENGTH) > 4 * 1024 * 1024) {
            new QTreeWidgetItem(functionTree, {"Function List supports documents up to 4 MiB.", ""});
            return;
        }
        const auto source = text(current().primary);
        const auto parserBytes = key.toUtf8();
        const auto request = qs(outline_request(rs(source), rs(parserBytes))).toUtf8();
        auto* task = new SearchTask(this);
        outlineTask = task;
        outlineError.clear();
        task->start(request, [this, task, id, revision, key, source](QByteArray response) {
            outlineTask = nullptr; task->deleteLater();
            guarded([&] {
                if (tabs->count() == 0 || current().id != id || document_revision(*controller, id) != revision ||
                    (current().functionKey.isEmpty() ? current().languageId : current().functionKey) != key) return;
                const auto symbols = outline_response(rs(response), rs(source));
                functionTree->clear();
                QMap<QString, QTreeWidgetItem*> groups;
                for (const auto& symbol : symbols) {
                    QTreeWidgetItem* parent = functionTree->invisibleRootItem();
                    if (!symbol.group.empty()) {
                        const auto groupKey = qs(symbol.group) + ":" + QString::number(symbol.group_start);
                        if (!groups.contains(groupKey)) {
                            auto* group = new QTreeWidgetItem(functionTree, {qs(symbol.group), ""});
                            if (symbol.group_start >= 0) group->setData(0, Qt::UserRole, static_cast<qulonglong>(symbol.group_start));
                            group->setData(0, Qt::UserRole + 1, static_cast<qulonglong>(id));
                            group->setData(0, Qt::UserRole + 2, static_cast<qulonglong>(revision));
                            group->setExpanded(true);
                            groups.insert(groupKey, group);
                        }
                        parent = groups.value(groupKey);
                    }
                    auto* item = new QTreeWidgetItem(parent, {qs(symbol.name), QString::number(symbol.line + 1)});
                    item->setData(0, Qt::UserRole, static_cast<qulonglong>(symbol.start));
                    item->setData(0, Qt::UserRole + 1, static_cast<qulonglong>(id));
                    item->setData(0, Qt::UserRole + 2, static_cast<qulonglong>(revision));
                }
                if (symbols.empty()) new QTreeWidgetItem(functionTree, {"No functions found.", ""});
            });
        }, [this, task](const QString& error) {
            outlineTask = nullptr; task->deleteLater(); outlineError = error;
            functionTree->clear();
            new QTreeWidgetItem(functionTree, {error, ""});
        }, "--outline-worker", 8 * 1024 * 1024);
    }
    void showProjectPanel(int index) {
        check(index >= 0 && index < 3, "Invalid project panel.");
        auto*& panel = projectPanels[static_cast<std::size_t>(index)];
        if (!panel) {
            panel = new ProjectPanel(QString("Project Panel %1").arg(index + 1), this);
            panel->setObjectName(QString("project-panel-%1").arg(index + 1));
            panel->openFile = [this](const QString& path) { guarded([&] { openFile(path); }); };
            panel->canSave = [this](const QString& path) { return disk_updated_documents(*controller, filePath(path)).empty(); };
            addDockWidget(Qt::LeftDockWidgetArea, panel);
            panel->show();
            const auto action = QString("project_panel%1").arg(index + 1);
            connect(panel, &QDockWidget::visibilityChanged, this, [this, action](bool visible) { commands.at(action)->setChecked(visible); });
        } else panel->setVisible(!panel->isVisible());
        commands.at(QString("project_panel%1").arg(index + 1))->setChecked(panel->isVisible());
    }
    void showDocumentMap() {
        if (!mapDock) {
            mapDock = new QDockWidget("Document Map", this);
            mapDock->setObjectName("document-map");
            documentMap = new DocumentMapView(mapDock);
            documentMap->navigate = [this](sptr_t position) {
                guarded([&] { current().primary->send(SCI_GOTOPOS, position); current().primary->setFocus(); });
            };
            mapDock->setWidget(documentMap);
            connect(mapDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
                if (tearingDown) return;
                if (visible) { mappedDocument = 0; updateMap(); }
                else {
                    documentMap->send(SCI_SETDOCPOINTER, 0, 0);
                    mappedDocument = 0;
                }
            });
            addDockWidget(Qt::RightDockWidgetArea, mapDock);
            resizeDocks({mapDock}, {160}, Qt::Horizontal);
            updateMap();
            mapDock->show();
        } else mapDock->setVisible(!mapDock->isVisible());
        commands.at("document_map")->setChecked(mapDock->isVisible());
    }
    void renameView(DocumentView& view, const QString& name) {
        const auto oldPath = pathText(document_path(*controller, view.id));
        check(!oldPath.isEmpty(), "Save the untitled document before renaming its file.");
        check(!document_changed_on_disk(*controller, view.id), "File changed on disk. Reload or Save As before renaming.");
        check(!name.isEmpty() && name != "." && name != ".." && !name.contains('/') && !name.contains('\\') && !name.contains(':'),
            "Use a filename without path separators or a colon.");
        const auto target = QFileInfo(oldPath).dir().filePath(name);
        if (target == oldPath) return;
        for (const auto other : disk_updated_documents(*controller, filePath(target))) check(other == view.id, "The destination is open in another tab.");
        QFile file(oldPath);
        if (!file.rename(target)) throw std::runtime_error(file.errorString().toStdString());
        document_renamed(*controller, view.id, filePath(target));
        if (view.udlXml.isEmpty()) applyLexer(view, detectedLexer(target));
        view.diskChanged = document_changed_on_disk(*controller, view.id);
        refresh(); refreshWatches(); checkpointNow();
        rememberFile(filePath(target), document_saved_encoding(*controller, view.id), filePath(oldPath));
    }
    void dispatch(const QString& id) {
        check(!macroPlaybackActive, "Cancel or finish macro playback before using another command.");
        const auto bytes = id.toUtf8();
        validate_command(*controller, rust::Str(bytes.constData(), bytes.size()));
        static const QSet<QString> recordingActions{"macro_start", "macro_stop", "macro_play", "macro_save",
            "macro_load", "import_notepad_macro", "select_all", "duplicate_line", "duplicate_selection", "delete_line"};
        check(!macroRecording || recordingActions.contains(id),
            "Stop macro recording before using this command. It cannot be captured by the basic-edit format.");
        if (id == "json_preview") { openJson(JsonView::Tree); return; }
        if (id == "json_graph") { openJson(JsonView::Graph); return; }
        if (id == "json_inspector") {
            if (jsonDock && !jsonDock->isHidden()) jsonDock->hide();
            else openJson(jsonPanel ? jsonPanel->view() : JsonView::Tree);
            return;
        }
        if (id == "json_view_tree" || id == "json_view_graph" || id == "json_view_pretty") {
            openJson(id == "json_view_graph" ? JsonView::Graph :
                id == "json_view_pretty" ? JsonView::Pretty : JsonView::Tree);
            return;
        }
        if (id == "toggle_comparison") {
            if (comparisonEnabled) stopComparison();
            else chooseComparison();
            return;
        }
        if (id == "compare_right") { beginComparison(current().id); return; }
        if (id == "compare_tabs") { chooseComparison(); return; }
        if (id == "stop_comparison") { stopComparison(); return; }
        if (id == "comparison_settings") { comparisonOptions(); return; }
        if (id == "next_difference" || id == "previous_difference") { navigateComparison(id == "next_difference" ? 1 : -1); return; }
        if (id == "swap_comparison") { swapComparedTabs(); return; }
        if (id == "move_other_view" || id == "move_left_view" || id == "move_right_view") {
            moveDocumentToGroup(current().id, id == "move_left_view" ? 0 : id == "move_right_view" ? 1 : 1 - tabs->activeGroup());
            return;
        }
        if (id == "move_all_other_view") { moveAllToOtherGroup(); return; }
        if (id == "find_all_current" || id == "find_all_open") { startBufferSearch(id == "find_all_open", false); return; }
        if (id == "search_count") { startBufferSearch(false, true); return; }
        if (id == "toggle_line_comment" || id == "add_line_comment" || id == "remove_line_comment" ||
            id == "block_comment" || id == "block_uncomment") { commentSelection(id); return; }
        if (id == "matching_brace" || id == "select_braces") { matchingBrace(id == "select_braces"); return; }
        if (id == "duplicate_line" || id == "duplicate_selection") { duplicateText(id == "duplicate_selection"); return; }
        if (id == "document_summary") { documentSummary(); return; }
        if (id == "date_time_short" || id == "date_time_long" || id == "date_time_custom") { insertDateTime(id); return; }
        if (id == "paste_image" || (id == "paste" && clipboardHoldsOnlyImage())) { pasteImage(); return; }
        if (id == "join_lines" || id == "split_lines" || id == "blank_line_above" || id == "blank_line_below") {
            lineLayout(id); return;
        }
        if (id == "copy_bookmarks" || id == "cut_bookmarks" || id == "paste_bookmarks" ||
            id == "delete_bookmarks" || id == "delete_unmarked" || id == "invert_bookmarks") { bookmarkOperation(id); return; }
        if (id == "column_editor") { columnEditor(); return; }
        if (id == "proper_case" || id == "proper_blend" || id == "sentence_case" || id == "sentence_blend" ||
            id == "invert_case" || id == "reverse_lines" || id == "remove_consecutive_duplicates" ||
            id == "remove_empty_lines" || id == "remove_blank_lines" || id == "trim_leading" || id == "trim_both" ||
            id == "tabs_to_spaces" || id == "leading_spaces_to_tabs" || id == "numeric_sort") { advancedTransform(id); return; }
        if (id == "rename_file") {
            auto& view = current();
            const auto oldPath = pathText(document_path(*controller, view.id));
            check(!oldPath.isEmpty(), "Save the untitled document before renaming its file.");
            check(!document_changed_on_disk(*controller, view.id), "File changed on disk. Reload or Save As before renaming.");
            bool accepted = false;
            const auto name = QInputDialog::getText(this, "Rename File", "New filename", QLineEdit::Normal, QFileInfo(oldPath).fileName(), &accepted);
            if (!accepted) return;
            renameView(view, name); return;
        }
        if (id == "trash_file") {
            const auto document = current().id;
            const auto path = pathText(document_path(*controller, document));
            check(!path.isEmpty(), "This document has no disk file.");
            check(!document_changed_on_disk(*controller, document), "File changed on disk. Reload before moving it to Trash.");
            QMessageBox confirm(this);
            confirm.setTextFormat(Qt::PlainText);
            confirm.setWindowTitle("Move File to Trash");
            confirm.setText("Move this file to the operating system Trash/Recycle Bin and close its tab?\n\n" + path +
                (document_dirty(*controller, document) ? "\n\nUnsaved edits in this tab will be discarded." : ""));
            confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
            confirm.setDefaultButton(QMessageBox::Cancel);
            if (confirm.exec() != QMessageBox::Yes) return;
            check(!document_changed_on_disk(*controller, document), "File changed while confirmation was open; it was not moved.");
            check(QFile::moveToTrash(path), "The operating system could not move this file to Trash. No permanent-delete fallback was attempted.");
            for (const auto& view : views) if (view.id == document) { closeTab(tabs->indexOf(view.page), true, true, false); break; }
            try {
                forget_recent(*controller, filePath(path));
                options = QJsonDocument::fromJson(qs(current_settings(*controller)).toUtf8()).object();
            } catch (const std::exception& error) { historyWarning(QString::fromUtf8(error.what())); }
            return;
        }
        if (id == "reveal_folder") {
            const auto path = pathText(document_path(*controller, current().id));
            check(!path.isEmpty(), "This document has no containing folder.");
            check(QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())), "Cannot open the containing folder.");
            return;
        }
        if (id == "full_screen") {
            if (isFullScreen()) showNormal(); else showFullScreen();
            commands.at(id)->setChecked(isFullScreen()); return;
        }
        if (id == "always_on_top") {
            const bool enabled = !(windowFlags() & Qt::WindowStaysOnTopHint);
            setWindowFlag(Qt::WindowStaysOnTopHint, enabled); show();
            commands.at(id)->setChecked(enabled); return;
        }
        if (id == "distraction_free") {
            distraction = !distraction;
            if (distraction) {
                hiddenChrome.clear();
                for (auto* toolbar : findChildren<QToolBar*>()) if (toolbar->isVisible()) hiddenChrome.push_back(toolbar);
                for (auto* panel : findChildren<QDockWidget*>()) if (panel->isVisible()) hiddenChrome.push_back(panel);
                for (auto* chrome : {static_cast<QWidget*>(menuBar()), static_cast<QWidget*>(statusBar()),
                    static_cast<QWidget*>(tabs->bar(0)), static_cast<QWidget*>(tabs->bar(1))})
                    if (chrome->isVisible()) hiddenChrome.push_back(chrome);
                for (const auto& chrome : hiddenChrome) if (chrome) chrome->hide();
            } else {
                for (const auto& chrome : hiddenChrome) if (chrome) chrome->show();
                hiddenChrome.clear();
            }
            commands.at(id)->setChecked(distraction); return;
        }
        if (id == "focus_other_view") {
            const auto other = 1 - tabs->activeGroup();
            if (auto* view = viewInGroup(other)) {
                tabs->setCurrentWidget(view->page);
                view->focused->setFocus();
                return;
            }
            auto& view = current();
            if (!view.clone->isVisible()) { view.clone->show(); commands.at("clone_view")->setChecked(true); }
            (activeEditor() == view.primary ? view.clone : view.primary)->setFocus(); return;
        }
        if (id == "zoom_sync") { synchronizeZoom = !synchronizeZoom; return; }
        if (id == "pin_tab") {
            const auto state = document_tab(*controller, current().id);
            set_document_tab(*controller, current().id, !state.pinned, state.color);
            normalizePinnedOrder(); refresh(); recoveryPending = true; return;
        }
        if (id == "tab_color") {
            const auto state = document_tab(*controller, current().id);
            bool accepted = false;
            const int color = QInputDialog::getInt(this, "Tab Text Color", "0=default, 1=red, 2=green, 3=blue, 4=purple, 5=amber",
                state.color, 0, 5, 1, &accepted);
            if (accepted) { set_document_tab(*controller, current().id, state.pinned, static_cast<std::uint8_t>(color)); refresh(); recoveryPending = true; }
            return;
        }
        if (id.startsWith("move_tab_")) { moveTab(id); return; }
        if (id == "sort_tabs") { sortTabs(); return; }
        if (id == "close_others" || id == "close_left" || id == "close_right" || id == "close_unchanged" || id == "close_unpinned") {
            closeCollection(id); return;
        }
        if (id == "function_list") { showFunctions(); return; }
        if (id == "monitoring") {
            auto& view = current();
            check(!pathText(document_path(*controller, view.id)).isEmpty(), "Save this document before monitoring it.");
            check(!document_dirty(*controller, view.id), "Save changes before enabling monitoring.");
            view.monitoring = !view.monitoring;
            if (view.monitoring) {
                view.previousReadOnly = view.primary->send(SCI_GETREADONLY) != 0;
                view.primary->send(SCI_SETREADONLY, true);
                view.primary->send(SCI_DOCUMENTEND);
            } else view.primary->send(SCI_SETREADONLY, view.previousReadOnly);
            refreshWatches(); refresh(); return;
        }
        if (id.startsWith("project_panel")) { showProjectPanel(id.right(1).toInt() - 1); return; }
        if (id == "document_map") { showDocumentMap(); return; }
        if (id == "sync_vertical") { synchronizeVertical = !synchronizeVertical; return; }
        if (id == "sync_horizontal") { synchronizeHorizontal = !synchronizeHorizontal; return; }
        if (id == "export_html" || id == "copy_html" || id == "copy_rtf") { styledExport(id); return; }
        if (id == "replace_in_files") { previewDiskReplace(); return; }
        if (id == "restore_disk_backup") { restoreDiskBackup(); return; }
        if (id == "select_language") {
            QStringList names;
            for (const auto& language : catalog->languages()) names.push_back(language.displayName);
            bool accepted = false;
            const auto selected = QInputDialog::getItem(this, "Language", "Language profile", names, 0, false, &accepted);
            if (accepted) for (const auto& language : catalog->languages())
                if (language.displayName == selected) { applyLexer(current(), language.id.toUtf8()); break; }
            recoveryPending = true;
            return;
        }
        if (id == "import_udl") {
            const auto path = QFileDialog::getOpenFileName(this, "Import UDL 2.1", {}, "Notepad++ UDL (*.xml)");
            if (path.isEmpty()) return;
            QFile file(path);
            check(file.open(QIODevice::ReadOnly) && file.size() <= 8 * 1024 * 1024, "Cannot read UDL file or it exceeds 8 MiB.");
            const auto xml = file.readAll();
            applyUdl(current(), xml);
            refresh();
            checkpointNow();
            return;
        }
        if (id == "export_udl") {
            check(!current().udlXml.isEmpty(), "The active document has no imported UDL profile.");
            const auto path = QFileDialog::getSaveFileName(this, "Export UDL", {}, "Notepad++ UDL (*.xml)");
            if (path.isEmpty()) return;
            QSaveFile file(path);
            check(file.open(QIODevice::WriteOnly) && file.write(current().udlXml) == current().udlXml.size() && file.commit(),
                "Could not export UDL.");
            return;
        }
        if (id == "complete_word") { completeWord(activeEditor(), true); return; }
        if (id == "calltip") { showCalltip(activeEditor(), true); return; }
        if (id == "run_extension") { runLocalExtension(); return; }
        if (id == "manage_extensions") {
            if (!extensionDock) ensureExtensionManager();
            else extensionDock->setVisible(!extensionDock->isVisible());
            return;
        }
        if (id == "create_example_extension") {
            const auto parent = QFileDialog::getExistingDirectory(this, "Choose a parent folder for the example extension");
            if (!parent.isEmpty()) {
                create_extension_example(filePath(QDir(parent).filePath("uppercase-extension")));
                QMessageBox::information(this, "Example created", "The uppercase-extension package is ready. Select text, then choose Run Local Wasm Extension.");
            }
            return;
        }
        if (id == "macro_start") {
            check(composingEditors.isEmpty(), "Finish input-method composition before recording a macro.");
            check(!activeSearch && !fileSearchRunning, "Finish or cancel background editing operations before recording a macro.");
            check(activeEditor()->send(SCI_GETSELECTIONS) == 1 && !activeEditor()->send(SCI_SELECTIONISRECTANGLE),
                "Basic macro recording requires one stream selection.");
            stopMacro(); macroSteps = QJsonArray(); macroRecording = true; macroDocument = current().id;
            macroName = "Recorded macro";
            current().primary->send(SCI_STARTRECORD); current().clone->send(SCI_STARTRECORD);
            statusBar()->showMessage("Recording basic text edits in this document."); return;
        }
        if (id == "macro_stop") { stopMacro(); statusBar()->showMessage("Macro recording stopped."); return; }
        if (id == "macro_play") { playMacro(); return; }
        if (id == "macro_repeat") {
            bool accepted = false;
            const auto document = current().id;
            const auto revision = document_revision(*controller, document);
            const auto count = QInputDialog::getInt(this, "Repeat Macro",
                "Repetitions (maximum 100,000 total steps; five-second playback budget)", 1, 1, 1000, 1, &accepted);
            if (!accepted) return;
            check(current().id == document && document_revision(*controller, document) == revision,
                "The document changed while repeat options were open.");
            playMacro(static_cast<std::uint32_t>(count)); return;
        }
        if (id == "macro_save") {
            stopMacro();
            const auto encoded = macroJson();
            validate_macro(rs(encoded));
            const auto path = QFileDialog::getSaveFileName(this, "Save Macro", {}, "Notepad Star Macro (*.json)");
            if (path.isEmpty()) return;
            QSaveFile file(path);
            check(file.open(QIODevice::WriteOnly) && file.write(encoded) == encoded.size() && file.commit(), "Cannot save macro.");
            return;
        }
        if (id == "macro_load") {
            const auto path = QFileDialog::getOpenFileName(this, "Load Macro", {}, "Notepad Star Macro (*.json)");
            if (path.isEmpty()) return;
            QFile file(path);
            check(file.open(QIODevice::ReadOnly) && file.size() <= 2 * 1024 * 1024, "Cannot read macro or macro exceeds limit.");
            const auto encoded = file.read(2 * 1024 * 1024 + 1);
            check(file.error() == QFileDevice::NoError && encoded.size() <= 2 * 1024 * 1024, "Cannot read bounded macro.");
            validate_macro(rs(encoded));
            const auto loaded = QJsonDocument::fromJson(encoded).object();
            stopMacro(); macroName = loaded["name"].toString(); macroSteps = loaded["steps"].toArray();
            statusBar()->showMessage("Macro loaded; it will run only when Play is selected."); return;
        }
        if (id == "import_notepad_macro") { importMacroDialog(); return; }
        if (id == "print" || id == "print_preview") { printDocument(id == "print_preview"); return; }
        if (id == "run_program") { runProgram(); return; }
        if (id == "hash" || id == "base64_encode" || id == "base64_decode" || id == "url_encode" ||
            id == "url_decode" || id == "json_format" || id == "json_minify") { utility(id); return; }
        if (id == "cancel_search") {
            bufferSearchRunning = false; bufferSearchQueue.clear();
            fileSearchRunning = false; fileQueue.clear(); replacementPreview = false;
            diskProposals = rust::Vec<DiskProposal>();
            if (activeSearch) activeSearch->cancel();
            return;
        }
        if (id == "preferences") { preferencesDialog(); return; }
        if (id == "import_preferences") { importPreferencesDialog(); return; }
        if (id == "reset_preferences") { resetPreferencesDialog(); return; }
        if (id == "shortcut_mapper") { shortcutsDialog(); return; }
        if (id == "find_in_files") { findInFiles(); return; }
        if (id == "new") { addDocument(false); return; }
        if (id == "open_recent") { populateRecentMenu(); recentMenu->popup(QCursor::pos()); return; }
        if (id == "open_all_recent") { openAllRecent(); return; }
        if (id == "clear_recent") {
            forget_recent(*controller, FilePath{});
            options = QJsonDocument::fromJson(qs(current_settings(*controller)).toUtf8()).object();
            closedFiles.clear();
            return;
        }
        if (id == "reopen_closed") {
            check(!closedFiles.empty(), "There is no closed named file to reopen.");
            const auto entry = closedFiles.back();
            closedFiles.pop_back();
            openNativeFile(entry.path, qs(entry.encoding).toUtf8());
            return;
        }
        if (id == "open_byte_preview") {
            const auto path = QFileDialog::getOpenFileName(this, "Open Read-only Byte Preview");
            if (!path.isEmpty()) {
                auto* viewer = new LargeFileWindow(path, this);
                viewer->show();
            }
            return;
        }
        if (id == "open" || id == "open_encoding") {
            const auto encoding = id == "open_encoding" ? chooseEncoding().toUtf8() : QByteArray();
            if (id == "open_encoding" && encoding.isEmpty()) return;
            for (const auto& file : QFileDialog::getOpenFileNames(this, "Open files"))
                guarded([&] { openFile(file, encoding); });
            return;
        }
        if (id == "save" || id == "save_as" || id == "save_copy") {
            saveView(current(), id == "save_as", id == "save_copy"); return;
        }
        if (id == "save_all") {
            for (auto& view : views) if (document_dirty(*controller, view.id) && !saveView(view, false)) return;
            return;
        }
        if (id == "close_all") {
            closeCollection(id);
            return;
        }
        if (id == "reload") {
            if (document_dirty(*controller, current().id) && QMessageBox::question(this, "Reload file",
                "Discard in-memory changes and reload from disk?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
            const auto loaded = reload_document(*controller, current().id);
            applyReload(current(), loaded);
            refresh(); checkpointNow(); return;
        }
        if (id == "save_session") {
            const auto path = QFileDialog::getSaveFileName(this, "Save Session", {}, "Notepad Star Session (*.json)");
            if (!path.isEmpty()) save_session(*controller, snapshots(), filePath(path), dark, wrap);
            return;
        }
        if (id == "load_session") {
            const auto path = QFileDialog::getOpenFileName(this, "Load Session", {}, "Notepad Star Session (*.json)");
            if (!path.isEmpty()) {
                const bool blank = views.size() == 1 && !document_dirty(*controller, current().id) &&
                    pathText(document_path(*controller, current().id)).isEmpty() && current().primary->send(SCI_GETLENGTH) == 0;
                if (blank) closeTab(0, true, false);
                try {
                    restoreViews(load_session(*controller, filePath(path)));
                    if (views.empty()) addDocument(false);
                } catch (...) {
                    if (views.empty()) addDocument(false);
                    throw;
                }
            }
            return;
        }
        if (id == "recover") { restoreViews(restore_recovery(*controller)); return; }
        if (id == "workspace") { showWorkspace(); return; }
        if (id == "close") { closeTab(tabs->currentIndex()); return; }
        if (id == "exit") { close(); return; }
        auto* editor = activeEditor();
        const std::map<QString, unsigned int> messages{
            {"undo", SCI_UNDO}, {"redo", SCI_REDO}, {"cut", SCI_CUT}, {"copy", SCI_COPY},
            {"paste", SCI_PASTE}, {"select_all", SCI_SELECTALL}, {"zoom_in", SCI_ZOOMIN},
            {"zoom_out", SCI_ZOOMOUT}, {"zoom_reset", SCI_SETZOOM},
            {"delete_line", SCI_LINEDELETE}, {"move_line_up", SCI_MOVESELECTEDLINESUP}, {"move_line_down", SCI_MOVESELECTEDLINESDOWN}};
        if (const auto it = messages.find(id); it != messages.end()) { editor->send(it->second); return; }
        if (id == "find" || id == "replace") { openSearch(id == "replace"); }
        else if (id == "replace_all") replace(true);
        else if (id == "next_tab" || id == "previous_tab") {
            const int group = tabs->activeGroup();
            const int start = tabs->groupStart(group);
            const int count = tabs->groupCount(group);
            tabs->setCurrentIndex(start + (tabs->currentIndex() - start + (id == "next_tab" ? 1 : count - 1)) % count);
        } else if (id == "go_to_line") {
            bool accepted = false;
            const auto line = QInputDialog::getInt(this, "Go to Line", "Line", 1, 1,
                static_cast<int>(editor->send(SCI_GETLINECOUNT)), 1, &accepted);
            if (accepted) editor->send(SCI_GOTOLINE, line - 1);
        }
        else if (id == "uppercase" || id == "lowercase" || id == "trim_trailing" ||
            id == "sort_ascending" || id == "sort_descending" || id == "remove_duplicates") transform(id);
        else if (id == "fold_all" || id == "unfold_all")
            editor->send(SCI_FOLDALL, id == "fold_all" ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND);
        else if (id == "show_whitespace") {
            const auto show = editor->send(SCI_GETVIEWWS) == SCWS_INVISIBLE;
            auto updated = options;
            auto display = updated.value("view").toObject();
            display["show_whitespace"] = show; display["show_eol"] = show;
            updated["view"] = display;
            const auto encoded = QJsonDocument(updated).toJson(QJsonDocument::Compact);
            store_settings(*controller, rs(encoded));
            options = updated; applyOptions();
        }
        else if (id == "read_only") {
            const bool value = editor->send(SCI_GETREADONLY) == 0;
            current().primary->send(SCI_SETREADONLY, value);
            current().clone->send(SCI_SETREADONLY, value);
        }
        else if (id == "encoding") {
            const auto selected = chooseEncoding().toUtf8();
            if (!selected.isEmpty()) {
                const auto bytesText = text(editor);
                change_encoding(*controller, current().id, rs(bytesText), rs(selected));
                recoveryPending = true; refresh();
            }
        }
        else if (id.startsWith("eol_")) {
            const auto ending = id == "eol_cr_lf" ? SC_EOL_CRLF : id == "eol_lf" ? SC_EOL_LF : SC_EOL_CR;
            editor->send(SCI_CONVERTEOLS, ending);
            editor->send(SCI_SETEOLMODE, ending);
        }
        else if (id == "toggle_bookmark") {
            const auto line = editor->send(SCI_LINEFROMPOSITION, editor->send(SCI_GETCURRENTPOS));
            if (editor->send(SCI_MARKERGET, line) & 1) editor->send(SCI_MARKERDELETE, line, 0);
            else editor->send(SCI_MARKERADD, line, 0);
            recoveryPending = true;
        }
        else if (id == "clear_bookmarks") { editor->send(SCI_MARKERDELETEALL, 0); recoveryPending = true; }
        else if (id == "next_bookmark" || id == "previous_bookmark") {
            const auto line = editor->send(SCI_LINEFROMPOSITION, editor->send(SCI_GETCURRENTPOS));
            const bool next = id == "next_bookmark";
            auto found = editor->send(next ? SCI_MARKERNEXT : SCI_MARKERPREVIOUS, line + (next ? 1 : -1), 1);
            if (found < 0) found = editor->send(next ? SCI_MARKERNEXT : SCI_MARKERPREVIOUS,
                next ? 0 : editor->send(SCI_GETLINECOUNT) - 1, 1);
            if (found >= 0) editor->send(SCI_GOTOLINE, found);
        }
        else if (id == "find_next") find(false);
        else if (id == "find_previous") find(true);
        else if (id == "clone_view") {
            auto& view = current();
            view.clone->setVisible(!view.clone->isVisible());
            commands.at(id)->setChecked(view.clone->isVisible());
            view.page->setSizes({width() / 2, width() / 2});
        } else if (id == "document_list") {
            dock->setVisible(dock->isHidden());
            if (!dock->isHidden()) dock->raise();
        } else if (id == "word_wrap") {
            check(!comparisonEnabled || !comparisonAligned, "Disable aligned comparison in Comparison Options before changing word wrap.");
            wrap = !wrap;
            for (const auto& view : views)
                for (auto* pane : {view.primary, view.clone}) pane->send(SCI_SETWRAPMODE, wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
            commands.at(id)->setChecked(wrap);
            persistFlags();
        }         else if (id == "dark_theme") {
            dark = !dark;
            applyPalette();
            for (auto& view : views) restyle(view);
            mappedDocument = 0; updateMap();
            comparisonCacheValid = false;
            jsonDocument = 0;
            queueToolsRefresh();
            commands.at(id)->setChecked(dark);
            persistFlags();
        } else if (id == "about") {
            QDialog dialog(this);
            dialog.setWindowTitle("About Notepad Star");
            auto* layout = new QVBoxLayout(&dialog);
            auto* logo = new QLabel(&dialog);
            logo->setObjectName("brand-wordmark");
            logo->setAccessibleName("Notepad Star logo");
            logo->setAlignment(Qt::AlignCenter);
            const QPixmap wordmark(dark ? ":/notepad-star/brand/notepad-star-wordmark-dark.png" :
                ":/notepad-star/brand/notepad-star-wordmark.png");
            check(!wordmark.isNull(), "The application wordmark is unavailable.");
            auto displayWordmark = wordmark.scaledToWidth(qRound(460 * logo->devicePixelRatioF()), Qt::SmoothTransformation);
            displayWordmark.setDevicePixelRatio(logo->devicePixelRatioF());
            logo->setPixmap(displayWordmark);
            layout->addWidget(logo);
            auto* details = new QLabel(&dialog);
            details->setTextFormat(Qt::PlainText);
            details->setWordWrap(true);
            details->setMaximumWidth(460);
            details->setText("Notepad Star " + qs(application_version()) + "\n" +
                (development_build() ? "Development build\n\n" : "Release candidate\n\n") +
                "Rust + Qt Widgets + Scintilla/Lexilla\n"
                "See release notes for supported features, platform qualification and limits.\n"
                "Independent from Notepad++; native DLL plugins are not loaded.\n"
                "GPL-3.0-or-later. See packaged third-party license notices.");
            layout->addWidget(details);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            layout->addWidget(buttons);
            dialog.exec();
        } else throw std::runtime_error("Registered command has no native adapter.");
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (macroPlaybackActive && qobject_cast<ScintillaEditBase*>(watched)) {
            switch (event->type()) {
            case QEvent::KeyPress: case QEvent::KeyRelease: case QEvent::InputMethod:
            case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick:
            case QEvent::MouseMove: case QEvent::Wheel: case QEvent::ContextMenu:
            case QEvent::DragEnter: case QEvent::DragMove: case QEvent::Drop:
                return true;
            default: break;
            }
        }
        if (event->type() == QEvent::InputMethod) {
            if (auto* pane = qobject_cast<ScintillaEditBase*>(watched)) {
                const auto* input = static_cast<QInputMethodEvent*>(event);
                if (input->preeditString().isEmpty()) composingEditors.remove(pane);
                else {
                    composingEditors.insert(pane);
                    pane->send(SCI_AUTOCCANCEL);
                    pane->send(SCI_CALLTIPCANCEL);
                }
            }
        }
        return QMainWindow::eventFilter(watched, event);
    }
    void closeEvent(QCloseEvent* event) override {
        if (macroPlaybackActive) {
            if (macroPlaybackDialog) macroPlaybackDialog->reject();
            event->ignore();
            return;
        }
        if (!isEnabled()) { event->ignore(); return; }
        bool allow = false;
        guarded([&] {
            check(!managementTask, "An extension package operation is still running. Wait for it to finish before closing.");
            if (runningProgram) {
                if (QMessageBox::question(this, "Program still running", "Terminate the program launched by this editor and close?",
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
            }
            for (auto* panel : projectPanels) if (panel && !panel->confirmClose()) return;
            const bool remember = !noSession && options.value("restore_session").toBool(true);
            if (remember) {
                check(settingsReady && recoveryReady,
                    "Session storage is unavailable. Save your work explicitly or repair the reported profile error before closing.");
                retain_workspace(*controller, snapshots(), dark, wrap);
            } else {
                for (const auto& view : views)
                    if (!confirmDiscard(view)) return;
                finish_recovery(*controller);
            }
            recoveryTimer->stop();
            search->hide();
            bufferSearchRunning = false; bufferSearchQueue.clear();
            fileSearchRunning = false; fileQueue.clear();
            if (activeSearch) delete activeSearch.data();
            if (outlineTask) delete outlineTask.data();
            if (runningProgram) {
                auto* process = runningProgram.data();
                process->kill();
                process->waitForFinished(2000);
            }
            closingWindow = true;
            allow = true;
        });
        if (allow) event->accept();
        else event->ignore();
    }
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        if (macroPlaybackActive) {
            qWarning("Files were not opened during macro playback; drop them again after playback finishes.");
            event->ignore();
            return;
        }
        for (const auto& url : event->mimeData()->urls())
            if (url.isLocalFile()) guarded([&] { openFile(url.toLocalFile()); });
        event->acceptProposedAction();
    }
};
}

std::int32_t run_desktop(const LaunchSettings& settings, rust::Vec<FilePath> files) {
    int argc = 1;
    char name[] = "notepad-star";
    char* argv[] = {name, nullptr};
    EditorApplication application(argc, argv);
    initializeApplicationResources();
    // Keep the existing storage identity so upgrades retain profiles and recovery.
    QApplication::setApplicationName("Notepad Star Rust Preview");
    QApplication::setApplicationDisplayName("Notepad Star");
    QApplication::setOrganizationName("NotepadStar");
    QApplication::setApplicationVersion(qs(application_version()));
    QApplication::setWindowIcon(QIcon(":/notepad-star/notepad-star.png"));
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    std::unique_ptr<InstanceChannel> channel;
    if (settings.reuse_instance) {
        const auto request = qs(forward_request(settings, rust::Slice<const FilePath>(files.data(), files.size()))).toUtf8();
        auto decoded = decode_forward_request(rs(request));
        files = std::move(decoded.files);
        const auto profile = settings.profile.windows.empty() && settings.profile.unix.empty() ?
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : pathText(settings.profile);
        channel = std::make_unique<InstanceChannel>(profile);
        if (!channel->primary()) {
            const auto response = InstanceChannel::exchange(channel->endpoint(), request);
            accept_forward_response(rs(response));
            return 0;
        }
    }
    Shell shell(settings, files);
    const auto closeChannel = qScopeGuard([&channel, &application] { channel.reset(); application.clearFileHandler(); });
    if (channel) channel->setHandler([&shell](const QByteArray& request) { return shell.acceptForwarded(request); });
    if (!settings.smoke_test && !settings.preview && shell.startMaximized()) shell.showMaximized();
    else shell.show();
    application.setFileHandler([&shell](const QString& path) { shell.openFromOperatingSystem(path); });
    int testResult = 0;
    std::string testError;
    const auto capture = qs(settings.screenshot);
    if (settings.smoke_test) {
        QTimer::singleShot(0, &shell, [&] {
            try {
                shell.smokeTest();
            } catch (const std::exception& error) {
                testError = error.what();
                testResult = 1;
            }
            application.exit(testResult);
        });
    } else if (!capture.isEmpty()) {
        QTimer::singleShot(500, &shell, [&shell, capture] {
            if (!shell.grab().save(capture)) qCritical("Could not save requested preview screenshot.");
        });
    }
    const int result = application.exec();
    if (!testError.empty()) throw std::runtime_error("Native integration checks failed: " + testError);
    return result;
}
}
