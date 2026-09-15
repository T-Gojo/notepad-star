// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>

namespace star::languages {

enum class Theme { Light, Dark };

struct KeywordSet {
    int index = 0;
    QByteArray text;
};

struct LexerProperty {
    QByteArray name;
    QByteArray value;
};

struct Style {
    int id = 0;
    QString name;
    std::optional<QColor> foreground;
    std::optional<QColor> background;
    int colorFlags = 3; // 1: foreground, 2: background.
    int fontFlags = -1; // -1: inherit; otherwise 1: bold, 2: italic, 4: underline.
    QString fontFamily;
    int fontSize = 0; // 0: inherit.
    quint32 nesting = 0;
};

struct Language {
    QString id;
    QString displayName;
    QStringList extensions;
    QByteArray engine;
    QByteArray commentLine;
    QByteArray commentStart;
    QByteArray commentEnd;
    QString commentError;
    QList<KeywordSet> keywords;
    QList<LexerProperty> properties;
};

struct Calltip {
    QString signature;
    QString returnType;
    QStringList parameters;
    QString description;
};

struct CompletionEntry {
    QString name;
    bool function = false;
    QList<Calltip> overloads;
};

struct CompletionData {
    bool ignoreCase = false;
    QChar startFunction = u'(';
    QChar stopFunction = u')';
    QChar parameterSeparator = u',';
    QString additionalWordCharacters;
    QList<CompletionEntry> entries;
};

struct ThemeData {
    std::optional<Style> defaultStyle;
    QMap<QString, QList<Style>> languages;
};

struct AssetPaths {
    QString languages;
    QString lightTheme;
    QString darkTheme;
    QString completionsDirectory;
    static AssetPaths embedded();
};

struct UdlConfiguration {
    Language language;
    QList<Style> styles;
    bool darkTheme = false;
};

class LanguageCatalog {
public:
    static std::optional<LanguageCatalog> load(QString* error = nullptr);
    static std::optional<LanguageCatalog> load(const AssetPaths& paths, QString* error = nullptr);
    const QList<Language>& languages() const { return languages_; }
    const Language* language(const QString& identifier) const;
    const Language* forExtension(const QString& extension) const;
    const Language* detectFileName(const QString& fileName) const;
    QList<Style> styles(const QString& identifier, Theme theme) const;
    std::optional<Style> defaultStyle(Theme theme) const;
    std::optional<CompletionData> completions(const QString& identifier, QString* error = nullptr) const;

    static bool supportsEngine(const QByteArray& engine);
    static bool supportsUserDefined();
    static std::optional<ThemeData> parseThemeXml(const QByteArray& xml, QString* error = nullptr);
    static std::optional<CompletionData> parseCompletionXml(const QByteArray& xml, QString* error = nullptr);
    // IDs must be positive and unique among live UDL configurations/documents.
    // UDL 2.1 only. The caller must recolor from byte zero after configuration.
    static std::optional<UdlConfiguration> parseUdlXml(
        const QByteArray& xml, int udlIdentity, int documentIdentity, QString* error = nullptr);

private:
    QList<Language> languages_;
    QMap<QString, qsizetype> byId_;
    QMap<QString, qsizetype> byExtension_;
    ThemeData light_;
    ThemeData dark_;
    QString completionDirectory_;
};

} // namespace star::languages
