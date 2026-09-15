// SPDX-License-Identifier: GPL-3.0-or-later
#include "language_catalog.h"
#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"
#include "LexerModule.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

extern const Lexilla::LexerModule lmCPP;
extern const Lexilla::LexerModule lmPython;
extern const Lexilla::LexerModule lmRust;
extern const Lexilla::LexerModule lmUserDefine;

using namespace star::languages;

static void require(bool value, const char* expression, int line) {
    if (!value) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(value) require(static_cast<bool>(value), #value, __LINE__)

static LanguageCatalog catalog() {
    QString error;
    auto result = LanguageCatalog::load(&error);
    if (!result) throw std::runtime_error(error.toStdString());
    return std::move(*result);
}

static QByteArray asset(const QString& path) {
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

static QByteArray udlAsset(bool dark = false) {
    AssetPaths::embedded();
    return asset(QStringLiteral(":/notepad-star/languages/PowerEditor/bin/userDefineLangs/")
        + (dark ? QStringLiteral("markdown._preinstalled_DM.udl.xml") : QStringLiteral("markdown._preinstalled.udl.xml")));
}

static QByteArray words(const Language& language, int index) {
    for (const auto& set : language.keywords) if (set.index == index) return set.text;
    return {};
}

static QByteArray property(const Language& language, const QByteArray& name) {
    for (const auto& item : language.properties) if (item.name == name) return item.value;
    return {};
}

static Style findStyle(const QList<Style>& styles, int id) {
    for (const auto& style : styles) if (style.id == id) return style;
    throw std::runtime_error("Missing style " + std::to_string(id));
}

class OwnedXmlFile {
public:
    QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/test-input-")
        + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".xml");
    explicit OwnedXmlFile(const QByteArray& bytes) {
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        CHECK(file.write(bytes) == bytes.size());
    }
    ~OwnedXmlFile() { QFile::remove(path); }
};

// An ASCII in-memory IDocument test double; no editor, windows or documents are opened.
class TestDocument final : public Scintilla::IDocument {
public:
    explicit TestDocument(QByteArray text) : text_(std::move(text)), styles_(static_cast<size_t>(text_.size()), 0) {
        starts_.push_back(0);
        for (qsizetype i = 0; i < text_.size(); ++i) if (text_[i] == '\n') starts_.push_back(i + 1);
    }
    int SCI_METHOD Version() const override { return Scintilla::dvRelease4; }
    void SCI_METHOD SetErrorStatus(int status) override { error_ = status; }
    Sci_Position SCI_METHOD Length() const override { return text_.size(); }
    void SCI_METHOD GetCharRange(char* out, Sci_Position position, Sci_Position length) const override {
        CHECK(position >= 0 && length >= 0 && position + length <= Length());
        std::memcpy(out, text_.constData() + position, static_cast<size_t>(length));
    }
    char SCI_METHOD StyleAt(Sci_Position position) const override {
        return position < 0 || position >= Length() ? 0 : styles_[static_cast<size_t>(position)];
    }
    Sci_Position SCI_METHOD LineFromPosition(Sci_Position position) const override {
        return std::max<Sci_Position>(0, std::upper_bound(starts_.begin(), starts_.end(), position) - starts_.begin() - 1);
    }
    Sci_Position SCI_METHOD LineStart(Sci_Position line) const override {
        return line < 0 ? 0 : line >= static_cast<Sci_Position>(starts_.size()) ? Length() : starts_[static_cast<size_t>(line)];
    }
    int SCI_METHOD GetLevel(Sci_Position line) const override {
        const auto it = levels_.find(line);
        return it == levels_.end() ? SC_FOLDLEVELBASE : it->second;
    }
    int SCI_METHOD SetLevel(Sci_Position line, int value) override {
        const int old = GetLevel(line); levels_[line] = value; return old;
    }
    int SCI_METHOD GetLineState(Sci_Position line) const override {
        const auto it = states_.find(line); return it == states_.end() ? 0 : it->second;
    }
    int SCI_METHOD SetLineState(Sci_Position line, int state) override {
        const int old = GetLineState(line); states_[line] = state; return old;
    }
    void SCI_METHOD StartStyling(Sci_Position position) override { position_ = position; }
    bool SCI_METHOD SetStyleFor(Sci_Position length, char style) override {
        CHECK(position_ >= 0 && length >= 0 && position_ + length <= Length());
        std::fill_n(styles_.begin() + position_, length, style); position_ += length; return true;
    }
    bool SCI_METHOD SetStyles(Sci_Position length, const char* styles) override {
        CHECK(position_ >= 0 && length >= 0 && position_ + length <= Length());
        std::copy_n(styles, length, styles_.begin() + position_); position_ += length; return true;
    }
    void SCI_METHOD DecorationSetCurrentIndicator(int) override {}
    void SCI_METHOD DecorationFillRange(Sci_Position, int, Sci_Position) override {}
    void SCI_METHOD ChangeLexerState(Sci_Position, Sci_Position) override {}
    int SCI_METHOD CodePage() const override { return SC_CP_UTF8; }
    bool SCI_METHOD IsDBCSLeadByte(char) const override { return false; }
    const char* SCI_METHOD BufferPointer() override { return text_.constData(); }
    int SCI_METHOD GetLineIndentation(Sci_Position line) override {
        int spaces = 0;
        for (auto i = LineStart(line); i < Length() && text_[i] == ' '; ++i) ++spaces;
        return spaces;
    }
    Sci_Position SCI_METHOD LineEnd(Sci_Position line) const override {
        auto end = LineStart(line + 1);
        while (end > LineStart(line) && (text_[end - 1] == '\n' || text_[end - 1] == '\r')) --end;
        return end;
    }
    Sci_Position SCI_METHOD GetRelativePosition(Sci_Position start, Sci_Position offset) const override {
        return std::clamp<Sci_Position>(start + offset, 0, Length());
    }
    int SCI_METHOD GetCharacterAndWidth(Sci_Position position, Sci_Position* width) const override {
        if (width) *width = 1;
        return position >= 0 && position < Length() ? static_cast<unsigned char>(text_[position]) : 0;
    }
    void verify() const { CHECK(error_ == 0); }
private:
    QByteArray text_;
    std::vector<char> styles_;
    std::vector<Sci_Position> starts_;
    std::map<Sci_Position, int> levels_;
    std::map<Sci_Position, int> states_;
    Sci_Position position_ = 0;
    int error_ = 0;
};

static TestDocument lex(const Lexilla::LexerModule& module, const Language& language, const QByteArray& source) {
    std::unique_ptr<Scintilla::ILexer5, std::function<void(Scintilla::ILexer5*)>>
        lexer(module.Create(), [](auto* value) { if (value) value->Release(); });
    CHECK(lexer);
    CHECK(QByteArray(lexer->GetName()) == language.engine);
    for (const auto& item : language.properties) lexer->PropertySet(item.name.constData(), item.value.constData());
    for (const auto& item : language.keywords) lexer->WordListSet(item.index, item.text.constData());
    TestDocument document(source);
    lexer->Lex(0, document.Length(), 0, &document);
    document.verify();
    return document;
}

static void aliasesAndExtensions() {
    const auto data = catalog();
    CHECK(data.languages().size() > 80);
    CHECK(data.language(QStringLiteral("C++"))->id == QStringLiteral("cpp"));
    CHECK(data.language(QStringLiteral("python3"))->engine == "python");
    CHECK(data.language(QStringLiteral("rs"))->engine == "rust");
    const std::vector<std::pair<QString, QByteArray>> cases{
        {QStringLiteral("project.CXX"), "cpp"}, {QStringLiteral("header.HPP"), "cpp"},
        {QStringLiteral("widget.cs"), "cpp"}, {QStringLiteral("Main.java"), "cpp"},
        {QStringLiteral("script.pyw"), "python"}, {QStringLiteral("types.pyi"), "python"},
        {QStringLiteral("main.RS"), "rust"}, {QStringLiteral("site.html"), "hypertext"},
        {QStringLiteral("site.php"), "hypertext"}, {QStringLiteral("project.csproj"), "xml"},
        {QStringLiteral("config.ini"), "props"}, {QStringLiteral("settings.properties"), "props"},
        {QStringLiteral("script.tsx"), "cpp"}, {QStringLiteral("legacy.f77"), "f77"},
        {QStringLiteral("file.nim"), "nimrod"}, {QStringLiteral("file.ps"), "ps"},
        {QStringLiteral("auto.au3"), "au3"}, {QStringLiteral("file.scm"), "lisp"},
        {QStringLiteral("file.mm"), "objc"}, {QStringLiteral("file.cob"), "COBOL"},
        {QStringLiteral("config.jsonc"), "json"}, {QStringLiteral("data.toml"), "toml"},
        {QStringLiteral("C:\\src\\CMakeLists.txt"), "cmake"}, {QStringLiteral("/src/GNUmakefile"), "makefile"},
        {QStringLiteral("SConstruct"), "python"}, {QStringLiteral(".bashrc"), "bash"},
    };
    for (const auto& item : cases) {
        const auto* detected = data.detectFileName(item.first);
        if (!detected || detected->engine != item.second)
            throw std::runtime_error("Wrong mapping for " + item.first.toStdString());
    }
    CHECK(data.forExtension(QStringLiteral(".RS"))->id == QStringLiteral("rust"));
    CHECK(!data.detectFileName(QStringLiteral("unknown.not-a-language")));
    for (const auto& language : data.languages()) CHECK(LanguageCatalog::supportsEngine(language.engine));
}

static void keywordMappingsAndActualLexers() {
    const auto data = catalog();
    const auto& cpp = *data.language(QStringLiteral("cpp"));
    CHECK(cpp.commentLine == "//" && cpp.commentStart == "/*" && cpp.commentEnd == "*/");
    CHECK(words(cpp, 0).split(' ').contains("for"));
    CHECK(words(cpp, 1).split(' ').contains("int"));
    CHECK(words(cpp, 2).split(' ').contains("param"));
    CHECK(!words(cpp, 2).split(' ').contains("int"));
    const auto& python = *data.language(QStringLiteral("python"));
    CHECK(words(python, 0).split(' ').contains("def"));
    CHECK(words(python, 1).split(' ').contains("print"));
    const auto& rust = *data.language(QStringLiteral("rust"));
    CHECK(words(rust, 0).split(' ').contains("fn"));
    CHECK(words(rust, 1).split(' ').contains("i32"));
    CHECK(words(rust, 2) == "Self");
    CHECK(words(*data.language(QStringLiteral("json5")), 0) == words(*data.language(QStringLiteral("json")), 0));
    CHECK(property(*data.language(QStringLiteral("json5")), "lexer.json.allow.comments") == "1");
    CHECK(words(*data.language(QStringLiteral("xml")), 5).contains("DOCTYPE"));
    CHECK(words(*data.language(QStringLiteral("html")), 5).contains("DOCTYPE"));
    CHECK(!words(*data.language(QStringLiteral("javascript.js")), 3).isEmpty());

    auto cppDoc = lex(lmCPP, cpp, "int main() { return 0; }\n");
    CHECK(cppDoc.StyleAt(0) == SCE_C_WORD2);
    auto pythonDoc = lex(lmPython, python, "def run():\n    print(1)\n");
    CHECK(pythonDoc.StyleAt(0) == SCE_P_WORD);
    CHECK(pythonDoc.StyleAt(15) == SCE_P_WORD2);
    const QByteArray rustText = "fn main() -> i32 { Self; }\n";
    auto rustDoc = lex(lmRust, rust, rustText);
    CHECK(rustDoc.StyleAt(0) == SCE_RUST_WORD);
    CHECK(rustDoc.StyleAt(rustText.indexOf("i32")) == SCE_RUST_WORD2);
    CHECK(rustDoc.StyleAt(rustText.indexOf("Self")) == SCE_RUST_WORD3);
}

static void themesAndCompletions() {
    const auto data = catalog();
    const auto light = findStyle(data.styles(QStringLiteral("cpp"), Theme::Light), 5);
    const auto dark = findStyle(data.styles(QStringLiteral("cpp"), Theme::Dark), 5);
    CHECK(light.foreground == QColor(QStringLiteral("#0000FF")));
    CHECK(dark.foreground == QColor(QStringLiteral("#DFC47D")));
    CHECK(light.fontFlags == 1);
    CHECK(data.defaultStyle(Theme::Light)->id == 32);
    CHECK(data.defaultStyle(Theme::Dark)->background == QColor(QStringLiteral("#3F3F3F")));
    CHECK(findStyle(data.styles(QStringLiteral("python"), Theme::Dark), 1).fontFlags == 3);
    CHECK(!data.styles(QStringLiteral("json5"), Theme::Dark).isEmpty());
    CHECK(findStyle(data.styles(QStringLiteral("html"), Theme::Light), SCE_HPHP_WORD).id == SCE_HPHP_WORD);
    for (const auto& id : {QStringLiteral("cpp"), QStringLiteral("python"), QStringLiteral("rust"), QStringLiteral("js")}) {
        QString error;
        auto completions = data.completions(id, &error);
        CHECK(completions);
        CHECK(!completions->entries.isEmpty());
    }
    const auto cpp = *data.completions(QStringLiteral("cpp"));
    bool found = false;
    for (const auto& entry : cpp.entries) {
        if (entry.name != QStringLiteral("abort")) continue;
        CHECK(entry.function);
        CHECK(entry.overloads.first().signature == QStringLiteral("void abort(void)"));
        found = true;
    }
    CHECK(found);
    const auto python = *data.completions(QStringLiteral("python"));
    CHECK(python.additionalWordCharacters == QStringLiteral("."));
    const QDir directory(AssetPaths::embedded().completionsDirectory);
    for (const auto& file : directory.entryList({QStringLiteral("*.xml")}, QDir::Files)) {
        QString error;
        auto parsed = LanguageCatalog::parseCompletionXml(asset(directory.filePath(file)), &error);
        if (!parsed) throw std::runtime_error(file.toStdString() + ": " + error.toStdString());
    }
}

static void malformedXmlAndDtdAreRejected() {
    const QList<QByteArray> invalid{
        {}, "<NotepadPlus>", "<other/>", "<NotepadPlus/><NotepadPlus/>",
        "<!DOCTYPE NotepadPlus [<!ENTITY secret SYSTEM 'file:///not-read'>]><NotepadPlus>&secret;</NotepadPlus>",
        "<!DOCTYPE NotepadPlus SYSTEM 'https://not-read.invalid'><NotepadPlus/>",
        "<!DOCTYPE NotepadPlus [<!ENTITY a 'aaaa'><!ENTITY b '&a;&a;'>]><NotepadPlus>&b;</NotepadPlus>",
    };
    for (const auto& xml : invalid) {
        CHECK(!LanguageCatalog::parseThemeXml(xml));
        CHECK(!LanguageCatalog::parseCompletionXml(xml));
        CHECK(!LanguageCatalog::parseUdlXml(xml, 1, 1));
        OwnedXmlFile file(xml);
        auto paths = AssetPaths::embedded();
        paths.languages = file.path;
        CHECK(!LanguageCatalog::load(paths));
    }
    const QString utf16Text = QStringLiteral("<!DOCTYPE NotepadPlus><NotepadPlus/>");
    QByteArray utf16("\xff\xfe", 2);
    utf16.append(reinterpret_cast<const char*>(utf16Text.utf16()), utf16Text.size() * 2);
    CHECK(!LanguageCatalog::parseCompletionXml(utf16));
    const QByteArray oversized(8 * 1024 * 1024 + 1, ' ');
    CHECK(!LanguageCatalog::parseThemeXml(oversized));
    const auto paths = AssetPaths::embedded();
    auto theme = asset(paths.lightTheme);
    theme.replace("fgColor=\"0000FF\"", "fgColor=\"invalid\"");
    CHECK(!LanguageCatalog::parseThemeXml(theme));
    auto relative = paths;
    relative.languages = QStringLiteral("langs.model.xml");
    CHECK(!LanguageCatalog::load(relative));
}

static void xmlKeywordOrderDoesNotChooseTheIndex() {
    const auto paths = AssetPaths::embedded();
    auto xml = asset(paths.languages);
    const auto start = xml.indexOf("<Language name=\"rust\"");
    const auto end = xml.indexOf("</Language>", start);
    CHECK(start >= 0 && end > start);
    auto rust = xml.mid(start, end - start);
    const auto firstStart = rust.indexOf("<Keywords name=\"instre1\"");
    const auto firstEnd = rust.indexOf("</Keywords>", firstStart) + QByteArray("</Keywords>").size();
    const auto first = rust.mid(firstStart, firstEnd - firstStart);
    rust.remove(firstStart, first.size());
    rust += first;
    rust.replace("name=\"instre1\"", "name=\"0\"");
    xml.replace(start, end - start, rust);
    OwnedXmlFile file(xml);
    auto customPaths = paths;
    customPaths.languages = file.path;
    auto custom = LanguageCatalog::load(customPaths);
    CHECK(custom);
    const auto* language = custom->language(QStringLiteral("rust"));
    CHECK(words(*language, 0).split(' ').contains("fn"));
    CHECK(words(*language, 2) == "Self");
    OwnedXmlFile duplicate(
        "<NotepadPlus><Languages><Language name=\"cpp\"><Keywords name=\"instre1\">if</Keywords>"
        "<Keywords name=\"0\">else</Keywords></Language></Languages></NotepadPlus>");
    customPaths.languages = duplicate.path;
    CHECK(!LanguageCatalog::load(customPaths));
}

static void udlProfilesConfigureActualUserLexer() {
    CHECK(LanguageCatalog::supportsUserDefined());
    for (bool dark : {false, true}) {
        QString error;
        auto configuration = LanguageCatalog::parseUdlXml(udlAsset(dark), dark ? 102 : 101, dark ? 202 : 201, &error);
        if (!configuration) throw std::runtime_error(error.toStdString());
        CHECK(configuration->darkTheme == dark);
        CHECK(configuration->language.engine == "user");
        CHECK(configuration->language.extensions.contains(QStringLiteral("md")));
        CHECK(configuration->language.keywords.size() == 15);
        CHECK(configuration->styles.size() == 24);
        CHECK(words(configuration->language, 0) == "- + :- :-- :--- :");
        CHECK(words(configuration->language, 7).contains("https://"));
        CHECK(property(configuration->language, "userDefine.comments") == "00# 01 02((EOL)) 03<!-- 04-->");
        CHECK(configuration->language.commentLine == "#" && configuration->language.commentStart == "<!--" &&
            configuration->language.commentEnd == "-->" && configuration->language.commentError.isEmpty());
        CHECK(property(configuration->language, "userDefine.prefixKeywords8") == "0");
        CHECK(property(configuration->language, "userDefine.nesting.19") == "65600");
        auto doc = lex(lmUserDefine, configuration->language, "# Heading\n`code`\n");
        CHECK(doc.StyleAt(0) == SCE_USER_STYLE_COMMENTLINE);
        CHECK(doc.StyleAt(11) == SCE_USER_STYLE_DELIMITER2);
    }
    auto bytes = udlAsset();
    bytes.replace("<Keywords name=\"Keywords8\"></Keywords>", "<Keywords name=\"Keywords8\">starword</Keywords>");
    auto config = LanguageCatalog::parseUdlXml(bytes, 103, 203);
    CHECK(config);
    auto doc = lex(lmUserDefine, config->language, "starword\n");
    CHECK(doc.StyleAt(0) == SCE_USER_STYLE_KEYWORD8);
}

static void udlValidationAndQuotedKeywords() {
    auto original = udlAsset();
    auto quoted = original;
    quoted.replace("<Keywords name=\"Keywords8\"></Keywords>",
        "<Keywords name=\"Keywords8\">\"end if\" 'else if'</Keywords>");
    auto config = LanguageCatalog::parseUdlXml(quoted, 301, 401);
    CHECK(config);
    CHECK(words(config->language, 14) == QByteArray("end\vif else\bif"));
    for (const auto& replacement : std::vector<std::pair<QByteArray, QByteArray>>{
        {"udlVersion=\"2.1\"", "udlVersion=\"2.0\""},
        {"caseIgnored=\"yes\"", "caseIgnored=\"maybe\""},
        {"decimalSeparator=\"0\"", "decimalSeparator=\"999\""},
        {"nesting=\"65600\"", "nesting=\"-1\""},
        {"name=\"Keywords8\"", "name=\"UnknownKeywords\""},
        {"00# 01", "99# 01"},
        {"<Keywords name=\"Keywords8\"></Keywords>", "<Keywords name=\"Keywords8\">\"unclosed</Keywords>"},
        {"name=\"DEFAULT\"", "name=\"UNKNOWN_STYLE\""},
        {"fontStyle=\"0\"", "fontStyle=\"8\""},
    }) {
        auto invalid = original;
        CHECK(invalid.contains(replacement.first));
        invalid.replace(replacement.first, replacement.second);
        QString error;
        CHECK(!LanguageCatalog::parseUdlXml(invalid, 1, 2, &error));
        CHECK(!error.isEmpty());
    }
    CHECK(!LanguageCatalog::parseUdlXml(original, 0, 1));
    CHECK(!LanguageCatalog::parseUdlXml(original, 1, -1));
}

static void resourcesDoNotDependOnWorkingDirectory() {
    const QString old = QDir::currentPath();
    CHECK(QDir::setCurrent(QDir::rootPath()));
    auto result = LanguageCatalog::load();
    CHECK(QDir::setCurrent(old));
    CHECK(result);
    CHECK(result->completions(QStringLiteral("rust")));
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::cout << std::unitbuf;
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"aliases and extensions", aliasesAndExtensions},
        {"keyword indices and real Lexilla engines", keywordMappingsAndActualLexers},
        {"themes and all completion XML assets", themesAndCompletions},
        {"malformed XML / DTD / limits", malformedXmlAndDtdAreRejected},
        {"XML keyword order independence", xmlKeywordOrderDoesNotChooseTheIndex},
        {"UDL profiles and actual user lexer", udlProfilesConfigureActualUserLexer},
        {"UDL validation and quoted keywords", udlValidationAndQuotedKeywords},
        {"working-directory independence", resourcesDoNotDependOnWorkingDirectory},
    };
    int failures = 0;
    for (const auto& test : tests) {
        std::cout << "RUN " << test.first << '\n';
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) {
            ++failures; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - static_cast<size_t>(failures) << '/' << tests.size() << " test groups passed\n";
    return failures ? 1 : 0;
}
