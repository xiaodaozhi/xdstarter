// src/services/search_service.cpp

#include "search_service.h"

#include <QMetaObject>

#include <utility>

namespace xdstarter {

class SearchWorker final : public QObject {
    Q_OBJECT

  public:
    void run(quint64 indexRevision, quint64 queryGeneration, bool replaceIndex,
             QVector<SearchEntry> entries, const QString& query) {
        if (QThread::currentThread()->isInterruptionRequested())
            return;
        if (replaceIndex)
            index_.setEntries(std::move(entries));
        if (QThread::currentThread()->isInterruptionRequested())
            return;
        emit finished(indexRevision, queryGeneration, index_.search(query));
    }

  signals:
    void finished(quint64 indexRevision, quint64 queryGeneration, SearchResult result);

  private:
    SearchIndex index_;
};

SearchService::SearchService(QObject* parent) : QObject(parent) {
    qRegisterMetaType<SearchResult>();
    worker_ = new SearchWorker;
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &SearchWorker::finished, this, &SearchService::complete);
    thread_.start();
}

SearchService::~SearchService() {
    thread_.requestInterruption();
    thread_.quit();
    thread_.wait();
}

void SearchService::setEntries(QVector<SearchEntry> entries) {
    entries_ = std::move(entries);
    ++indexRevision_;
    indexDirty_ = true;
    invalidateResults();
    busy_ = !query_.trimmed().isEmpty();
    emit stateChanged();
    dispatch();
}

void SearchService::setQuery(QString query) {
    query = query.left(SearchIndex::maximumQueryLength);
    if (!query.isEmpty() && query.at(query.size() - 1).isHighSurrogate())
        query.chop(1);
    if (query_ == query)
        return;
    query_ = std::move(query);
    ++queryGeneration_;
    invalidateResults();
    busy_ = !query_.trimmed().isEmpty();
    emit stateChanged();
    dispatch();
}

QString SearchService::query() const { return query_; }
bool SearchService::busy() const { return busy_; }
QVector<SearchMatch> SearchService::results() const { return results_; }
int SearchService::matchCount() const { return matchCount_; }

void SearchService::invalidateResults() {
    results_.clear();
    matchCount_ = 0;
    emit resultsChanged();
}

void SearchService::dispatch() {
    if (inFlight_ || (!indexDirty_ && query_.trimmed().isEmpty()))
        return;
    const auto revision = indexRevision_;
    const auto generation = queryGeneration_;
    const bool replaceIndex = indexDirty_;
    auto entries = replaceIndex ? entries_ : QVector<SearchEntry>{};
    const auto query = query_;
    indexDirty_ = false;
    inFlight_ = true;
    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, revision, generation, replaceIndex, entries = std::move(entries),
         query]() mutable {
            worker->run(revision, generation, replaceIndex, std::move(entries), query);
        },
        Qt::QueuedConnection);
}

void SearchService::complete(quint64 indexRevision, quint64 queryGeneration, SearchResult result) {
    inFlight_ = false;
    if (indexRevision != indexRevision_ || queryGeneration != queryGeneration_) {
        dispatch();
        return;
    }
    if (!query_.trimmed().isEmpty()) {
        results_ = std::move(result.matches);
        matchCount_ = result.matchCount;
        busy_ = false;
        emit resultsChanged();
        emit stateChanged();
    }
}

} // namespace xdstarter

#include "search_service.moc"
