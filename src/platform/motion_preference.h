#pragma once

#include <QObject>

#include <memory>

namespace xdstarter {

// Read-only preference supplied by the platform. It is independent of layout
// persistence and can be replaced by a process-local test source.
class MotionPreference : public QObject {
    Q_OBJECT

  public:
    explicit MotionPreference(bool animationsEnabled = true, QObject* parent = nullptr)
        : QObject(parent), animationsEnabled_(animationsEnabled) {}
    [[nodiscard]] bool animationsEnabled() const { return animationsEnabled_; }
    virtual void refresh() {}

  signals:
    void animationsEnabledChanged();

  protected:
    void publishAnimationsEnabled(bool enabled);

  private:
    bool animationsEnabled_;
};

[[nodiscard]] std::unique_ptr<MotionPreference> createMotionPreference();

} // namespace xdstarter
