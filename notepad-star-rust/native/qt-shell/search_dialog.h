#pragma once
#include <QDialog>
#include <functional>
class QLineEdit;
class QCheckBox;
class QComboBox;
class QLabel;
class QTabBar;

namespace star {
class SearchDialog final : public QDialog {
public:
    explicit SearchDialog(QWidget* parent);
    void openPage(bool replace);
    void report(const QString& message);
    QLineEdit* findText;
    QLineEdit* replacementText;
    QCheckBox* matchCase;
    QCheckBox* wholeWord;
    QCheckBox* wrapAround;
    QCheckBox* dotNewline;
    QComboBox* mode;
    std::function<void(bool)> findRequested;
    std::function<void(bool)> replaceRequested;
    std::function<void(bool, bool)> findAllRequested;
    std::function<void()> cancelRequested;
    std::function<void()> filesRequested;
private:
    QTabBar* tabs;
    QLabel* status;
};
}
