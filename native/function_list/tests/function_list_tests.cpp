// SPDX-License-Identifier: GPL-3.0-or-later
#include "function_list.h"
#include "scintilla_search.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace star::function_list;

static void require(bool condition, const char* text, int line) {
    if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + text);
}
#define CHECK(condition) require(static_cast<bool>(condition), #condition, __LINE__)

static QByteArray asset(const QString& path) {
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

static Definition definition(const QString& key) {
    Error error;
    auto parsed = DefinitionCatalog::loadEmbedded(key, &error);
    if (!parsed) throw std::runtime_error(error.message.toStdString());
    return *parsed;
}

static Extraction run(const Definition& rules, const QByteArray& source, const Limits& limits = {}) {
    ScintillaSearch backend(source);
    Error error;
    auto parsed = extract(rules, source, std::ref(backend), limits, &error);
    if (!parsed) throw std::runtime_error(error.message.toStdString());
    for (const auto& entry : parsed->entries) {
        CHECK(entry.nameRange.begin >= entry.matchRange.begin);
        CHECK(entry.nameRange.end <= entry.matchRange.end);
        CHECK(entry.nameRange.end <= source.size());
        CHECK(entry.name == QString::fromUtf8(source.mid(entry.nameRange.begin, entry.nameRange.end - entry.nameRange.begin)));
        if (entry.groupNameRange)
            CHECK(entry.groupName == QString::fromUtf8(source.mid(entry.groupNameRange->begin,
                entry.groupNameRange->end - entry.groupNameRange->begin)));
    }
    return *parsed;
}

static QStringList strings(const QJsonArray& array) {
    QStringList result;
    for (const auto& value : array) result.push_back(value.toString());
    return result;
}

static QMap<QString, QStringList> grouped(const Extraction& extraction) {
    QMap<QString, QStringList> groups;
    groups[QString{}] = {};
    for (const auto& entry : extraction.entries) groups[entry.groupName].push_back(entry.name);
    return groups;
}

static void upstreamFixture(const QString& key, const QString& fixture) {
    const QString path = QStringLiteral(":/notepad-star/function-list-tests/PowerEditor/Test/FunctionList/") + fixture;
    const QByteArray source = asset(path + QStringLiteral("/unitTest"));
    const auto expected = QJsonDocument::fromJson(asset(path + QStringLiteral("/unitTest.expected.result"))).object();
    CHECK(!expected.isEmpty());
    QMap<QString, QStringList> expectedGroups;
    expectedGroups[QString{}] = strings(expected.value(QStringLiteral("leaves")).toArray());
    for (const auto& node : expected.value(QStringLiteral("nodes")).toArray()) {
        const auto object = node.toObject();
        expectedGroups[object.value(QStringLiteral("name")).toString()] = strings(object.value(QStringLiteral("leaves")).toArray());
    }
    const auto result = run(definition(key), source);
    const auto actual = grouped(result);
    if (actual != expectedGroups) {
        for (auto it = expectedGroups.cbegin(); it != expectedGroups.cend(); ++it) {
            if (actual.value(it.key()) == it.value()) continue;
            std::cerr << "Fixture " << fixture.toStdString() << " group [" << it.key().toStdString() << "]\n";
            std::cerr << "Expected " << it.value().size() << ", got " << actual.value(it.key()).size() << '\n';
            for (const auto& name : it.value())
                if (!actual.value(it.key()).contains(name)) std::cerr << "  missing: " << name.toStdString() << '\n';
            for (const auto& name : actual.value(it.key()))
                if (!it.value().contains(name)) std::cerr << "  extra: " << name.toStdString() << '\n';
        }
        for (auto it = actual.cbegin(); it != actual.cend(); ++it)
            if (!expectedGroups.contains(it.key())) std::cerr << "Unexpected group: " << it.key().toStdString() << '\n';
        throw std::runtime_error("Upstream fixture mismatch: " + fixture.toStdString());
    }
    qint64 previous = -1;
    for (const auto& entry : result.entries) {
        CHECK(entry.nameRange.begin >= previous);
        previous = entry.nameRange.begin;
    }
    std::cout << "  " << fixture.toStdString() << ": " << result.entries.size()
        << " names, " << result.searchCalls << " bounded searches\n";
}

static Definition simpleRule() {
    Definition rules;
    rules.id = QStringLiteral("bounded_test");
    rules.displayName = QStringLiteral("Bounded test");
    rules.commentExpression = R"(//[^\r\n]*|/\*[\s\S]*?\*/)";
    rules.functions = FunctionRule{R"(func\h+\K[^\r\n]+)", {}, {}};
    return rules;
}

static void catalogAndWhitespace() {
    const QString old = QDir::currentPath();
    CHECK(QDir::setCurrent(QDir::rootPath()));
    const auto keys = DefinitionCatalog::embeddedKeys();
    CHECK(QDir::setCurrent(old));
    CHECK(keys.size() >= 40);
    CHECK(keys.contains(QStringLiteral("javascript.js")));
    CHECK(!DefinitionCatalog::loadEmbedded(QStringLiteral("../cpp")));
    CHECK(!DefinitionCatalog::loadEmbedded(QStringLiteral("html")));
    for (const auto& key : keys) {
        Error error;
        const auto parsed = DefinitionCatalog::loadEmbedded(key, &error);
        if (!parsed) throw std::runtime_error(key.toStdString() + ": " + error.message.toStdString());
        CHECK(parsed->key == key);
    }
    const auto rust = definition(QStringLiteral("rust"));
    CHECK(rust.functions->expression.contains('\n'));
    CHECK(rust.functions->expression.contains('\t'));
    CHECK(!rust.functions->expression.contains('\r'));
    CHECK(rust.functions->expression.contains("(?x)"));
    const auto cpp = definition(QStringLiteral("cpp"));
    CHECK(cpp.classes->functions.nameExpressions.size() == 2);
    CHECK(cpp.functions->nameExpressions.size() == 2);
    CHECK(cpp.classes->nameExpressions.size() == 3);

    const QByteArray xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><NotepadPlus><functionList>"
        "<parser id=\"literal\"><function mainExpr=\"(?x) # first\r\n\t func\\h+\\K[[:alpha:]]+ # end\n\">"
        "<functionName><nameExpr expr=\"[[:alpha:]]+\"/></functionName>"
        "</function></parser></functionList></NotepadPlus>";
    auto parsed = DefinitionCatalog::parseXml(xml);
    CHECK(parsed);
    CHECK(parsed->functions->expression.contains("# first\n\t func"));
    const auto result = run(*parsed, "func visible\n");
    CHECK(result.entries.size() == 1);
    CHECK(result.entries[0].name == QStringLiteral("visible"));
}

static void malformedAndUnsupportedDefinitions() {
    for (const QByteArray& xml : QList<QByteArray>{
        {}, "<NotepadPlus>", "<other/>", "<NotepadPlus/><NotepadPlus/>",
        "<!DOCTYPE NotepadPlus SYSTEM 'https://not-accessed.invalid'><NotepadPlus/>",
        "<!DOCTYPE NotepadPlus [<!ENTITY x SYSTEM 'file:///not-accessed'>]><NotepadPlus>&x;</NotepadPlus>",
        "<NotepadPlus><functionList><parser id='x'><function mainExpr='x'><functionName>"
        "<funcNameExpr expr='x'/></functionName></function></parser></functionList></NotepadPlus>",
        "<NotepadPlus><functionList><parser id='x'><function mainExpr='x'/><function mainExpr='y'/></parser></functionList></NotepadPlus>",
        "<NotepadPlus><functionList><parser id='x'><classRange mainExpr='x' openSymbole='x'>"
        "<function mainExpr='f'/></classRange></parser></functionList></NotepadPlus>",
        "<NotepadPlus><functionList><parser id='x'><unknown/></parser></functionList></NotepadPlus>",
        "<NotepadPlus><functionList><parser id='x'><function mainExpr=''/></parser></functionList></NotepadPlus>",
        "<?xml version='1.0' encoding='ISO-8859-1'?><NotepadPlus/>",
    }) {
        Error error;
        CHECK(!DefinitionCatalog::parseXml(xml, &error));
        CHECK(error.code != ErrorCode::None);
        CHECK(!error.message.isEmpty());
    }
    const QString utf16Text = QStringLiteral("<!DOCTYPE NotepadPlus><NotepadPlus/>");
    QByteArray utf16("\xff\xfe", 2);
    utf16.append(reinterpret_cast<const char*>(utf16Text.utf16()), utf16Text.size() * 2);
    CHECK(!DefinitionCatalog::parseXml(utf16));
    CHECK(!DefinitionCatalog::parseXml(QByteArray(512 * 1024 + 1, ' ')));
    auto forged = simpleRule();
    forged.functions->expression = QByteArray(64 * 1024 + 1, 'a');
    int calls = 0;
    CHECK(!extract(forged, "func one\n", [&](const SearchRequest&) { ++calls; return SearchReply{}; }));
    CHECK(calls == 0);
}

static void utf8BoundariesAndDeterminism() {
    const QByteArray source = QString::fromUtf8("// 😀 heading\r\nfunc café\r\nfunc same\nfunc same").toUtf8();
    const auto first = run(simpleRule(), source);
    const auto second = run(simpleRule(), source);
    CHECK(first.entries.size() == 3);
    CHECK(first.searchCalls == second.searchCalls);
    CHECK(grouped(first) == grouped(second));
    CHECK(first.entries[0].name == QStringLiteral("café"));
    CHECK(first.entries[0].nameRange.begin == source.indexOf(QStringLiteral("café").toUtf8()));
    CHECK(first.entries[0].nameRange.end - first.entries[0].nameRange.begin == 5);
    CHECK(first.entries[0].line == 1);
    CHECK(first.entries[0].columnBytes == 5);
    CHECK(first.entries[2].nameRange.end == source.size());
    const auto adjacency = run(simpleRule(), "func before/*comment*/func after");
    CHECK(adjacency.entries.size() == 2);
    CHECK(adjacency.entries[0].name == QStringLiteral("before"));
    CHECK(adjacency.entries[1].name == QStringLiteral("after"));
    CHECK(run(simpleRule(), {}).entries.isEmpty());
}

static void backendContractAndBounds() {
    const auto rules = simpleRule();
    const QByteArray source = "func one\nfunc two\n";
    for (const auto match : {ByteRange{-1, 2}, ByteRange{0, 0}, ByteRange{3, 2}, ByteRange{0, 999}}) {
        Error error;
        CHECK(!extract(rules, source, [=](const SearchRequest&) { return SearchReply{SearchStatus::Found, match, {}}; }, {}, &error));
        CHECK(error.code == ErrorCode::InvalidMatch);
    }
    Error error;
    CHECK(!extract(rules, QByteArray("\xc3\xa9", 2), [](const SearchRequest&) {
        return SearchReply{SearchStatus::Found, {1, 2}, {}};
    }, {}, &error));
    CHECK(error.code == ErrorCode::InvalidMatch);
    CHECK(!extract(rules, QByteArray("\xff", 1), [](const SearchRequest&) { return SearchReply{}; }, {}, &error));
    CHECK(error.code == ErrorCode::InvalidInput);
    CHECK(!extract(rules, source, [](const SearchRequest&) -> SearchReply { throw std::runtime_error("test"); }, {}, &error));
    CHECK(error.code == ErrorCode::BackendFailure);
    auto invalidRegex = rules;
    invalidRegex.functions->expression = "[";
    ScintillaSearch regexBackend(source);
    CHECK(!extract(invalidRegex, source, std::ref(regexBackend), {}, &error));
    CHECK(error.code == ErrorCode::BackendFailure);
    for (int budget = 0; budget < 6; ++budget) {
        Limits limits;
        if (budget == 0) limits.maxResults = 1;
        if (budget == 1) limits.maxSearchCalls = 1;
        if (budget == 2) limits.maxMatchBytes = 2;
        if (budget == 3) limits.maxNameBytes = 2;
        if (budget == 4) limits.maxTotalNameBytes = 4;
        if (budget == 5) limits.maxDocumentBytes = 2;
        ScintillaSearch backend(source);
        CHECK(!extract(rules, source, std::ref(backend), limits, &error));
        CHECK(error.code == (budget == 5 ? ErrorCode::InvalidInput : ErrorCode::LimitExceeded));
    }
    ScintillaSearch backend(source);
    auto stopped = extract(rules, source, [&](const SearchRequest& request) {
        CHECK(request.flags == FunctionSearchFlags);
        if (request.range.begin >= 8) return SearchReply{SearchStatus::Failed, {}, QStringLiteral("cancelled")};
        return backend(request);
    }, {}, &error);
    CHECK(!stopped);
    CHECK(error.code == ErrorCode::BackendFailure);
}

static void groupsAndDelimiterBounds() {
    Definition rules = simpleRule();
    rules.classes = ClassRule{
        R"(TYPE\h+\w+\h*\{)", R"(\{)", R"(\})", {R"((?<=TYPE )\w+)"},
        FunctionRule{R"(func\h+\K\w+)", {}, {}}};
    const auto result = run(rules, "TYPE Named {\n // }\n func member\n}\nfunc free\n");
    CHECK(result.entries.size() == 2);
    CHECK(result.entries[0].groupName == QStringLiteral("Named"));
    CHECK(result.entries[0].groupNameRange);
    CHECK(result.entries[1].groupName.isEmpty());
    CHECK(result.entries[0].name == QStringLiteral("member"));
    for (const auto& source : QList<QByteArray>{
        "TYPE Broken { func missing\n", "TYPE Outer { TYPE Inner { func nested } }\n"}) {
        ScintillaSearch backend(source);
        Error error;
        CHECK(!extract(rules, source, std::ref(backend), {}, &error));
        CHECK(error.code == ErrorCode::IncompleteClass || error.code == ErrorCode::UnsupportedDefinition);
    }
    const QByteArray deep = "TYPE Deep {{{}}}\n";
    ScintillaSearch backend(deep);
    Limits limits;
    limits.maxDelimiterDepth = 2;
    Error error;
    CHECK(!extract(rules, deep, std::ref(backend), limits, &error));
    CHECK(error.code == ErrorCode::LimitExceeded);
    const QByteArray classes = "TYPE One { func a }\nTYPE Two { func b }\n";
    ScintillaSearch classBackend(classes);
    limits = {};
    limits.maxClassRanges = 1;
    CHECK(!extract(rules, classes, std::ref(classBackend), limits, &error));
    CHECK(error.code == ErrorCode::LimitExceeded);
    const QByteArray comments = "// one\n// two\nfunc last\n";
    ScintillaSearch commentBackend(comments);
    limits = {};
    limits.maxCommentRanges = 1;
    CHECK(!extract(rules, comments, std::ref(commentBackend), limits, &error));
    CHECK(error.code == ErrorCode::LimitExceeded);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::cout << std::unitbuf;
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"catalog / original attribute whitespace", catalogAndWhitespace},
        {"Rust upstream fixture", [] { upstreamFixture(QStringLiteral("rust"), QStringLiteral("rust")); }},
        {"Python upstream fixtures", [] {
            upstreamFixture(QStringLiteral("python"), QStringLiteral("python"));
            upstreamFixture(QStringLiteral("python"), QStringLiteral("python/baddeftest"));
            upstreamFixture(QStringLiteral("python"), QStringLiteral("python/function_space_test"));
        }},
        {"C++ upstream fixtures", [] {
            upstreamFixture(QStringLiteral("cpp"), QStringLiteral("cpp"));
            upstreamFixture(QStringLiteral("cpp"), QStringLiteral("cpp/1"));
        }},
        {"XML upstream fixture", [] { upstreamFixture(QStringLiteral("xml"), QStringLiteral("xml")); }},
        {"malformed / unsupported definitions", malformedAndUnsupportedDefinitions},
        {"UTF-8 offsets / boundaries / determinism", utf8BoundariesAndDeterminism},
        {"backend contract / budgets", backendContractAndBounds},
        {"flat groups / delimiter bounds", groupsAndDelimiterBounds},
    };
    int failures = 0;
    for (const auto& test : tests) {
        std::cout << "RUN " << test.first << '\n';
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& failure) { ++failures; std::cerr << "FAIL " << test.first << ": " << failure.what() << '\n'; }
    }
    return failures ? 1 : 0;
}
