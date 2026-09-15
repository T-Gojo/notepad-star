#include "search_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTabBar>
#include <QVBoxLayout>

namespace star {
SearchDialog::SearchDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("find-replace-dialog");
    setModal(false);
    resize(660, 420);
    auto* outer = new QVBoxLayout(this);
    tabs = new QTabBar(this);
    tabs->addTab("Find");
    tabs->addTab("Replace");
    outer->addWidget(tabs);
    auto* columns = new QHBoxLayout;
    auto* fields = new QVBoxLayout;
    auto* form = new QFormLayout;
    findText = new QLineEdit;
    findText->setObjectName("find-text");
    findText->setMaxLength(4096);
    replacementText = new QLineEdit;
    replacementText->setObjectName("replace-text");
    replacementText->setMaxLength(65536);
    form->addRow("&Find what:", findText);
    form->addRow("Replace &with:", replacementText);
    fields->addLayout(form);
    wholeWord = new QCheckBox("Match &whole word only");
    matchCase = new QCheckBox("Match &case");
    wrapAround = new QCheckBox("Wra&p around");
    wrapAround->setChecked(true);
    fields->addWidget(wholeWord);
    fields->addWidget(matchCase);
    fields->addWidget(wrapAround);
    auto* modes = new QGroupBox("Search mode");
    auto* modeLayout = new QVBoxLayout(modes);
    mode = new QComboBox;
    mode->addItem("Normal", "normal");
    mode->addItem("Extended (\\n, \\r, \\t, \\x...)", "extended");
    mode->addItem("Regular expression", "regex");
    dotNewline = new QCheckBox(". matches newline");
    dotNewline->setEnabled(false);
    modeLayout->addWidget(mode);
    modeLayout->addWidget(dotNewline);
    fields->addWidget(modes);
    fields->addStretch();
    columns->addLayout(fields, 1);
    auto* actions = new QVBoxLayout;
    const auto add = [&](const QString& text, const QString& name, std::function<void()> callback) {
        auto* button = new QPushButton(text);
        button->setObjectName(name);
        button->setAutoDefault(false);
        actions->addWidget(button);
        connect(button, &QPushButton::clicked, this, std::move(callback));
        return button;
    };
    add("Find &Next", "find-next", [this] { if (findRequested) findRequested(false); });
    add("Find &Previous", "find-previous", [this] { if (findRequested) findRequested(true); });
    auto* replace = add("&Replace", "replace-one", [this] { if (replaceRequested) replaceRequested(false); });
    auto* replaceAll = add("Replace &All", "replace-all", [this] { if (replaceRequested) replaceRequested(true); });
    add("Coun&t", "find-count", [this] { if (findAllRequested) findAllRequested(false, true); });
    add("Find All in Current Document", "find-all-current", [this] { if (findAllRequested) findAllRequested(false, false); });
    add("Find All in All Opened Documents", "find-all-open", [this] { if (findAllRequested) findAllRequested(true, false); });
    add("Find in Files...", "find-in-files", [this] { if (filesRequested) filesRequested(); });
    add("Cancel Search", "cancel-find", [this] { if (cancelRequested) cancelRequested(); });
    add("Close", "close-find", [this] { hide(); });
    actions->addStretch();
    columns->addLayout(actions);
    outer->addLayout(columns);
    status = new QLabel("Searches apply to the active document. Find All can search both tab groups.");
    status->setObjectName("find-status");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    outer->addWidget(status);
    connect(tabs, &QTabBar::currentChanged, this, [this, form, replace, replaceAll](int index) {
        form->setRowVisible(replacementText, index == 1);
        replace->setVisible(index == 1);
        replaceAll->setVisible(index == 1);
        setWindowTitle(index == 1 ? "Replace" : "Find");
    });
    form->setRowVisible(replacementText, false);
    replace->hide();
    replaceAll->hide();
    setWindowTitle("Find");
    connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const bool regex = mode->currentData().toString() == "regex";
        wholeWord->setEnabled(!regex);
        if (regex) wholeWord->setChecked(false);
        dotNewline->setEnabled(regex);
    });
    for (auto* field : {findText, replacementText})
        connect(field, &QLineEdit::returnPressed, this, [this] { if (findRequested) findRequested(false); });
    const auto shortcut = [this](const char* sequence, std::function<void()> callback) {
        auto* action = new QShortcut(QKeySequence(sequence), this);
        connect(action, &QShortcut::activated, this, std::move(callback));
    };
    shortcut("Ctrl+F", [this] { openPage(false); });
    shortcut("Ctrl+H", [this] { openPage(true); });
    shortcut("F3", [this] { if (findRequested) findRequested(false); });
    shortcut("Shift+F3", [this] { if (findRequested) findRequested(true); });
}
void SearchDialog::openPage(bool replace) {
    tabs->setCurrentIndex(replace ? 1 : 0);
    show();
    raise();
    activateWindow();
    findText->setFocus();
    findText->selectAll();
}
void SearchDialog::report(const QString& message) { status->setText(message); }
}
