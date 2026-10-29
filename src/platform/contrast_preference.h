#pragma once

#include <QObject>
#include <QVariantMap>

#include <memory>

namespace xdstarter {

struct ContrastState {
    bool enabled = false;
    QVariantMap colors;
};

// Read-only OS preference. User layout and the explicit light/dark choice do
// not own this state. A local test can inject a replacement source.
class ContrastPreference : public QObject {
    Q_OBJECT

  public:
    explicit ContrastPreference(QObject* parent = nullptr) : QObject(parent) {}
    [[nodiscard]] bool enabled() const { return state_.enabled; }
    [[nodiscard]] QVariantMap colors() const { return state_.colors; }
    virtual void refresh() {}

  signals:
    void changed();

  protected:
    void publish(ContrastState state);

  private:
    ContrastState state_;
};

[[nodiscard]] std::unique_ptr<ContrastPreference> createContrastPreference();

} // namespace xdstarter
