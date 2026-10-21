// src/services/search_service.h

#pragma once

#include "services/search_index.h"

#include <QObject>
#include <QThread>

namespace xdstarter {

class SearchWorker;

class SearchService final : public QObject {
    Q_OBJECT

  public:
    explicit SearchService(QObject* parent = nullptr);
    ~SearchService() override;

    void setEntries(QVector<SearchEntry> entries);
    void setQuery(QString query);
    [[nodiscard]] QString query() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QVector<SearchMatch> results() const;
    [[nodiscard]] int matchCount() const;

  signals:
    void resultsChanged();
    void stateChanged();

  private:
    void dispatch();
    void invalidateResults();
    void complete(quint64 indexRevision, quint64 queryGeneration, SearchResult result);

    QThread thread_;
    SearchWorker* worker_ = nullptr;
    QVector<SearchEntry> entries_;
    QVector<SearchMatch> results_;
    QString query_;
    int matchCount_ = 0;
    quint64 indexRevision_ = 0;
    quint64 queryGeneration_ = 0;
    bool inFlight_ = false;
    bool indexDirty_ = false;
    bool busy_ = false;
};

} // namespace xdstarter
