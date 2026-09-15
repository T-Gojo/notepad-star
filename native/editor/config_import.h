#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QStringList>

namespace star {
struct ConfigImport {
    QJsonObject patch;
    QStringList unsupported;
};
ConfigImport importNotepadConfig(const QByteArray& xml);
QJsonObject mergeConfigPatch(QJsonObject settings, const QJsonObject& patch);
}
