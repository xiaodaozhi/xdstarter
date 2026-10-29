#pragma once

#include <QObject>

#include <functional>

class QCoreApplication;

namespace xdstarter {

class SceneGraphFailureGuard;

// Only launches the notice after releasing the resident's input captures.
// The launcher must start an independent process and must not wait for it.
class SceneGraphFailureNotice final : public QObject {
  public:
    SceneGraphFailureNotice(QCoreApplication& application, SceneGraphFailureGuard& failure,
                            bool enabled, std::function<void()> shutdown,
                            std::function<bool()> launch);
    void dispatch();

  private:
    SceneGraphFailureGuard& failure_;
    bool enabled_;
    bool attempted_ = false;
    std::function<void()> shutdown_;
    std::function<bool()> launch_;
};

} // namespace xdstarter
