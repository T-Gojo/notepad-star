// SPDX-License-Identifier: GPL-3.0-or-later
#include "internal.h"
#include <QDir>
#include <QFile>
#include <QMap>
#include <QResource>
#include <QStringDecoder>
#include <QXmlStreamReader>

static void initializeFunctionListResources() {
    static const bool ready = [] {
        Q_INIT_RESOURCE(star_function_list_assets);
        return true;
    }();
    Q_UNUSED(ready);
}

namespace star::function_list {
namespace detail {
void fail(Error* error, ErrorCode code, const QString& message) {
    if (error) *error = {code, message.left(512)};
}

bool validUtf8(const QByteArray& bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder(bytes);
    Q_UNUSED(decoded);
    return !decoder.hasError();
}

bool validateDefinition(const Definition& value, Error* error) {
    auto expression = [&](const QByteArray& text, bool required) {
        if ((required && text.isEmpty()) || text.size() > MaxExpressionBytes
            || text.contains('\0') || !validUtf8(text)) {
            fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("Missing, oversized, NUL-containing or non-UTF-8 expression."));
            return false;
        }
        return true;
    };
    auto chain = [&](const QList<QByteArray>& list) {
        if (list.size() > 16) {
            fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("At most 16 successive name expressions are supported."));
            return false;
        }
        for (const auto& text : list) if (!expression(text, true)) return false;
        return true;
    };
    auto function = [&](const FunctionRule& rule) {
        return expression(rule.expression, true) && chain(rule.nameExpressions) && chain(rule.groupExpressions);
    };
    if (value.id.isEmpty() || value.id.size() > 256 || value.displayName.size() > 256
        || (!value.functions && !value.classes)) {
        fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("Definition needs an ID and at least one supported function/class rule."));
        return false;
    }
    if (!expression(value.commentExpression, false)) return false;
    if (value.functions && !function(*value.functions)) return false;
    if (value.classes) {
        const auto& rule = *value.classes;
        if (rule.openExpression.isEmpty() != rule.closeExpression.isEmpty()) {
            fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("Class opening and closing expressions must be supplied together."));
            return false;
        }
        if (!expression(rule.expression, true) || !expression(rule.openExpression, false)
            || !expression(rule.closeExpression, false) || !chain(rule.nameExpressions)
            || !function(rule.functions)) return false;
        if (rule.openExpression.size() + rule.closeExpression.size() + 3 > MaxExpressionBytes) {
            fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("Combined delimiter expression exceeds the limit."));
            return false;
        }
    }
    return true;
}
}

namespace {
struct Node {
    QString name;
    QMap<QString, QString> attributes;
    QList<Node> children;
};

bool checkXml(const QByteArray& bytes, Error* error) {
    QXmlStreamReader reader(bytes);
    int depth = 0;
    int count = 0;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartDocument && !reader.documentEncoding().isEmpty()
            && reader.documentEncoding().compare(QLatin1String("UTF-8"), Qt::CaseInsensitive) != 0
            && reader.documentEncoding().compare(QLatin1String("UTF8"), Qt::CaseInsensitive) != 0) {
            detail::fail(error, ErrorCode::InvalidXml, QStringLiteral("Only UTF-8 definition XML is supported."));
            return false;
        }
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference) {
            detail::fail(error, ErrorCode::InvalidXml, QStringLiteral("DTD and non-predefined entities are forbidden."));
            return false;
        }
        if (token == QXmlStreamReader::StartElement) {
            if (++depth > 16 || ++count > 512 || reader.attributes().size() > 16
                || !reader.namespaceDeclarations().isEmpty() || !reader.namespaceUri().isEmpty()) {
                detail::fail(error, ErrorCode::InvalidXml, QStringLiteral("XML exceeds structural limits or uses unsupported namespaces."));
                return false;
            }
        } else if (token == QXmlStreamReader::EndElement) --depth;
    }
    if (reader.hasError()) {
        detail::fail(error, ErrorCode::InvalidXml, QStringLiteral("Malformed XML at line %1.").arg(reader.lineNumber()));
        return false;
    }
    return true;
}

QByteArray preserveAttributeWhitespace(const QByteArray& bytes) {
    // NppXml::loadFileFunctionParser disables attribute whitespace conversion.
    // Qt normalizes literal attribute whitespace, so encode it as character
    // references AFTER validation. Otherwise (?x) # comments eat entire regexes.
    QByteArray output;
    bool inTag = false;
    char quote = 0;
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        if (!inTag && bytes[i] == '<') {
            QByteArray closing;
            if (bytes.mid(i, 4) == "<!--") closing = "-->";
            else if (bytes.mid(i, 9) == "<![CDATA[") closing = "]]>";
            else if (bytes.mid(i, 2) == "<?") closing = "?>";
            if (!closing.isEmpty()) {
                const auto end = bytes.indexOf(closing, i) + closing.size();
                output += bytes.mid(i, end - i);
                i = end - 1;
                continue;
            }
            inTag = true;
        }
        const char ch = bytes[i];
        if (inTag && quote) {
            if (ch == quote) quote = 0;
            else if (ch == '\t') { output += "&#9;"; continue; }
            else if (ch == '\n' || ch == '\r') {
                output += "&#10;";
                if (ch == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n') ++i;
                continue;
            }
        } else if (inTag) {
            if (ch == '\'' || ch == '"') quote = ch;
            else if (ch == '>') inTag = false;
        }
        output += ch;
    }
    return output;
}

std::optional<Node> tree(const QByteArray& original, Error* error) {
    if (original.isEmpty() || original.size() > detail::MaxDefinitionBytes || !detail::validUtf8(original)) {
        detail::fail(error, ErrorCode::InvalidXml, QStringLiteral("Definition must be nonempty UTF-8 XML of at most 512 KiB."));
        return std::nullopt;
    }
    if (!checkXml(original, error)) return std::nullopt;
    const auto bytes = preserveAttributeWhitespace(original);
    QXmlStreamReader reader(bytes);
    QList<Node> stack;
    std::optional<Node> root;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            Node node;
            node.name = reader.name().toString();
            for (const auto& attr : reader.attributes()) {
                if (!attr.namespaceUri().isEmpty()) {
                    detail::fail(error, ErrorCode::InvalidXml, QStringLiteral("Namespaced attributes are unsupported."));
                    return std::nullopt;
                }
                node.attributes.insert(attr.name().toString(), attr.value().toString());
            }
            stack.push_back(std::move(node));
        } else if (token == QXmlStreamReader::EndElement) {
            auto node = stack.takeLast();
            if (stack.isEmpty()) root = std::move(node);
            else stack.last().children.push_back(std::move(node));
        } else if (token == QXmlStreamReader::Characters && !reader.isWhitespace()) {
            detail::fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("Parser definitions must use expression attributes, not element text."));
            return std::nullopt;
        }
    }
    if (reader.hasError() || !root || root->name != QStringLiteral("NotepadPlus")) {
        detail::fail(error, ErrorCode::InvalidXml, QStringLiteral("Malformed parser XML or missing NotepadPlus root."));
        return std::nullopt;
    }
    return root;
}

bool shape(const Node& node, const QStringList& attributes, const QStringList& children, Error* error) {
    for (auto it = node.attributes.cbegin(); it != node.attributes.cend(); ++it)
        if (!attributes.contains(it.key())) {
            detail::fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("Unsupported attribute %1 on %2.").arg(it.key(), node.name));
            return false;
        }
    for (const auto& child : node.children)
        if (!children.contains(child.name)) {
            detail::fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("Unsupported element %1 in %2.").arg(child.name, node.name));
            return false;
        }
    return true;
}

const Node* child(const Node& node, const QString& name, bool required, Error* error) {
    const Node* found = nullptr;
    for (const auto& candidate : node.children) {
        if (candidate.name != name) continue;
        if (found) {
            detail::fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("Multiple %1 elements are unsupported.").arg(name));
            return nullptr;
        }
        found = &candidate;
    }
    if (!found && required)
        detail::fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("Missing %1 element.").arg(name));
    return found;
}

bool expressions(const Node& parent, const QString& container, const QString& tag,
    QList<QByteArray>& output, Error* error) {
    const auto* node = child(parent, container, false, error);
    if (error->code != ErrorCode::None) return false;
    if (!node) return true;
    if (!shape(*node, {}, {tag}, error)) return false;
    if (node->children.isEmpty()) {
        detail::fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("Empty name-expression containers are unsupported."));
        return false;
    }
    for (const auto& expression : node->children) {
        if (!shape(expression, {QStringLiteral("expr")}, {}, error)) return false;
        output.push_back(expression.attributes.value(QStringLiteral("expr")).toUtf8());
    }
    return true;
}

std::optional<FunctionRule> function(const Node& node, bool member, Error* error) {
    if (!shape(node, {QStringLiteral("mainExpr"), QStringLiteral("displayMode")},
        member ? QStringList{QStringLiteral("functionName")}
               : QStringList{QStringLiteral("functionName"), QStringLiteral("className")}, error))
        return std::nullopt;
    FunctionRule result;
    result.expression = node.attributes.value(QStringLiteral("mainExpr")).toUtf8();
    if (!expressions(node, QStringLiteral("functionName"),
        member ? QStringLiteral("funcNameExpr") : QStringLiteral("nameExpr"), result.nameExpressions, error))
        return std::nullopt;
    if (!member && !expressions(node, QStringLiteral("className"), QStringLiteral("nameExpr"), result.groupExpressions, error))
        return std::nullopt;
    return result;
}
}

QStringList DefinitionCatalog::embeddedKeys() {
    initializeFunctionListResources();
    const QDir directory(QStringLiteral(":/notepad-star/function-list/PowerEditor/installer/functionList"));
    auto files = directory.entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name);
    files.removeAll(QStringLiteral("overrideMap.xml"));
    for (auto& file : files) file.chop(4);
    return files;
}

std::optional<Definition> DefinitionCatalog::loadEmbedded(const QString& key, Error* error) {
    if (error) *error = {};
    if (!embeddedKeys().contains(key)) {
        detail::fail(error, ErrorCode::UnsupportedDefinition, QStringLiteral("No embedded definition with this exact language/definition key."));
        return std::nullopt;
    }
    QFile file(QStringLiteral(":/notepad-star/function-list/PowerEditor/installer/functionList/") + key + QStringLiteral(".xml"));
    if (!file.open(QIODevice::ReadOnly)) {
        detail::fail(error, ErrorCode::InvalidXml, QStringLiteral("Could not read embedded function-list definition."));
        return std::nullopt;
    }
    auto result = parseXml(file.read(detail::MaxDefinitionBytes + 1), error);
    if (result) result->key = key;
    return result;
}

std::optional<Definition> DefinitionCatalog::parseXml(const QByteArray& xml, Error* error) {
    Error local;
    if (!error) error = &local;
    *error = {};
    const auto root = tree(xml, error);
    if (!root || !shape(*root, {}, {QStringLiteral("functionList")}, error)) return std::nullopt;
    const auto* list = child(*root, QStringLiteral("functionList"), true, error);
    if (!list || !shape(*list, {}, {QStringLiteral("parser")}, error)) return std::nullopt;
    const auto* parser = child(*list, QStringLiteral("parser"), true, error);
    if (!parser || !shape(*parser,
        {QStringLiteral("id"), QStringLiteral("displayName"), QStringLiteral("commentExpr")},
        {QStringLiteral("function"), QStringLiteral("classRange")}, error)) return std::nullopt;
    Definition result;
    result.id = parser->attributes.value(QStringLiteral("id"));
    result.displayName = parser->attributes.value(QStringLiteral("displayName"), result.id);
    result.commentExpression = parser->attributes.value(QStringLiteral("commentExpr")).toUtf8();
    const auto* unit = child(*parser, QStringLiteral("function"), false, error);
    const auto* zone = child(*parser, QStringLiteral("classRange"), false, error);
    if (error->code != ErrorCode::None) return std::nullopt;
    if (unit) {
        result.functions = function(*unit, false, error);
        if (!result.functions) return std::nullopt;
    }
    if (zone) {
        if (!shape(*zone, {QStringLiteral("mainExpr"), QStringLiteral("openSymbole"),
            QStringLiteral("closeSymbole"), QStringLiteral("displayMode")},
            {QStringLiteral("className"), QStringLiteral("function")}, error)) return std::nullopt;
        ClassRule rule;
        rule.expression = zone->attributes.value(QStringLiteral("mainExpr")).toUtf8();
        rule.openExpression = zone->attributes.value(QStringLiteral("openSymbole")).toUtf8();
        rule.closeExpression = zone->attributes.value(QStringLiteral("closeSymbole")).toUtf8();
        if (!expressions(*zone, QStringLiteral("className"), QStringLiteral("nameExpr"), rule.nameExpressions, error))
            return std::nullopt;
        const auto* member = child(*zone, QStringLiteral("function"), true, error);
        if (!member) return std::nullopt;
        const auto ruleFunction = function(*member, true, error);
        if (!ruleFunction) return std::nullopt;
        rule.functions = *ruleFunction;
        result.classes = std::move(rule);
    }
    if (!detail::validateDefinition(result, error)) return std::nullopt;
    return result;
}
} // namespace star::function_list
