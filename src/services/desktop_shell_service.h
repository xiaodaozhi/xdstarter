#pragma once

#include "platform/desktop_platform.h"

#include <QMetaType>
#include <QObject>
#include <QThread>

#include <memory>

namespace xdstarter {

class DesktopShellWorker;

class DesktopShellService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)

  public:
    explicit DesktopShellService(std::unique_ptr<DesktopPlatform> platform,
                                 QObject* parent = nullptr);
    ~DesktopShellService() override;

    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString status() const;
    bool requestDesktopAction(const QString& actionId);

    // Repeated requests coalesce while the worker is querying. Only the latest
    // request may deliver a result, so a previous reveal cannot move a new one.
    quint64 requestStartButtonAnchor();
    void invalidateStartButtonAnchor();

  signals:
    void stateChanged();
    void desktopActionSucceeded(const QString& actionId);
    void desktopActionFailed(const QString& actionId, const QString& message);
    void startButtonAnchorReady(quint64 requestId, const DesktopAnchorResult& result);

  private:
    void queueAnchorQuery(quint64 requestId);
    bool rejectAction(const QString& actionId, const QString& message);

    QThread thread_;
    DesktopShellWorker* worker_ = nullptr;
    bool busy_ = false;
    QString status_;
    quint64 anchorGeneration_ = 0;
    bool anchorPending_ = false;
    quint64 queuedAnchorRequest_ = 0;
};

} // namespace xdstarter

Q_DECLARE_METATYPE(xdstarter::DesktopActionResult)
Q_DECLARE_METATYPE(xdstarter::DesktopAnchorResult)
