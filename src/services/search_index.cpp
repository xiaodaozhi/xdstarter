// src/services/search_index.cpp

#include "search_index.h"

#include "services/pinyin_data.h"

#include <QHash>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace xdstarter {
namespace {

QString boundedText(const QString& value, int maximumLength) {
    QString result = value.left(maximumLength);
    if (!result.isEmpty() && result.at(result.size() - 1).isHighSurrogate())
        result.chop(1);
    return result;
}

QString normalizedName(const QString& value) {
    return boundedText(boundedText(value, SearchIndex::maximumNameLength)
                           .normalized(QString::NormalizationForm_KC)
                           .toCaseFolded(),
                       SearchIndex::maximumNameLength)
        .trimmed();
}

QString pinyinQuery(const QString& normalized) {
    const auto decomposed = normalized.normalized(QString::NormalizationForm_D);
    QString result;
    for (qsizetype index = 0; index < decomposed.size(); ++index) {
        auto character = decomposed.at(index);
        if (character == QLatin1Char('u') && index + 1 < decomposed.size() &&
            (decomposed.at(index + 1) == QChar(0x0308) ||
             decomposed.at(index + 1) == QLatin1Char(':'))) {
            result += QLatin1Char('v');
            ++index;
        } else if (character.category() == QChar::Mark_NonSpacing ||
                   character.category() == QChar::Mark_SpacingCombining ||
                   character.category() == QChar::Mark_Enclosing) {
            continue;
        } else if ((character >= QLatin1Char('a') && character <= QLatin1Char('z')) ||
                   (character >= QLatin1Char('0') && character <= QLatin1Char('9'))) {
            result += character;
        } else if (character.isSpace() || character == QLatin1Char('\'') ||
                   character == QLatin1Char('-')) {
            continue;
        } else {
            return {};
        }
    }
    return result;
}

QVector<QStringList> readingTokens(const QString& name, bool initials) {
    QVector<QStringList> tokens;
    for (const auto codepoint : name.toUcs4()) {
        QStringList readings;
        if (codepoint < 128) {
            const QChar character{ushort(codepoint)};
            if ((character >= QLatin1Char('a') && character <= QLatin1Char('z')) ||
                character.isDigit()) {
                readings.append(QString(character));
            } else if (character.isSpace() || character.isPunct()) {
                continue;
            }
        } else {
            readings = pinyinReadings(codepoint);
            if (readings.isEmpty()) {
                const QString character = QString::fromUcs4(&codepoint, 1);
                const auto latin = pinyinQuery(character);
                if (!latin.isEmpty())
                    readings.append(latin);
                else if (QChar::isSpace(codepoint) || QChar::isPunct(codepoint))
                    continue;
            }
        }
        QStringList bounded;
        for (const auto& reading : readings) {
            const auto value = reading.left(16);
            if (value.isEmpty() || value.size() != reading.size())
                continue;
            const auto key = initials && codepoint >= 128 ? value.left(1) : value;
            if (!bounded.contains(key))
                bounded.append(key);
            if (bounded.size() == 32)
                break;
        }
        // An unknown character is a boundary, not an opportunity to join unrelated words.
        if (bounded.isEmpty())
            bounded.append(QStringLiteral(" "));
        tokens.append(std::move(bounded));
    }
    return tokens;
}

QVector<int> prefixTable(const QString& pattern) {
    QVector<int> table(pattern.size(), 0);
    int prefix = 0;
    for (qsizetype index = 1; index < pattern.size(); ++index) {
        while (prefix != 0 && pattern.at(index) != pattern.at(prefix))
            prefix = table.at(prefix - 1);
        if (pattern.at(index) == pattern.at(prefix))
            ++prefix;
        table[index] = prefix;
    }
    return table;
}

// Keep the reachable KMP states after each character's reading alternatives.
// This checks all polyphonic paths without constructing their Cartesian product.
bool containsReading(const QVector<QStringList>& tokens, const QString& pattern,
                     const QVector<int>& prefix) {
    QVector<int> states{0};
    for (const auto& alternatives : tokens) {
        QVector<int> next;
        std::array<bool, SearchIndex::maximumQueryLength + 1> seen{};
        for (const int state : states) {
            for (const auto& reading : alternatives) {
                int matched = state;
                for (const auto character : reading) {
                    while (matched != 0 && character != pattern.at(matched))
                        matched = prefix.at(matched - 1);
                    if (character == pattern.at(matched))
                        ++matched;
                    if (matched == pattern.size())
                        return true;
                }
                if (!seen[matched]) {
                    seen[matched] = true;
                    next.append(matched);
                }
            }
        }
        states = std::move(next);
    }
    return false;
}

} // namespace

void SearchIndex::setEntries(QVector<SearchEntry> entries) {
    entries_.clear();
    QHash<QString, qsizetype> positions;
    for (qsizetype index = 0; index < std::min(entries.size(), qsizetype(maximumEntries));
         ++index) {
        const auto& entry = entries.at(index);
        if (entry.targetId.isEmpty() || entry.displayName.trimmed().isEmpty())
            continue;
        auto position = positions.value(entry.targetId, -1);
        if (position < 0) {
            position = entries_.size();
            positions.insert(entry.targetId, position);
            entries_.append({entry.targetId,
                             boundedText(entry.displayName, maximumNameLength),
                             normalizedName(entry.displayName),
                             {}});
        }
        auto& destination = entries_[position];
        QStringList names{entry.displayName};
        names.append(entry.aliases.mid(0, maximumNamesPerEntry - 1));
        for (const auto& name : names) {
            if (destination.names.size() == maximumNamesPerEntry)
                break;
            const auto normalized = normalizedName(name);
            if (normalized.isEmpty())
                continue;
            const auto duplicate = std::any_of(
                destination.names.cbegin(), destination.names.cend(),
                [&normalized](const IndexedName& existing) {
                    return existing.normalized == normalized;
                });
            if (!duplicate)
                destination.names.append({normalized, readingTokens(normalized, false),
                                          readingTokens(normalized, true)});
        }
    }
}

SearchResult SearchIndex::search(const QString& query) const {
    const auto normalized = boundedText(boundedText(query, maximumQueryLength)
                                           .normalized(QString::NormalizationForm_KC)
                                           .toCaseFolded(),
                                       maximumQueryLength)
                                .trimmed();
    if (normalized.isEmpty())
        return {};
    const auto phonetic = pinyinQuery(normalized);
    const auto prefix = prefixTable(phonetic);
    struct RankedMatch {
        SearchMatch match;
        QString sortName;
    };
    QVector<RankedMatch> matches;
    for (const auto& entry : entries_) {
        int rank = std::numeric_limits<int>::max();
        for (const auto& name : entry.names) {
            if (name.normalized == normalized)
                rank = std::min(rank, 0);
            else if (name.normalized.startsWith(normalized))
                rank = std::min(rank, 1);
            else if (name.normalized.contains(normalized))
                rank = std::min(rank, 2);
            else if (!phonetic.isEmpty() && rank > 3 &&
                     containsReading(name.readings, phonetic, prefix))
                rank = 3;
            else if (!phonetic.isEmpty() && rank > 4 &&
                     containsReading(name.initials, phonetic, prefix))
                rank = 4;
            if (rank == 0)
                break;
        }
        if (rank != std::numeric_limits<int>::max())
            matches.append({{entry.targetId, entry.displayName, rank}, entry.sortName});
    }
    std::sort(matches.begin(), matches.end(), [](const RankedMatch& left, const RankedMatch& right) {
        if (left.match.rank != right.match.rank)
            return left.match.rank < right.match.rank;
        if (left.sortName != right.sortName)
            return left.sortName < right.sortName;
        return left.match.targetId < right.match.targetId;
    });
    SearchResult result;
    result.matchCount = int(matches.size());
    for (qsizetype index = 0; index < std::min(matches.size(), qsizetype(maximumResults)); ++index)
        result.matches.append(std::move(matches[index].match));
    return result;
}

} // namespace xdstarter
