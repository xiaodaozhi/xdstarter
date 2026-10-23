// src/services/session_instance.h

#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>

#include <functional>
#include <utility>

namespace xdstarter {

class SessionInstance final : public QObject {
    Q_OBJECT
  public:
    enum class Result { Primary, ActivatedExisting, Failed };
    explicit SessionInstance(const QString& key, const QString& directory,
                             QObject* parent = nullptr);
    [[nodiscard]] Result acquire();
    [[nodiscard]] QString errorString() const;
    void setForegroundGrant(std::function<void(qint64)> grant) {
        foregroundGrant_ = std::move(grant);
    }
  signals:
    void activationRequested();

  private:
    QString endpoint_;
    QString error_;
    QLockFile lock_;
    QLocalServer server_;
    std::function<void(qint64)> foregroundGrant_;
};

} // namespace xdstarter
