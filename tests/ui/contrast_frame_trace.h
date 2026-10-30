#pragma once

#include <QImage>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QString>

#include <memory>
#include <vector>

class QQuickWindow;

// Opt-in research instrumentation. Render-thread callbacks only record
// monotonic event metadata; QML state and images are read on the GUI thread.
class ContrastFrameTrace {
  public:
    ContrastFrameTrace(QQuickWindow* window, QString directory);
    ~ContrastFrameTrace();
    ContrastFrameTrace(const ContrastFrameTrace&) = delete;
    ContrastFrameTrace& operator=(const ContrastFrameTrace&) = delete;

    void mark(const QString& phase, const QJsonObject& state);
    [[nodiscard]] bool capture(const QString& scene, const QString& stage,
                               const QImage& image, const QJsonObject& state);
    [[nodiscard]] bool finish();

  private:
    struct Shared;
    QQuickWindow* window_;
    QString directory_;
    std::shared_ptr<Shared> shared_;
    std::vector<QMetaObject::Connection> connections_;
    QHash<QString, QImage> previousImages_;
    QHash<QString, QJsonObject> previousStates_;
    QJsonArray comparisons_;
    bool finished_ = false;
};
