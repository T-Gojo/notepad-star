// SPDX-License-Identifier: GPL-3.0-or-later
#include "function_list.h"
#include <QCoreApplication>
#include <QDir>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (!QDir::setCurrent(QDir::rootPath())) return 1;
    const auto keys = star::function_list::DefinitionCatalog::embeddedKeys();
    star::function_list::Error error;
    const auto rust = star::function_list::DefinitionCatalog::loadEmbedded(QStringLiteral("rust"), &error);
    if (!rust || !rust->functions || keys.size() < 40) return 1;
    std::cout << keys.size() << " embedded definitions; raw archive links with Qt Core only\n";
    return 0;
}
