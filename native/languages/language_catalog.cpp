// SPDX-License-Identifier: GPL-3.0-or-later
#include "language_catalog.h"
#include "xml_internal.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QResource>
#include <QSet>
#include <algorithm>

static void initializeLanguageResources() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(star_language_assets);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace star::languages {
namespace {
using namespace detail;

struct BuiltinMapping {
    const char* id;
    const char* display;
    const char* engine;
    int genericMask;
};
#include "upstream_mappings.h"

struct RawLanguage {
    Language language;
    QMap<QString, QString> words;
    int genericMask = -1;
};

const BuiltinMapping* mapping(const QString& name) {
    for (const auto& item : builtinMappings)
        if (name == QLatin1String(item.id)) return &item;
    return nullptr;
}

const QStringList& keywordClassNames() {
    static const QStringList names{
        QStringLiteral("instre1"), QStringLiteral("instre2"), QStringLiteral("type1"),
        QStringLiteral("type2"), QStringLiteral("type3"), QStringLiteral("type4"),
        QStringLiteral("type5"), QStringLiteral("type6"), QStringLiteral("type7")};
    return names;
}

int keywordClass(const QString& name) {
    const auto index = keywordClassNames().indexOf(name);
    if (index >= 0) return static_cast<int>(index);
    if (name.size() == 1 && name.front() >= u'0' && name.front() <= u'8')
        return name.front().unicode() - u'0';
    static const QRegularExpression substyle(QStringLiteral("^substyle[1-8]$"));
    return substyle.match(name).hasMatch() ? -2 : -1;
}

bool configureKeywords(RawLanguage& raw, const QMap<QString, RawLanguage>& all, QString* error) {
    auto& language = raw.language;
    QMap<int, QByteArray> words;
    bool validSources = true;
    auto add = [&](int index, const QString& source, const char* group) {
        const auto found = all.constFind(source);
        if (found == all.cend()) {
            fail(error, QStringLiteral("Missing shared keyword source: %1.").arg(source));
            validSources = false;
            return;
        }
        words[index] = found->words.value(QLatin1String(group)).simplified().toUtf8();
    };
    const QString id = language.id;
    if (language.engine == "cpp" || id == QStringLiteral("objc")) {
        const QString source = id == QStringLiteral("javascript") ? QStringLiteral("javascript.js") : id;
        add(0, source, "instre1");
        add(1, source, "type1");
        if (id != QStringLiteral("rc")) add(2, QStringLiteral("cpp"), "type2");
        if (id != QStringLiteral("typescript")) add(3, source, "instre2");
        if (id == QStringLiteral("objc")) add(4, source, "type2");
        if (language.engine == "cpp") language.properties.push_back({"lexer.cpp.track.preprocessor", "0"});
        language.properties.push_back({"fold.preprocessor", "1"});
        if (id == QStringLiteral("javascript") || id == QStringLiteral("javascript.js"))
            language.properties.push_back({"lexer.cpp.backquoted.strings", "2"});
        else if (id == QStringLiteral("typescript") || id == QStringLiteral("go"))
            language.properties.push_back({"lexer.cpp.backquoted.strings", "1"});
    } else if (id == QStringLiteral("html") || id == QStringLiteral("php")
        || id == QStringLiteral("asp") || id == QStringLiteral("jsp")) {
        // setXmlLexer overrides PHP's table entry ("phpscript") to "hypertext".
        language.engine = "hypertext";
        add(0, QStringLiteral("html"), "instre1");
        add(1, QStringLiteral("javascript"), "instre1");
        add(2, QStringLiteral("vb"), "instre1");
        add(4, QStringLiteral("php"), "instre1");
        add(5, QStringLiteral("html"), "instre2");
        language.properties.push_back({"asp.default.language", "2"});
        language.properties.push_back({"fold.html", "1"});
    } else if (id == QStringLiteral("xml")) {
        add(5, id, "instre1");
        language.properties.push_back({"lexer.xml.allow.scripts", "0"});
    } else if (id == QStringLiteral("json") || id == QStringLiteral("json5")) {
        add(0, QStringLiteral("json"), "instre1");
        add(1, QStringLiteral("json"), "instre2");
        language.properties.push_back({"lexer.json.escape.sequence", "1"});
        language.properties.push_back({"lexer.json.allow.comments", id == QStringLiteral("json5") ? "1" : "0"});
    } else if (id == QStringLiteral("tcl")) {
        add(0, id, "instre1");
        add(1, id, "type1");
        add(2, id, "instre2");
        for (auto it = raw.words.cbegin(); it != raw.words.cend(); ++it) {
            const int index = keywordClass(it.key());
            if (index >= 3) words[index] = it.value().simplified().toUtf8();
        }
    } else {
        const int mask = id == QStringLiteral("bash") ? 1 : raw.genericMask;
        for (auto it = raw.words.cbegin(); it != raw.words.cend(); ++it) {
            const int index = keywordClass(it.key());
            if (index < 0) continue;
            if (mask < 0 && !it.value().isEmpty()) {
                fail(error, QStringLiteral("No verified keyword mapping for %1.").arg(id));
                return false;
            }
            if (mask >= 0 && (mask & (1 << index)))
                words[index] = it.value().simplified().toUtf8();
        }
    }
    if (!validSources) return false;
    if (id == QStringLiteral("python")) {
        language.properties.push_back({"fold.quotes.python", "1"});
        language.properties.push_back({"lexer.python.decorator.attributes", "1"});
        language.properties.push_back({"lexer.python.identifier.attributes", "1"});
    }
    if (language.engine != "null") {
        language.properties.push_back({"fold", "1"});
        language.properties.push_back({"fold.compact", "0"});
        language.properties.push_back({"fold.comment", "1"});
    }
    for (auto it = words.cbegin(); it != words.cend(); ++it)
        language.keywords.push_back({it.key(), it.value()});
    return true;
}

QString canonicalIdentifier(QString name) {
    name = name.trimmed().toCaseFolded();
    static const QMap<QString, QString> aliases{
        {QStringLiteral("c++"), QStringLiteral("cpp")},
        {QStringLiteral("cxx"), QStringLiteral("cpp")},
        {QStringLiteral("c#"), QStringLiteral("cs")},
        {QStringLiteral("csharp"), QStringLiteral("cs")},
        {QStringLiteral("py"), QStringLiteral("python")},
        {QStringLiteral("python3"), QStringLiteral("python")},
        {QStringLiteral("rs"), QStringLiteral("rust")},
        {QStringLiteral("js"), QStringLiteral("javascript.js")},
        {QStringLiteral("shell"), QStringLiteral("bash")},
        {QStringLiteral("sh"), QStringLiteral("bash")},
        {QStringLiteral("objective-c"), QStringLiteral("objc")},
        {QStringLiteral("text"), QStringLiteral("normal")},
    };
    return aliases.value(name, name);
}
} // namespace

AssetPaths AssetPaths::embedded() {
    initializeLanguageResources();
    const QString root = QStringLiteral(":/notepad-star/languages/PowerEditor/");
    return {
        root + QStringLiteral("src/langs.model.xml"),
        root + QStringLiteral("src/stylers.model.xml"),
        root + QStringLiteral("installer/themes/DarkModeDefault.xml"),
        root + QStringLiteral("installer/APIs"),
    };
}

bool LanguageCatalog::supportsEngine(const QByteArray& engine) {
    return std::any_of(std::begin(availableEngines), std::end(availableEngines),
        [&](const char* name) { return engine == name; });
}

bool LanguageCatalog::supportsUserDefined() { return supportsEngine("user"); }

std::optional<LanguageCatalog> LanguageCatalog::load(QString* error) {
    return load(AssetPaths::embedded(), error);
}

std::optional<LanguageCatalog> LanguageCatalog::load(const AssetPaths& paths, QString* error) {
    QString localError;
    if (!error) error = &localError;
    error->clear();
    initializeLanguageResources();
    auto bytes = detail::readAsset(paths.languages, error);
    if (!bytes) return std::nullopt;
    auto root = detail::parseXml(*bytes, error);
    if (!root) return std::nullopt;
    const auto* languages = detail::child(*root, QStringLiteral("Languages"), error);
    if (!languages || !detail::childrenAllowed(*languages, {QStringLiteral("Language")}, error))
        return std::nullopt;
    LanguageCatalog catalog;
    QMap<QString, RawLanguage> raw;
    QStringList order;
    for (const auto& node : languages->children) {
        const QString id = node.attributes.value(QStringLiteral("name"));
        const auto* mapped = mapping(id);
        if (!mapped || raw.contains(id) || raw.size() >= 256) {
            detail::fail(error, QStringLiteral("Unknown, duplicate, or excessive language definitions: %1.").arg(id));
            return std::nullopt;
        }
        RawLanguage entry;
        entry.language.id = id;
        entry.language.displayName = QString::fromUtf8(mapped->display);
        entry.language.engine = mapped->engine;
        entry.language.commentLine = node.attributes.value(QStringLiteral("commentLine")).toUtf8();
        entry.language.commentStart = node.attributes.value(QStringLiteral("commentStart")).toUtf8();
        entry.language.commentEnd = node.attributes.value(QStringLiteral("commentEnd")).toUtf8();
        for (const auto& symbol : {entry.language.commentLine, entry.language.commentStart, entry.language.commentEnd}) {
            if (symbol.size() > 127 || symbol.contains('\0') || symbol.contains('\n') || symbol.contains('\r')) {
                detail::fail(error, QStringLiteral("Unsupported language comment delimiter."));
                return std::nullopt;
            }
        }
        entry.genericMask = mapped->genericMask;
        if (!supportsEngine(entry.language.engine)) {
            detail::fail(error, QStringLiteral("Language %1 uses an unavailable Lexilla engine.").arg(id));
            return std::nullopt;
        }
        entry.language.extensions = node.attributes.value(QStringLiteral("ext"))
            .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        for (auto& extension : entry.language.extensions) {
            extension = extension.toCaseFolded();
            if (extension.contains(u'/') || extension.contains(u'\\') || extension.size() > 128) {
                detail::fail(error, QStringLiteral("Invalid language extension."));
                return std::nullopt;
            }
        }
        if (!detail::childrenAllowed(node, {QStringLiteral("Keywords")}, error)) return std::nullopt;
        for (const auto& keywords : node.children) {
            QString name = keywords.attributes.value(QStringLiteral("name"));
            const int index = keywordClass(name);
            if (index >= 0) name = keywordClassNames()[index];
            if (index == -1 || entry.words.contains(name) || !keywords.children.isEmpty()) {
                detail::fail(error, QStringLiteral("Unknown, duplicate, or nested keyword class: %1.").arg(name));
                return std::nullopt;
            }
            entry.words.insert(name, keywords.text);
        }
        raw.insert(id, std::move(entry));
        order.push_back(id);
    }
    if (raw.isEmpty()) {
        detail::fail(error, QStringLiteral("No language definitions."));
        return std::nullopt;
    }
    for (const auto& id : order) {
        auto entry = raw.value(id);
        if (!configureKeywords(entry, raw, error)) return std::nullopt;
        const auto index = catalog.languages_.size();
        catalog.byId_.insert(id, index);
        // Upstream getLangFromExt searches the XML-loaded language list backwards.
        for (const auto& extension : entry.language.extensions)
            catalog.byExtension_.insert(extension, index);
        catalog.languages_.push_back(std::move(entry.language));
    }
    auto lightBytes = detail::readAsset(paths.lightTheme, error);
    if (!lightBytes) return std::nullopt;
    auto light = parseThemeXml(*lightBytes, error);
    if (!light) return std::nullopt;
    auto darkBytes = detail::readAsset(paths.darkTheme, error);
    if (!darkBytes) return std::nullopt;
    auto dark = parseThemeXml(*darkBytes, error);
    if (!dark) return std::nullopt;
    if (!(paths.completionsDirectory.startsWith(QStringLiteral(":/"))
        || QDir::isAbsolutePath(paths.completionsDirectory))) {
        detail::fail(error, QStringLiteral("Completion directory must be an explicit absolute/resource path."));
        return std::nullopt;
    }
    catalog.light_ = std::move(*light);
    catalog.dark_ = std::move(*dark);
    catalog.completionDirectory_ = paths.completionsDirectory;
    return catalog;
}

const Language* LanguageCatalog::language(const QString& identifier) const {
    const auto it = byId_.constFind(canonicalIdentifier(identifier));
    return it == byId_.cend() ? nullptr : &languages_[it.value()];
}

const Language* LanguageCatalog::forExtension(const QString& extension) const {
    QString key = extension.toCaseFolded();
    if (key.startsWith(u'.')) key.remove(0, 1);
    const auto it = byExtension_.constFind(key);
    return it == byExtension_.cend() ? nullptr : &languages_[it.value()];
}

const Language* LanguageCatalog::detectFileName(const QString& fileName) const {
    const auto name = QString(fileName).replace(u'\\', u'/').section(u'/', -1).toCaseFolded();
    static const QMap<QString, QString> exactNames{
        {QStringLiteral("makefile"), QStringLiteral("makefile")},
        {QStringLiteral("gnumakefile"), QStringLiteral("makefile")},
        {QStringLiteral("cmakelists.txt"), QStringLiteral("cmake")},
        {QStringLiteral("sconstruct"), QStringLiteral("python")},
        {QStringLiteral("sconscript"), QStringLiteral("python")},
        {QStringLiteral("wscript"), QStringLiteral("python")},
        {QStringLiteral("rakefile"), QStringLiteral("ruby")},
        {QStringLiteral("vagrantfile"), QStringLiteral("ruby")},
        {QStringLiteral("crontab"), QStringLiteral("bash")},
        {QStringLiteral("pkgbuild"), QStringLiteral("bash")},
        {QStringLiteral("apkbuild"), QStringLiteral("bash")},
    };
    if (exactNames.contains(name)) return language(exactNames.value(name));
    // Longest suffix first also handles any future multi-part extension assets.
    for (auto dot = name.indexOf(u'.'); dot >= 0; dot = name.indexOf(u'.', dot + 1))
        if (const auto* result = forExtension(name.mid(dot + 1))) return result;
    return nullptr;
}

std::optional<Style> LanguageCatalog::defaultStyle(Theme theme) const {
    return (theme == Theme::Light ? light_ : dark_).defaultStyle;
}

QList<Style> LanguageCatalog::styles(const QString& identifier, Theme theme) const {
    const auto* item = language(identifier);
    if (!item) return {};
    const auto& data = theme == Theme::Light ? light_ : dark_;
    QStringList sources{item->id};
    if (item->id == QStringLiteral("json5")) sources = {QStringLiteral("json")};
    else if (item->id == QStringLiteral("javascript")) sources = {QStringLiteral("javascript.js")};
    else if (item->engine == "hypertext")
        sources = {QStringLiteral("html"), QStringLiteral("javascript"), QStringLiteral("php"), QStringLiteral("asp")};
    QMap<int, Style> byStyle;
    for (const auto& source : sources)
        for (const auto& style : data.languages.value(source))
            byStyle.insert(style.id, style);
    return byStyle.values();
}

std::optional<ThemeData> LanguageCatalog::parseThemeXml(const QByteArray& xml, QString* error) {
    QString localError;
    if (!error) error = &localError;
    auto root = detail::parseXml(xml, error);
    if (!root) return std::nullopt;
    const auto* lexers = detail::child(*root, QStringLiteral("LexerStyles"), error);
    const auto* globals = detail::child(*root, QStringLiteral("GlobalStyles"), error);
    if (!lexers || !globals) return std::nullopt;
    ThemeData result;
    for (const auto& node : lexers->children) {
        const QString name = node.attributes.value(QStringLiteral("name"));
        if (node.name != QStringLiteral("LexerType") || name.isEmpty() || result.languages.contains(name)) {
            detail::fail(error, QStringLiteral("Invalid or duplicate theme language."));
            return std::nullopt;
        }
        QList<Style> styles;
        QSet<int> ids;
        for (const auto& wordStyle : node.children) {
            if (wordStyle.name != QStringLiteral("WordsStyle")) {
                detail::fail(error, QStringLiteral("Unexpected theme style element."));
                return std::nullopt;
            }
            auto style = detail::parseStyle(wordStyle, std::nullopt, error);
            if (!style) return std::nullopt;
            if (ids.contains(style->id)) {
                detail::fail(error, QStringLiteral("Duplicate style ID within theme language."));
                return std::nullopt;
            }
            ids.insert(style->id);
            styles.push_back(std::move(*style));
        }
        result.languages.insert(name, std::move(styles));
    }
    for (const auto& widget : globals->children) {
        if (widget.name != QStringLiteral("WidgetStyle")) {
            detail::fail(error, QStringLiteral("Unexpected global theme element."));
            return std::nullopt;
        }
        if (widget.attributes.value(QStringLiteral("name")) != QStringLiteral("Default Style")) continue;
        if (result.defaultStyle) {
            detail::fail(error, QStringLiteral("Duplicate default style."));
            return std::nullopt;
        }
        result.defaultStyle = detail::parseStyle(widget, 32, error);
        if (!result.defaultStyle) return std::nullopt;
    }
    if (!result.defaultStyle) {
        detail::fail(error, QStringLiteral("Theme has no global Default Style."));
        return std::nullopt;
    }
    return result;
}

std::optional<CompletionData> LanguageCatalog::completions(const QString& identifier, QString* error) const {
    if (error) error->clear();
    const auto* item = language(identifier);
    if (!item) {
        detail::fail(error, QStringLiteral("Unknown completion language."));
        return std::nullopt;
    }
    QString base = item->id;
    if (base == QStringLiteral("javascript.js")) base = QStringLiteral("javascript");
    if (base == QStringLiteral("coffeescript")) base = QStringLiteral("coffee");
    const QDir directory(completionDirectory_);
    for (const auto& name : directory.entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name)) {
        if (QFileInfo(name).completeBaseName().compare(base, Qt::CaseInsensitive) != 0) continue;
        auto bytes = detail::readAsset(directory.filePath(name), error);
        return bytes ? parseCompletionXml(*bytes, error) : std::nullopt;
    }
    detail::fail(error, QStringLiteral("No checked-in completion asset for this language."));
    return std::nullopt;
}

std::optional<CompletionData> LanguageCatalog::parseCompletionXml(const QByteArray& xml, QString* error) {
    QString localError;
    if (!error) error = &localError;
    auto root = detail::parseXml(xml, error);
    if (!root) return std::nullopt;
    const auto* complete = detail::child(*root, QStringLiteral("AutoComplete"), error);
    if (!complete || !detail::childrenAllowed(*complete,
        {QStringLiteral("Environment"), QStringLiteral("KeyWord")}, error)) return std::nullopt;
    CompletionData data;
    const auto* environment = detail::child(*complete, QStringLiteral("Environment"), error, false);
    if (!error->isEmpty()) return std::nullopt;
    if (environment) {
        const auto ignore = detail::boolean(*environment, QStringLiteral("ignoreCase"), false, error);
        if (!ignore) return std::nullopt;
        data.ignoreCase = *ignore;
        for (const auto& pair : {
            std::pair{QStringLiteral("startFunc"), &data.startFunction},
            std::pair{QStringLiteral("stopFunc"), &data.stopFunction},
            std::pair{QStringLiteral("paramSeparator"), &data.parameterSeparator}}) {
            const QString value = environment->attributes.value(pair.first);
            if (value.isEmpty()) continue;
            if (value.size() != 1) {
                detail::fail(error, QStringLiteral("Calltip delimiters must be single characters."));
                return std::nullopt;
            }
            *pair.second = value.front();
        }
        data.additionalWordCharacters = environment->attributes.value(QStringLiteral("additionalWordChar"));
    }
    QMap<QString, qsizetype> names;
    for (const auto& node : complete->children) {
        if (node.name == QStringLiteral("Environment")) continue;
        CompletionEntry entry;
        entry.name = node.attributes.value(QStringLiteral("name"));
        const auto function = detail::boolean(node, QStringLiteral("func"), false, error);
        if (entry.name.isEmpty() || entry.name.size() > 1024 || !function
            || !detail::childrenAllowed(node, {QStringLiteral("Overload")}, error)) {
            detail::fail(error, QStringLiteral("Invalid completion keyword."));
            return std::nullopt;
        }
        entry.function = *function || !node.children.isEmpty();
        for (const auto& overload : node.children) {
            Calltip tip;
            tip.returnType = overload.attributes.value(QStringLiteral("retVal"));
            tip.description = overload.attributes.value(QStringLiteral("descr"));
            if (!detail::childrenAllowed(overload, {QStringLiteral("Param")}, error)) return std::nullopt;
            for (const auto& param : overload.children) {
                if (!param.children.isEmpty() || !param.attributes.contains(QStringLiteral("name"))) {
                    detail::fail(error, QStringLiteral("Invalid calltip parameter."));
                    return std::nullopt;
                }
                tip.parameters.push_back(param.attributes.value(QStringLiteral("name")));
            }
            tip.signature = (tip.returnType.isEmpty() ? QString{} : tip.returnType + u' ')
                + entry.name + data.startFunction
                + tip.parameters.join(QString(data.parameterSeparator) + u' ') + data.stopFunction;
            entry.overloads.push_back(std::move(tip));
        }
        // Some upstream completion files repeat names with additional overloads.
        if (names.contains(entry.name)) {
            auto& existing = data.entries[names.value(entry.name)];
            existing.function = existing.function || entry.function;
            existing.overloads.append(entry.overloads);
        } else {
            names.insert(entry.name, data.entries.size());
            data.entries.push_back(std::move(entry));
        }
    }
    return data;
}
} // namespace star::languages
