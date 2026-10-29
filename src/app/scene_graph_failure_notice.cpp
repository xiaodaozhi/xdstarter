#include "scene_graph_failure_notice.h"

#include "scene_graph_failure_guard.h"

#include <QCoreApplication>
#include <QDebug>

#include <utility>

namespace xdstarter {

SceneGraphFailureNotice::SceneGraphFailureNotice(QCoreApplication& application,
                                                 SceneGraphFailureGuard& failure, bool enabled,
                                                 std::function<void()> shutdown,
                                                 std::function<bool()> launch)
    : failure_(failure), enabled_(enabled), shutdown_(std::move(shutdown)),
      launch_(std::move(launch)) {
    connect(&application, &QCoreApplication::aboutToQuit, this,
            [this] { dispatch(); });
}

void SceneGraphFailureNotice::dispatch() {
    if (!enabled_ || attempted_ || !failure_.failed())
        return;
    attempted_ = true;
    shutdown_();
    if (!launch_())
        qWarning() << "Could not launch the graphics failure notice";
}

} // namespace xdstarter
