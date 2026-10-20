#include "desktop_shell_service.h"

#include <QMetaObject>

#include <utility>

namespace xdstarter {

class DesktopShellWorker final : public QObject {
    Q_OBJECT

  public:
    explicit DesktopShellWorker(std::unique_ptr<DesktopPlatform> platform)
        : platform_(std::move(platform)) {}

    void openAction(const QString& actionId) {
        if (QThread::currentThread()->isInterruptionRequested())
            return;
        DesktopActionResult result;
        try {
            result = platform_->openDesktopAction(actionId);
        } catch (...) {
            result = {false, QStringLiteral("快捷操作失败，请稍后重试。"),
                      QStringLiteral("desktop-operation-failed")};
        }
        emit actionFinished(actionId, result);
    }

    void queryAnchor(quint64 requestId) {
        if (QThread::currentThread()->isInterruptionRequested())
            return;
        DesktopAnchorResult result;
        try {
            result = platform_->queryMenuAnchor();
        } catch (...) {
            result.message = QStringLiteral("未能读取屏幕工作区，使用 Qt 工作区回退位置。");
            result.errorCode = QStringLiteral("desktop-query-failed");
        }
        emit anchorFinished(requestId, result);
    }

  signals:
    void actionFinished(const QString& actionId, const DesktopActionResult& result);
    void anchorFinished(quint64 requestId, const DesktopAnchorResult& result);

  private:
    std::unique_ptr<DesktopPlatform> platform_;
};

DesktopShellService::DesktopShellService(std::unique_ptr<DesktopPlatform> platform, QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<DesktopActionResult>();
    qRegisterMetaType<DesktopAnchorResult>();
    if (!platform) {
        status_ = QStringLiteral("当前未连接桌面快捷入口。");
        return;
    }
    worker_ = new DesktopShellWorker(std::move(platform));
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &DesktopShellWorker::actionFinished, this,
            [this](const QString& actionId, const DesktopActionResult& result) {
                busy_ = false;
                status_ = result.message.isEmpty()
                              ? (result.success ? QStringLiteral("快捷入口已打开。")
                                                : QStringLiteral("快捷操作失败，请稍后重试。"))
                              : result.message;
                emit stateChanged();
                if (result.success)
                    emit desktopActionSucceeded(actionId);
                else
                    emit desktopActionFailed(actionId, status_);
            });
    connect(worker_, &DesktopShellWorker::anchorFinished, this,
            [this](quint64 requestId, const DesktopAnchorResult& result) {
                anchorPending_ = false;
                if (queuedAnchorRequest_ != 0) {
                    const auto latestRequest = std::exchange(queuedAnchorRequest_, 0);
                    queueAnchorQuery(latestRequest);
                    return;
                }
                if (requestId == anchorGeneration_)
                    emit startButtonAnchorReady(requestId, result);
            });
    thread_.start();
}

DesktopShellService::~DesktopShellService() {
    thread_.requestInterruption();
    thread_.quit();
    thread_.wait();
}

bool DesktopShellService::busy() const { return busy_; }
QString DesktopShellService::status() const { return status_; }

bool DesktopShellService::rejectAction(const QString& actionId, const QString& message) {
    status_ = message;
    emit stateChanged();
    emit desktopActionFailed(actionId, message);
    return false;
}

bool DesktopShellService::requestDesktopAction(const QString& actionId) {
    if (!isSupportedDesktopAction(actionId))
        return rejectAction(actionId, QStringLiteral("不支持此快捷操作。"));
    if (!worker_)
        return rejectAction(actionId, QStringLiteral("当前未连接桌面快捷入口。"));
    if (busy_)
        return rejectAction(actionId, QStringLiteral("正在处理快捷操作，请稍候。"));
    busy_ = true;
    status_ = QStringLiteral("正在打开快捷入口…");
    emit stateChanged();
    auto* worker = worker_;
    QMetaObject::invokeMethod(
        worker, [worker, actionId] { worker->openAction(actionId); }, Qt::QueuedConnection);
    return true;
}

quint64 DesktopShellService::requestStartButtonAnchor() {
    const auto requestId = ++anchorGeneration_;
    if (!worker_) {
        DesktopAnchorResult result;
        result.message = QStringLiteral("当前未连接桌面定位服务。");
        result.errorCode = QStringLiteral("desktop-platform-unavailable");
        // Delivery is asynchronous even without a platform, so callers can
        // store the returned request ID before receiving the result.
        QMetaObject::invokeMethod(
            this,
            [this, requestId, result] {
                if (requestId == anchorGeneration_)
                    emit startButtonAnchorReady(requestId, result);
            },
            Qt::QueuedConnection);
    } else if (anchorPending_) {
        queuedAnchorRequest_ = requestId;
    } else {
        queueAnchorQuery(requestId);
    }
    return requestId;
}

void DesktopShellService::invalidateStartButtonAnchor() {
    ++anchorGeneration_;
    queuedAnchorRequest_ = 0;
}

void DesktopShellService::queueAnchorQuery(quint64 requestId) {
    anchorPending_ = true;
    auto* worker = worker_;
    QMetaObject::invokeMethod(
        worker, [worker, requestId] { worker->queryAnchor(requestId); }, Qt::QueuedConnection);
}

} // namespace xdstarter

#include "desktop_shell_service.moc"
