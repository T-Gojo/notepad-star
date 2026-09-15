#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <map>

namespace star {
struct ImportedMacro {
    QString name;
    QJsonArray steps;
    QString error;
};
QList<ImportedMacro> importNotepadMacros(const QByteArray& xml, const std::map<unsigned int, QString>& allowedCommands);
}
