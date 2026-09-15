// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "language_catalog.h"

namespace star::languages::detail {

inline constexpr qsizetype MaxXmlBytes = 8 * 1024 * 1024;
struct Node {
    QString name;
    QMap<QString, QString> attributes;
    QString text;
    QList<Node> children;
};

void fail(QString* error, const QString& message);
std::optional<Node> parseXml(const QByteArray& bytes, QString* error);
const Node* child(const Node& node, const QString& name, QString* error, bool required = true);
std::optional<QByteArray> readAsset(const QString& path, QString* error);
std::optional<int> integer(const Node& node, const QString& attribute, int fallback,
    int minimum, int maximum, QString* error);
std::optional<bool> boolean(const Node& node, const QString& attribute, bool fallback, QString* error);
std::optional<Style> parseStyle(const Node& node, std::optional<int> fixedId, QString* error);
bool attributesAllowed(const Node& node, const QStringList& allowed, QString* error);
bool childrenAllowed(const Node& node, const QStringList& allowed, QString* error);

} // namespace star::languages::detail
