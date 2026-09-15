// SPDX-License-Identifier: GPL-3.0-or-later
#include "xml_internal.h"
#include "SciLexer.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <array>

namespace star::languages {
namespace {
using namespace detail;

struct UdlWord {
    const char* name;
    const char* property;
    int keywordIndex;
};

// UserDefineDialog.h GlobalMappers + setUserLexer's ordered, non-property lists.
constexpr std::array<UdlWord, SCE_USER_KWLIST_TOTAL> udlWords{{
    {"Comments", "userDefine.comments", -1},
    {"Numbers, prefix1", "userDefine.numberPrefix1", -1},
    {"Numbers, prefix2", "userDefine.numberPrefix2", -1},
    {"Numbers, extras1", "userDefine.numberExtras1", -1},
    {"Numbers, extras2", "userDefine.numberExtras2", -1},
    {"Numbers, suffix1", "userDefine.numberSuffix1", -1},
    {"Numbers, suffix2", "userDefine.numberSuffix2", -1},
    {"Numbers, range", "userDefine.numberRange", -1},
    {"Operators1", "userDefine.operators1", -1},
    {"Operators2", nullptr, 0},
    {"Folders in code1, open", "userDefine.foldersInCode1Open", -1},
    {"Folders in code1, middle", "userDefine.foldersInCode1Middle", -1},
    {"Folders in code1, close", "userDefine.foldersInCode1Close", -1},
    {"Folders in code2, open", nullptr, 1},
    {"Folders in code2, middle", nullptr, 2},
    {"Folders in code2, close", nullptr, 3},
    {"Folders in comment, open", nullptr, 4},
    {"Folders in comment, middle", nullptr, 5},
    {"Folders in comment, close", nullptr, 6},
    {"Keywords1", nullptr, 7},
    {"Keywords2", nullptr, 8},
    {"Keywords3", nullptr, 9},
    {"Keywords4", nullptr, 10},
    {"Keywords5", nullptr, 11},
    {"Keywords6", nullptr, 12},
    {"Keywords7", nullptr, 13},
    {"Keywords8", nullptr, 14},
    {"Delimiters", "userDefine.delimiters", -1},
}};
static_assert(SCE_USER_KWLIST_OPERATORS2 == 9 && SCE_USER_KWLIST_KEYWORDS1 == 19
    && SCE_USER_KWLIST_DELIMITERS == 27);

const std::array<const char*, SCE_USER_STYLE_TOTAL_STYLES> udlStyleNames{{
    "DEFAULT", "COMMENTS", "LINE COMMENTS", "NUMBERS",
    "KEYWORDS1", "KEYWORDS2", "KEYWORDS3", "KEYWORDS4",
    "KEYWORDS5", "KEYWORDS6", "KEYWORDS7", "KEYWORDS8",
    "OPERATORS", "FOLDER IN CODE1", "FOLDER IN CODE2", "FOLDER IN COMMENT",
    "DELIMITERS1", "DELIMITERS2", "DELIMITERS3", "DELIMITERS4",
    "DELIMITERS5", "DELIMITERS6", "DELIMITERS7", "DELIMITERS8",
}};
static_assert(SCE_USER_STYLE_KEYWORD1 == 4 && SCE_USER_STYLE_DELIMITER1 == 16);

std::optional<QByteArray> encodeQuotedWords(const QByteArray& input, QString* error) {
    bool doubleQuoted = false;
    bool singleQuoted = false;
    bool nonWhitespaceFound = false;
    QByteArray result;
    for (qsizetype j = 0; j < input.size(); ++j) {
        const auto ch = input[j];
        if (!singleQuoted && ch == '"') { doubleQuoted = !doubleQuoted; continue; }
        if (!doubleQuoted && ch == '\'') { singleQuoted = !singleQuoted; continue; }
        if (ch == '\\' && j + 1 < input.size()
            && (input[j + 1] == '"' || input[j + 1] == '\'' || input[j + 1] == '\\')) {
            result += input[++j];
            continue;
        }
        if (doubleQuoted || singleQuoted) {
            if (static_cast<unsigned char>(ch) > ' ') {
                result += ch;
                nonWhitespaceFound = true;
            } else if (nonWhitespaceFound && j > 0 && j + 1 < input.size()
                && input[j - 1] != '"' && input[j + 1] != '"'
                && static_cast<unsigned char>(input[j + 1]) > ' ') {
                result += doubleQuoted ? '\v' : '\b';
            }
        } else result += ch;
    }
    if (singleQuoted || doubleQuoted) {
        fail(error, QStringLiteral("Unterminated quoted UDL keyword."));
        return std::nullopt;
    }
    return result;
}

bool validateNumberedTokens(const QByteArray& bytes, int maximum, QString* error) {
    qsizetype i = 0;
    while (i < bytes.size()) {
        while (i < bytes.size() && bytes[i] == ' ') ++i;
        if (i == bytes.size()) return true;
        if (i + 1 >= bytes.size() || bytes[i] < '0' || bytes[i] > '9'
            || bytes[i + 1] < '0' || bytes[i + 1] > '9'
            || (bytes[i] - '0') * 10 + bytes[i + 1] - '0' > maximum) {
            fail(error, QStringLiteral("Invalid numbered UDL comment/delimiter token."));
            return false;
        }
        i += 2;
        if (bytes.mid(i, 2) == "((") {
            const auto end = bytes.indexOf("))", i + 2);
            if (end < 0 || (end + 2 < bytes.size() && bytes[end + 2] != ' ')) {
                fail(error, QStringLiteral("Invalid grouped UDL comment/delimiter token."));
                return false;
            }
            i = end + 2;
        } else {
            while (i < bytes.size() && bytes[i] != ' ') {
                if (static_cast<unsigned char>(bytes[i]) < 32) {
                    fail(error, QStringLiteral("UDL numbered tokens require space separators."));
                    return false;
                }
                ++i;
            }
        }
    }
    return true;
}
} // namespace

std::optional<UdlConfiguration> LanguageCatalog::parseUdlXml(
    const QByteArray& xml, int udlIdentity, int documentIdentity, QString* error) {
    QString localError;
    if (!error) error = &localError;
    error->clear();
    if (!supportsUserDefined() || udlIdentity <= 0 || documentIdentity <= 0) {
        detail::fail(error, QStringLiteral("UDL requires the actual user lexer and positive UDL/document identities."));
        return std::nullopt;
    }
    auto root = detail::parseXml(xml, error);
    if (!root || !detail::childrenAllowed(*root, {QStringLiteral("UserLang")}, error)) return std::nullopt;
    const auto* udl = detail::child(*root, QStringLiteral("UserLang"), error);
    if (!udl || !detail::attributesAllowed(*udl,
        {QStringLiteral("name"), QStringLiteral("ext"), QStringLiteral("udlVersion"), QStringLiteral("darkModeTheme")}, error)
        || !detail::childrenAllowed(*udl,
            {QStringLiteral("Settings"), QStringLiteral("KeywordLists"), QStringLiteral("Styles")}, error))
        return std::nullopt;
    if (udl->attributes.value(QStringLiteral("udlVersion")) != QStringLiteral("2.1")) {
        detail::fail(error, QStringLiteral("Only Notepad++ UDL 2.1 is supported; legacy conversion is not implemented."));
        return std::nullopt;
    }
    UdlConfiguration result;
    auto& language = result.language;
    language.id = QStringLiteral("udl:%1").arg(udlIdentity);
    language.displayName = udl->attributes.value(QStringLiteral("name"));
    language.engine = "user";
    const auto dark = detail::boolean(*udl, QStringLiteral("darkModeTheme"), false, error);
    if (!dark || language.displayName.trimmed().isEmpty() || language.displayName.size() > 128
        || std::any_of(language.displayName.cbegin(), language.displayName.cend(),
            [](QChar c) { return c.category() == QChar::Other_Control; })) {
        detail::fail(error, QStringLiteral("Invalid UDL display name or dark-mode flag."));
        return std::nullopt;
    }
    result.darkTheme = *dark;
    language.extensions = udl->attributes.value(QStringLiteral("ext"))
        .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    static const QRegularExpression extensionPattern(QStringLiteral("^[\\p{L}\\p{N}_][\\p{L}\\p{N}_.+-]*$"));
    if (language.extensions.size() > 128) {
        detail::fail(error, QStringLiteral("Too many UDL extensions."));
        return std::nullopt;
    }
    for (auto& extension : language.extensions) {
        if (extension.size() > 128 || !extensionPattern.match(extension).hasMatch()) {
            detail::fail(error, QStringLiteral("UDL extensions must be simple suffixes, not paths or wildcards."));
            return std::nullopt;
        }
        extension = extension.toCaseFolded();
    }
    const auto* settings = detail::child(*udl, QStringLiteral("Settings"), error);
    const auto* keywords = detail::child(*udl, QStringLiteral("KeywordLists"), error);
    const auto* styles = detail::child(*udl, QStringLiteral("Styles"), error);
    if (!settings || !keywords || !styles
        || !detail::childrenAllowed(*settings, {QStringLiteral("Global"), QStringLiteral("Prefix")}, error)
        || !detail::childrenAllowed(*keywords, {QStringLiteral("Keywords")}, error)
        || !detail::childrenAllowed(*styles, {QStringLiteral("WordsStyle")}, error)) return std::nullopt;
    const auto* global = detail::child(*settings, QStringLiteral("Global"), error);
    const auto* prefix = detail::child(*settings, QStringLiteral("Prefix"), error);
    if (!global || !prefix || !detail::childrenAllowed(*global, {}, error)
        || !detail::childrenAllowed(*prefix, {}, error)) return std::nullopt;
    if (!detail::attributesAllowed(*global, {
        QStringLiteral("caseIgnored"), QStringLiteral("allowFoldOfComments"), QStringLiteral("foldCompact"),
        QStringLiteral("forcePureLC"), QStringLiteral("decimalSeparator")}, error)) return std::nullopt;
    auto property = [&](const QByteArray& name, const QByteArray& value) {
        language.properties.push_back({name, value});
    };
    property("fold", "1");
    for (const auto& pair : {
        std::pair{"caseIgnored", "userDefine.isCaseIgnored"},
        std::pair{"allowFoldOfComments", "userDefine.allowFoldOfComments"},
        std::pair{"foldCompact", "userDefine.foldCompact"}}) {
        const auto value = detail::boolean(*global, QLatin1String(pair.first), false, error);
        if (!value) return std::nullopt;
        property(pair.second, *value ? "1" : "0");
    }
    const auto pure = detail::integer(*global, QStringLiteral("forcePureLC"), 0, 0, 2, error);
    const auto decimal = detail::integer(*global, QStringLiteral("decimalSeparator"), 0, 0, 2, error);
    if (!pure || !decimal) return std::nullopt;
    property("userDefine.forcePureLC", QByteArray::number(*pure));
    property("userDefine.decimalSeparator", QByteArray::number(*decimal));
    property("userDefine.udlName", QByteArray::number(udlIdentity));
    property("userDefine.currentBufferID", QByteArray::number(documentIdentity));
    QStringList prefixNames;
    for (int i = 1; i <= 8; ++i) {
        const auto name = QStringLiteral("Keywords%1").arg(i);
        prefixNames.push_back(name);
        const auto value = detail::boolean(*prefix, name, false, error);
        if (!value) return std::nullopt;
        property("userDefine.prefixKeywords" + QByteArray::number(i), *value ? "1" : "0");
    }
    if (!detail::attributesAllowed(*prefix, prefixNames, error)) return std::nullopt;

    QMap<QString, QByteArray> lists;
    for (const auto& node : keywords->children) {
        const auto name = node.attributes.value(QStringLiteral("name"));
        const bool known = std::any_of(udlWords.cbegin(), udlWords.cend(),
            [&](const UdlWord& word) { return name == QLatin1String(word.name); });
        const QByteArray text = node.text.toUtf8();
        if (!known || lists.contains(name) || !node.children.isEmpty() || text.size() >= 30720
            || !detail::attributesAllowed(node, {QStringLiteral("name")}, error)) {
            detail::fail(error, QStringLiteral("Unknown, duplicate, nested or oversized UDL keyword group."));
            return std::nullopt;
        }
        lists.insert(name, text);
    }
    for (const auto& word : udlWords) {
        const auto text = lists.value(QLatin1String(word.name));
        if (word.property) {
            if ((QByteArray(word.property) == "userDefine.comments" && !validateNumberedTokens(text, 4, error))
                || (QByteArray(word.property) == "userDefine.delimiters" && !validateNumberedTokens(text, 23, error)))
                return std::nullopt;
            property(word.property, text);
        } else {
            auto encoded = encodeQuotedWords(text, error);
            if (!encoded) return std::nullopt;
            language.keywords.push_back({word.keywordIndex, *encoded});
        }
    }
    QSet<int> seenStyles;
    const auto comments = lists.value(QStringLiteral("Comments"));
    auto commentSymbol = [&](const QByteArray& key) {
        for (const auto& token : comments.split(' ')) if (token.startsWith(key)) {
            const auto value = token.mid(2);
            if (value.startsWith("(("))
                language.commentError = QStringLiteral("Grouped UDL comment delimiters are not supported by comment editing.");
            return value;
        }
        return QByteArray();
    };
    language.commentLine = commentSymbol("00");
    language.commentStart = commentSymbol("03");
    language.commentEnd = commentSymbol("04");
    for (const auto& node : styles->children) {
        const auto name = node.attributes.value(QStringLiteral("name"));
        int id = -1;
        for (size_t i = 0; i < udlStyleNames.size(); ++i)
            if (name == QLatin1String(udlStyleNames[i])) id = static_cast<int>(i);
        if (id < 0 || seenStyles.contains(id) || !node.children.isEmpty()
            || !detail::attributesAllowed(node, {
                QStringLiteral("name"), QStringLiteral("styleID"), QStringLiteral("fgColor"), QStringLiteral("bgColor"),
                QStringLiteral("colorStyle"), QStringLiteral("fontName"), QStringLiteral("fontStyle"),
                QStringLiteral("fontSize"), QStringLiteral("nesting")}, error)) {
            detail::fail(error, QStringLiteral("Unknown, duplicate, or malformed UDL style."));
            return std::nullopt;
        }
        auto style = detail::parseStyle(node, id, error);
        if (!style) return std::nullopt;
        seenStyles.insert(id);
        property("userDefine.nesting." + QByteArray::number(id).rightJustified(2, '0'),
            QByteArray::number(style->nesting));
        result.styles.push_back(std::move(*style));
    }
    for (int id = 0; id < SCE_USER_STYLE_TOTAL_STYLES; ++id)
        if (!seenStyles.contains(id))
            property("userDefine.nesting." + QByteArray::number(id).rightJustified(2, '0'), "0");
    return result;
}
} // namespace star::languages
