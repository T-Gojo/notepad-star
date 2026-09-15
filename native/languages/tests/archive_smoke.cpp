// SPDX-License-Identifier: GPL-3.0-or-later
#include "language_catalog.h"
#include <QCoreApplication>
#include <QDir>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (!QDir::setCurrent(QDir::rootPath())) return 1;
    QString error;
    auto catalog = star::languages::LanguageCatalog::load(&error);
    if (!catalog) { std::cerr << error.toStdString() << '\n'; return 1; }
    const auto* rust = catalog->language(QStringLiteral("rust"));
    if (!rust || rust->engine != "rust" || !catalog->completions(QStringLiteral("rust"))) return 1;
    std::cout << catalog->languages().size() << " languages loaded from raw-linked archive resources\n";
    return 0;
}
