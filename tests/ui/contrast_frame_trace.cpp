#include "contrast_frame_trace.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSGRendererInterface>
#include <QThread>

#include <utility>
#include <algorithm>
#include <cmath>

struct ContrastFrameTrace::Shared {
    QElapsedTimer clock;
    QMutex mutex;
    QJsonArray events;
    QJsonArray captures;
    QString phase = QStringLiteral("setup");
    bool active = true;
    bool overflow = false;
    static constexpr int maximumEvents = 20000;

    void append(const QString& event, const QJsonObject& state = {}) {
        QMutexLocker lock(&mutex);
        if (!active) return;
        if (events.size() >= maximumEvents) { overflow = true; return; }
        QJsonObject row{{QStringLiteral("ns"), double(clock.nsecsElapsed())},
                        {QStringLiteral("event"), event},
                        {QStringLiteral("phase"), phase},
                        {QStringLiteral("thread"), QString::number(quintptr(QThread::currentThreadId()))}};
        if (!state.isEmpty()) row.insert(QStringLiteral("state"), state);
        events.append(row);
    }
};

ContrastFrameTrace::ContrastFrameTrace(QQuickWindow* window, QString directory)
    : window_(window), directory_(std::move(directory)), shared_(std::make_shared<Shared>()) {
    shared_->clock.start();
    const auto connectEvent = [this](auto signal, const char* name) {
        const auto shared = shared_;
        connections_.push_back(QObject::connect(window_, signal, window_, [shared, name] {
            shared->append(QString::fromLatin1(name));
        }, Qt::DirectConnection));
    };
    connectEvent(&QQuickWindow::beforeSynchronizing, "beforeSynchronizing");
    connectEvent(&QQuickWindow::afterSynchronizing, "afterSynchronizing");
    connectEvent(&QQuickWindow::beforeRendering, "beforeRendering");
    connectEvent(&QQuickWindow::afterRendering, "afterRendering");
    connectEvent(&QQuickWindow::afterFrameEnd, "afterFrameEnd");
    connectEvent(&QQuickWindow::frameSwapped, "frameSwapped");
    shared_->append(QStringLiteral("trace-start"));
}

ContrastFrameTrace::~ContrastFrameTrace() {
    for (const auto& connection : connections_) QObject::disconnect(connection);
    QMutexLocker lock(&shared_->mutex);
    shared_->active = false;
}

void ContrastFrameTrace::mark(const QString& phase, const QJsonObject& state) {
    QMutexLocker lock(&shared_->mutex);
    if (!shared_->active) return;
    shared_->phase = phase;
    if (shared_->events.size() >= Shared::maximumEvents) { shared_->overflow = true; return; }
    shared_->events.append(QJsonObject{
        {QStringLiteral("ns"), double(shared_->clock.nsecsElapsed())},
        {QStringLiteral("event"), QStringLiteral("gui-state")},
        {QStringLiteral("phase"), phase},
        {QStringLiteral("thread"), QString::number(quintptr(QThread::currentThreadId()))},
        {QStringLiteral("state"), state}});
}

bool ContrastFrameTrace::capture(const QString& scene, const QString& stage,
                                 const QImage& image, const QJsonObject& state) {
    const QString fileName = scene + QLatin1Char('-') + stage + QStringLiteral(".png");
    QJsonObject row{{QStringLiteral("scene"), scene}, {QStringLiteral("stage"), stage},
                    {QStringLiteral("ns"), double(shared_->clock.nsecsElapsed())},
                    {QStringLiteral("state"), state}, {QStringLiteral("valid"), !image.isNull()}};
    bool written = true;
    if (!image.isNull()) {
        const QString path = QDir(directory_).filePath(fileName);
        written = image.save(path);
        QFile file(path);
        if (!written || !file.open(QIODevice::ReadOnly)) return false;
        row.insert(QStringLiteral("file"), fileName);
        row.insert(QStringLiteral("sha256"), QString::fromLatin1(
            QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()));
        row.insert(QStringLiteral("width"), image.width());
        row.insert(QStringLiteral("height"), image.height());
        row.insert(QStringLiteral("dpr"), image.devicePixelRatio());
    }
    {
        QMutexLocker lock(&shared_->mutex);
        shared_->captures.append(row);
    }
    shared_->append(QStringLiteral("grab-returned"));
    if (stage == QLatin1String("early")) {
        previousImages_.insert(scene, image);
        previousStates_.insert(scene, state);
    } else {
        const auto previous = previousImages_.value(scene);
        const auto previousState = previousStates_.value(scene);
        const bool comparable = !previous.isNull() && !image.isNull() && previous.size() == image.size() &&
                                previous.devicePixelRatio() == image.devicePixelRatio();
        const QString previousStage = stage == QLatin1String("ready") ? QStringLiteral("early")
                                      : stage == QLatin1String("first") ? QStringLiteral("ready")
                                                                         : QStringLiteral("first");
        QJsonObject comparison{{QStringLiteral("scene"), scene},
                               {QStringLiteral("from"), previousStage},
                               {QStringLiteral("to"), stage},
                               {QStringLiteral("comparable"), comparable}};
        if (comparable) {
            const auto left = previous.convertToFormat(QImage::Format_ARGB32);
            const auto right = image.convertToFormat(QImage::Format_ARGB32);
            int minX = left.width(), minY = left.height(), maxX = -1, maxY = -1;
            qint64 changed = 0;
            const auto glyphRect = state.value(QStringLiteral("searchGlyphSceneRect")).toObject();
            const auto priorRect = previousState.value(QStringLiteral("searchGlyphSceneRect")).toObject();
            const auto dpr = image.devicePixelRatio();
            const int glyphLeft = int(std::floor(glyphRect.value(QStringLiteral("x")).toDouble() * dpr));
            const int glyphTop = int(std::floor(glyphRect.value(QStringLiteral("y")).toDouble() * dpr));
            const int glyphRight = int(std::ceil((glyphRect.value(QStringLiteral("x")).toDouble() +
                                                  glyphRect.value(QStringLiteral("width")).toDouble()) * dpr));
            const int glyphBottom = int(std::ceil((glyphRect.value(QStringLiteral("y")).toDouble() +
                                                   glyphRect.value(QStringLiteral("height")).toDouble()) * dpr));
            const bool glyphComparable = glyphRect == priorRect && glyphLeft >= 0 && glyphTop >= 0 &&
                                         glyphRight > glyphLeft && glyphBottom > glyphTop &&
                                         glyphRight <= left.width() && glyphBottom <= left.height();
            qint64 glyphChanged = 0;
            for (int y = 0; y < left.height(); ++y) {
                const auto* a = reinterpret_cast<const QRgb*>(left.constScanLine(y));
                const auto* b = reinterpret_cast<const QRgb*>(right.constScanLine(y));
                for (int x = 0; x < left.width(); ++x) {
                    if (a[x] == b[x]) continue;
                    ++changed;
                    if (glyphComparable && x >= glyphLeft && x < glyphRight && y >= glyphTop && y < glyphBottom)
                        ++glyphChanged;
                    minX = std::min(minX, x); minY = std::min(minY, y);
                    maxX = std::max(maxX, x); maxY = std::max(maxY, y);
                }
            }
            comparison.insert(QStringLiteral("changedPixels"), double(changed));
            comparison.insert(QStringLiteral("glyphComparable"), glyphComparable);
            if (glyphComparable) comparison.insert(QStringLiteral("glyphChangedPixels"), double(glyphChanged));
            if (changed)
                comparison.insert(QStringLiteral("bounds"),
                                  QJsonObject{{QStringLiteral("x"), minX}, {QStringLiteral("y"), minY},
                                              {QStringLiteral("width"), maxX - minX + 1},
                                              {QStringLiteral("height"), maxY - minY + 1}});
        }
        comparisons_.append(comparison);
        if (stage == QLatin1String("later")) {
            previousImages_.remove(scene);
            previousStates_.remove(scene);
        } else {
            previousImages_.insert(scene, image);
            previousStates_.insert(scene, state);
        }
    }
    return written;
}

bool ContrastFrameTrace::finish() {
    if (finished_) return false;
    finished_ = true;
    for (const auto& connection : connections_) QObject::disconnect(connection);
    QJsonObject result;
    {
        QMutexLocker lock(&shared_->mutex);
        shared_->active = false;
        result = {{QStringLiteral("schemaVersion"), 2},
                  {QStringLiteral("qpa"), QGuiApplication::platformName()},
                  {QStringLiteral("graphicsApi"), int(window_->rendererInterface()->graphicsApi())},
                  {QStringLiteral("qtVersion"), QStringLiteral(QT_VERSION_STR)},
                  {QStringLiteral("observerIsInstrumented"), true},
                  {QStringLiteral("eventLimitReached"), shared_->overflow},
                  {QStringLiteral("events"), shared_->events},
                  {QStringLiteral("captures"), shared_->captures},
                  {QStringLiteral("pixelComparisons"), comparisons_}};
    }
    QSaveFile file(QDir(directory_).filePath(QStringLiteral("frame-trace.json")));
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly) &&
           file.write(bytes) == bytes.size() && file.commit();
}
