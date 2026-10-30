#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace xdstarter {

class NativeLayoutPickerPrivate;

// The Windows implementation uses an isolated OS-dialog process. Other Qt
// platforms keep using the existing Quick FileDialog through the QML wrapper.
class NativeLayoutPicker : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
    Q_PROPERTY(quint32 currentProcessId READ currentProcessId NOTIFY currentProcessIdChanged)
    Q_PROPERTY(QWindow* parentWindow READ parentWindow WRITE setParentWindow NOTIFY parentWindowChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString jsonFilter READ jsonFilter WRITE setJsonFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString allFilter READ allFilter WRITE setAllFilter NOTIFY filtersChanged)
    Q_PROPERTY(QUrl selectedFile READ selectedFile NOTIFY selectedFileChanged)

  public:
    explicit NativeLayoutPicker(QObject* parent = nullptr);
    ~NativeLayoutPicker() override;
    [[nodiscard]] bool supported() const;
    [[nodiscard]] bool visible() const;
    [[nodiscard]] quint32 currentProcessId() const;
    [[nodiscard]] QWindow* parentWindow() const;
    void setParentWindow(QWindow* value);
    [[nodiscard]] QString title() const;
    void setTitle(const QString& value);
    [[nodiscard]] QString jsonFilter() const;
    void setJsonFilter(const QString& value);
    [[nodiscard]] QString allFilter() const;
    void setAllFilter(const QString& value);
    [[nodiscard]] QUrl selectedFile() const;
    Q_INVOKABLE void open();
    Q_INVOKABLE void close();

  signals:
    void visibleChanged();
    void currentProcessIdChanged();
    void parentWindowChanged();
    void titleChanged();
    void filtersChanged();
    void selectedFileChanged();
    void accepted();
    void rejected();
    void failed();

  private:
    std::unique_ptr<NativeLayoutPickerPrivate> d;
};

} // namespace xdstarter
