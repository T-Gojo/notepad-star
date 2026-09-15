// SPDX-License-Identifier: GPL-3.0-or-later
#include "internal.h"
#include <algorithm>
#include <exception>

namespace star::function_list {
namespace {

bool validLimits(const Limits& limits) {
    const Limits hard;
    return limits.maxDocumentBytes > 0 && limits.maxDocumentBytes <= hard.maxDocumentBytes
        && limits.maxMatchBytes > 0 && limits.maxMatchBytes <= hard.maxMatchBytes
        && limits.maxNameBytes > 0 && limits.maxNameBytes <= hard.maxNameBytes
        && limits.maxTotalNameBytes > 0 && limits.maxTotalNameBytes <= hard.maxTotalNameBytes
        && limits.maxResults > 0 && limits.maxResults <= hard.maxResults
        && limits.maxSearchCalls > 0 && limits.maxSearchCalls <= hard.maxSearchCalls
        && limits.maxCommentRanges > 0 && limits.maxCommentRanges <= hard.maxCommentRanges
        && limits.maxClassRanges > 0 && limits.maxClassRanges <= hard.maxClassRanges
        && limits.maxDelimiterDepth > 0 && limits.maxDelimiterDepth <= hard.maxDelimiterDepth;
}

QList<ByteRange> complement(QList<ByteRange> ranges, ByteRange whole) {
    std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) {
        return a.begin < b.begin || (a.begin == b.begin && a.end < b.end);
    });
    QList<ByteRange> result;
    qint64 cursor = whole.begin;
    for (auto range : ranges) {
        range.begin = std::max(range.begin, whole.begin);
        range.end = std::min(range.end, whole.end);
        if (range.end <= cursor) continue;
        if (cursor < range.begin) result.push_back({cursor, range.begin});
        cursor = std::max(cursor, range.end);
    }
    if (cursor < whole.end) result.push_back({cursor, whole.end});
    return result;
}

class Runner {
public:
    Runner(const QByteArray& source, const SearchCallback& callback, const Limits& limits, Error* error)
        : source_(source), callback_(callback), limits_(limits), error_(error) {}

    std::optional<Extraction> run(const Definition& definition) {
        if (!definition.commentExpression.isEmpty()) {
            qint64 cursor = 0;
            while (cursor < source_.size()) {
                const auto match = find(definition.commentExpression, {cursor, source_.size()}, SearchPurpose::Comment);
                if (!match) break;
                if (comments_.size() >= limits_.maxCommentRanges) {
                    limit(QStringLiteral("Comment range limit exceeded."));
                    return std::nullopt;
                }
                comments_.push_back(*match);
                cursor = match->end;
            }
        }
        if (failed()) return std::nullopt;
        QList<ByteRange> classRanges;
        if (definition.classes) {
            const auto& rule = *definition.classes;
            qint64 cursor = 0;
            while (cursor < source_.size()) {
                const auto header = find(rule.expression, {cursor, source_.size()}, SearchPurpose::ClassRange);
                if (!header) break;
                if (const auto comment = commentAt(header->begin)) {
                    cursor = std::max(header->end, comment->end);
                    continue;
                }
                const auto classNameRange = selectName(*header, rule.nameExpressions, SearchPurpose::GroupName);
                if (failed()) return std::nullopt;
                ByteRange body = *header;
                if (!rule.openExpression.isEmpty()) {
                    const auto close = closeBody(header->end, rule);
                    if (!close) return std::nullopt;
                    body.end = *close;
                }
                if (body.end - body.begin > limits_.maxMatchBytes || classRanges.size() >= limits_.maxClassRanges) {
                    limit(QStringLiteral("Class size or class range count limit exceeded."));
                    return std::nullopt;
                }
                const qint64 interior = rule.openExpression.isEmpty()
                    ? (classNameRange ? classNameRange->end : header->begin + 1) : header->end;
                if (boundary(interior)) {
                    const auto nested = find(rule.expression, {interior, body.end}, SearchPurpose::ClassRange);
                    if (failed()) return std::nullopt;
                    if (nested) {
                        detail::fail(error_, ErrorCode::UnsupportedDefinition,
                            QStringLiteral("Nested class ranges are not supported by the flat grouping model."));
                        return std::nullopt;
                    }
                }
                classRanges.push_back(body);
                if (!functions(rule.functions, body, classNameRange)) return std::nullopt;
                cursor = body.end;
            }
        }
        if (failed()) return std::nullopt;
        if (definition.functions) {
            // Unit parsers search outside comments. Mixed parsers search outside
            // class ranges, then filter candidate name positions against comments.
            const auto excluded = definition.classes ? classRanges : comments_;
            for (const auto& range : complement(excluded, {0, source_.size()}))
                if (!functions(*definition.functions, range, std::nullopt)) return std::nullopt;
        }
        if (failed()) return std::nullopt;
        std::stable_sort(result_.entries.begin(), result_.entries.end(), [](const auto& a, const auto& b) {
            if (a.nameRange.begin != b.nameRange.begin) return a.nameRange.begin < b.nameRange.begin;
            if (a.matchRange.begin != b.matchRange.begin) return a.matchRange.begin < b.matchRange.begin;
            if (a.groupName != b.groupName) return a.groupName < b.groupName;
            return a.name < b.name;
        });
        QList<qint64> starts{0};
        for (qsizetype i = 0; i < source_.size(); ++i) {
            if (source_[i] == '\r') {
                if (i + 1 < source_.size() && source_[i + 1] == '\n') ++i;
                starts.push_back(i + 1);
            } else if (source_[i] == '\n') starts.push_back(i + 1);
        }
        for (auto& entry : result_.entries) {
            const auto line = std::upper_bound(starts.cbegin(), starts.cend(), entry.nameRange.begin) - starts.cbegin() - 1;
            entry.line = line;
            entry.columnBytes = entry.nameRange.begin - starts[line];
        }
        return std::move(result_);
    }

private:
    bool failed() const { return error_->code != ErrorCode::None; }
    void limit(const QString& message) { detail::fail(error_, ErrorCode::LimitExceeded, message); }
    bool boundary(qint64 position) const {
        return position >= 0 && position <= source_.size()
            && (position == source_.size() || (static_cast<unsigned char>(source_[position]) & 0xc0) != 0x80);
    }
    std::optional<ByteRange> find(const QByteArray& expression, ByteRange range, SearchPurpose purpose) {
        if (failed() || range.begin >= range.end) return std::nullopt;
        if (!boundary(range.begin) || !boundary(range.end)) {
            detail::fail(error_, ErrorCode::InvalidMatch, QStringLiteral("Search range is not on UTF-8 byte boundaries."));
            return std::nullopt;
        }
        if (result_.searchCalls >= limits_.maxSearchCalls) {
            limit(QStringLiteral("Search callback count limit exceeded."));
            return std::nullopt;
        }
        ++result_.searchCalls;
        SearchReply reply;
        try {
            reply = callback_({expression, range, FunctionSearchFlags, purpose});
        } catch (const std::exception&) {
            detail::fail(error_, ErrorCode::BackendFailure, QStringLiteral("Search callback threw an exception."));
            return std::nullopt;
        } catch (...) {
            detail::fail(error_, ErrorCode::BackendFailure, QStringLiteral("Search callback failed unexpectedly."));
            return std::nullopt;
        }
        if (reply.status == SearchStatus::NotFound) return std::nullopt;
        if (reply.status != SearchStatus::Found) {
            detail::fail(error_, ErrorCode::BackendFailure,
                reply.error.isEmpty() ? QStringLiteral("Search backend reported failure.") : reply.error);
            return std::nullopt;
        }
        const auto match = reply.match;
        if (match.begin < range.begin || match.end > range.end || match.end <= match.begin
            || !boundary(match.begin) || !boundary(match.end)) {
            detail::fail(error_, ErrorCode::InvalidMatch,
                QStringLiteral("Backend returned an empty, out-of-range, reversed, or split-UTF-8 match."));
            return std::nullopt;
        }
        if (match.end - match.begin > limits_.maxMatchBytes) {
            limit(QStringLiteral("Backend match byte limit exceeded."));
            return std::nullopt;
        }
        return match;
    }

    std::optional<ByteRange> commentAt(qint64 position) const {
        const auto after = std::upper_bound(comments_.cbegin(), comments_.cend(), position,
            [](qint64 value, const ByteRange& range) { return value < range.begin; });
        if (after == comments_.cbegin()) return std::nullopt;
        const auto& range = *std::prev(after);
        return position < range.end ? std::optional<ByteRange>(range) : std::nullopt;
    }

    std::optional<ByteRange> selectName(ByteRange range, const QList<QByteArray>& expressions, SearchPurpose purpose) {
        if (expressions.isEmpty()) return std::nullopt;
        for (const auto& expression : expressions) {
            auto match = find(expression, range, purpose);
            if (!match) return std::nullopt;
            range = *match;
        }
        return range;
    }

    std::optional<qint64> closeBody(qint64 cursor, const ClassRule& rule) {
        int depth = 1;
        const QByteArray delimiters = "(" + rule.openExpression + "|" + rule.closeExpression + ")";
        while (cursor < source_.size()) {
            const auto token = find(delimiters, {cursor, source_.size()}, SearchPurpose::Delimiter);
            if (!token) break;
            cursor = token->end;
            if (commentAt(token->begin)) continue;
            const auto open = find(rule.openExpression, *token, SearchPurpose::OpenDelimiter);
            if (failed()) return std::nullopt;
            depth += open ? 1 : -1;
            if (depth > limits_.maxDelimiterDepth) {
                limit(QStringLiteral("Class delimiter depth limit exceeded."));
                return std::nullopt;
            }
            if (depth == 0) return cursor;
        }
        if (!failed()) detail::fail(error_, ErrorCode::IncompleteClass, QStringLiteral("Class body has no matched closing delimiter."));
        return std::nullopt;
    }

    std::optional<QString> name(ByteRange range) {
        if (range.end - range.begin > limits_.maxNameBytes) {
            limit(QStringLiteral("Function/group name byte limit exceeded."));
            return std::nullopt;
        }
        const auto bytes = source_.mid(range.begin, range.end - range.begin);
        if (bytes.contains('\0')) {
            detail::fail(error_, ErrorCode::InvalidMatch, QStringLiteral("Extracted names cannot contain NUL."));
            return std::nullopt;
        }
        return QString::fromUtf8(bytes);
    }

    bool functions(const FunctionRule& rule, ByteRange range, std::optional<ByteRange> enclosingName) {
        qint64 cursor = range.begin;
        while (cursor < range.end) {
            const auto match = find(rule.expression, {cursor, range.end}, SearchPurpose::Function);
            if (!match) break;
            cursor = match->end;
            const auto nameRange = rule.nameExpressions.isEmpty()
                ? match : selectName(*match, rule.nameExpressions, SearchPurpose::Name);
            if (failed()) return false;
            if (!nameRange) continue;
            auto groupRange = enclosingName;
            if (!groupRange && !rule.groupExpressions.isEmpty())
                groupRange = selectName(*match, rule.groupExpressions, SearchPurpose::GroupName);
            if (failed()) return false;
            if (commentAt(nameRange->begin) || (groupRange && commentAt(groupRange->begin))) continue;
            const auto functionName = name(*nameRange);
            if (!functionName) return false;
            const auto groupName = groupRange ? name(*groupRange) : std::optional<QString>(QString{});
            if (!groupName) return false;
            const qint64 nameBytes = (nameRange->end - nameRange->begin)
                + (groupRange ? groupRange->end - groupRange->begin : 0);
            if (result_.entries.size() >= limits_.maxResults || nameBytes > limits_.maxTotalNameBytes - totalNames_) {
                limit(QStringLiteral("Function count or total name byte limit exceeded."));
                return false;
            }
            totalNames_ += nameBytes;
            result_.entries.push_back({*functionName, *nameRange, *match, *groupName, groupRange, 0, 0});
        }
        return !failed();
    }

    const QByteArray& source_;
    const SearchCallback& callback_;
    const Limits& limits_;
    Error* error_;
    QList<ByteRange> comments_;
    Extraction result_;
    qint64 totalNames_ = 0;
};
}

std::optional<Extraction> extract(
    const Definition& definition, const QByteArray& utf8,
    const SearchCallback& search, const Limits& limits, Error* error) {
    Error local;
    if (!error) error = &local;
    *error = {};
    if (!search || !validLimits(limits) || utf8.size() > limits.maxDocumentBytes || !detail::validUtf8(utf8)) {
        detail::fail(error, ErrorCode::InvalidInput, QStringLiteral("Invalid UTF-8 snapshot, search callback, document size, or limits."));
        return std::nullopt;
    }
    if (!detail::validateDefinition(definition, error)) return std::nullopt;
    return Runner(utf8, search, limits, error).run(definition);
}
} // namespace star::function_list
