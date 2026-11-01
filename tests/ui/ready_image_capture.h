#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include <functional>
#include <utility>

// Test evidence must satisfy both the image's load state and a scene-specific
// pixel condition. Image.Ready alone does not mean the pixels were rendered.
enum class ReadyImageCaptureStatus { Captured, ImageError, TargetDestroyed, TimedOut };

struct ReadyImageCapture {
    ReadyImageCaptureStatus status = ReadyImageCaptureStatus::TimedOut;
    QImage image;
};

inline ReadyImageCapture captureReadyImage(QQuickWindow* window, QQuickItem* item,
                                           const std::function<bool(const QImage&)>& pixelsMatch,
                                           int timeoutMs = 5000) {
    const QPointer<QQuickWindow> guardedWindow(window);
    const QPointer<QQuickItem> guardedItem(item);
    ReadyImageCapture result;
    (void)QTest::qWaitFor([&] {
        if (!guardedWindow || !guardedItem) {
            result.status = ReadyImageCaptureStatus::TargetDestroyed;
            return true;
        }
        const int status = guardedItem->property("status").toInt();
        if (status == 3) { // Image.Error
            result.status = ReadyImageCaptureStatus::ImageError;
            return true;
        }
        if (status != 1) return false; // Image.Ready
        guardedWindow->requestUpdate();
        auto frame = guardedWindow->grabWindow();
        if (frame.isNull() || !pixelsMatch(frame)) return false;
        result.status = ReadyImageCaptureStatus::Captured;
        result.image = std::move(frame);
        return true;
    }, timeoutMs);
    return result;
}
