// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

namespace star::function_list {

inline constexpr quint32 FunctionSearchFlags = 0x10600000;

struct ByteRange {
    qint64 begin = 0;
    qint64 end = 0; // Exclusive. All positions refer to the original UTF-8 snapshot.
};

enum class ErrorCode {
    None, InvalidXml, UnsupportedDefinition, InvalidInput, InvalidMatch,
    BackendFailure, LimitExceeded, IncompleteClass
};

struct Error {
    ErrorCode code = ErrorCode::None;
    QString message;
};

struct FunctionRule {
    QByteArray expression;
    QList<QByteArray> nameExpressions;
    QList<QByteArray> groupExpressions;
};

struct ClassRule {
    QByteArray expression;
    QByteArray openExpression;
    QByteArray closeExpression;
    QList<QByteArray> nameExpressions;
    FunctionRule functions;
};

struct Definition {
    QString key;
    QString id;
    QString displayName;
    QByteArray commentExpression;
    std::optional<FunctionRule> functions;
    std::optional<ClassRule> classes;
};

class DefinitionCatalog {
public:
    static QStringList embeddedKeys();
    static std::optional<Definition> loadEmbedded(const QString& key, Error* error = nullptr);
    static std::optional<Definition> parseXml(const QByteArray& xml, Error* error = nullptr);
};

enum class SearchPurpose { Comment, ClassRange, Delimiter, OpenDelimiter, Function, Name, GroupName };
struct SearchRequest {
    QByteArray expression;
    ByteRange range;
    quint32 flags = FunctionSearchFlags;
    SearchPurpose purpose = SearchPurpose::Function;
};

enum class SearchStatus { Found, NotFound, Failed };
struct SearchReply {
    SearchStatus status = SearchStatus::NotFound;
    ByteRange match;
    QString error;
};

// Synchronous first-match search on one immutable full Scintilla UTF-8 document.
// Must run in the parent's bounded child process, NEVER in an editor GUI thread.
using SearchCallback = std::function<SearchReply(const SearchRequest&)>;

struct Limits {
    qint64 maxDocumentBytes = 4 * 1024 * 1024;
    qint64 maxMatchBytes = 1024 * 1024;
    qint64 maxNameBytes = 8 * 1024;
    qint64 maxTotalNameBytes = 2 * 1024 * 1024;
    int maxResults = 10000;
    int maxSearchCalls = 100000;
    int maxCommentRanges = 20000;
    int maxClassRanges = 2000;
    int maxDelimiterDepth = 256;
};

struct FunctionEntry {
    QString name;
    ByteRange nameRange;
    ByteRange matchRange;
    QString groupName; // Flat, regex-derived class/namespace label, not an AST hierarchy.
    std::optional<ByteRange> groupNameRange;
    qint64 line = 0; // Zero-based line number.
    qint64 columnBytes = 0;
};

struct Extraction {
    QList<FunctionEntry> entries; // Deterministically ordered by source byte position.
    int searchCalls = 0;
};

std::optional<Extraction> extract(
    const Definition& definition, const QByteArray& utf8,
    const SearchCallback& search, const Limits& limits = {}, Error* error = nullptr);

} // namespace star::function_list
