#include "scene_graph_failure_guard.h"

#include <QCoreApplication>
#include <QDebug>
#include <QQuickWindow>

#include <cstdlib>

namespace xdstarter {

SceneGraphFailureGuard::SceneGraphFailureGuard(QQuickWindow& window,
                                               QCoreApplication& application) {
    connect(&window, &QQuickWindow::sceneGraphError, this,
            [this, &application](QQuickWindow::SceneGraphError error, const QString& message) {
                if (failed_)
                    return;
                failed_ = true;
                qCritical().noquote() << "Graphics initialization failed:" << static_cast<int>(error)
                                      << message;
                application.exit(EXIT_FAILURE);
            });
}

} // namespace xdstarter
