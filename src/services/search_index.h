// src/services/search_index.h

#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace xdstarter {

struct SearchEntry {
    QString targetId;
    QString displayName;
    QStringList aliases;
};

struct SearchMatch {
    QString targetId;
    QString displayName;
    int rank = 0;
};

struct SearchResult {
    QVector<SearchMatch> matches;
    int matchCount = 0;
};

// A value-only, offline index. Construction and lookup belong to the search worker.
class SearchIndex final {
  public:
    static constexpr int maximumEntries = 8192;
    static constexpr int maximumNameLength = 256;
    static constexpr int maximumQueryLength = 128;
    static constexpr int maximumNamesPerEntry = 16;
    static constexpr int maximumResults = 100;

    void setEntries(QVector<SearchEntry> entries);
    [[nodiscard]] SearchResult search(const QString& query) const;

  private:
    struct IndexedName {
        QString normalized;
        QVector<QStringList> readings;
        QVector<QStringList> initials;
    };
    struct IndexedEntry {
        QString targetId;
        QString displayName;
        QString sortName;
        QVector<IndexedName> names;
    };
    QVector<IndexedEntry> entries_;
};

} // namespace xdstarter

Q_DECLARE_METATYPE(xdstarter::SearchResult)
