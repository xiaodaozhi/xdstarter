#include "windows_application_platform.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cstring>
#include <utility>

#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <propkey.h>

namespace xdstarter {
namespace {

constexpr int maxApplications = 4096;
constexpr int maxDirectories = 512;
constexpr int maxDepth = 12;
constexpr int maxEntries = 32768;

template <typename T> class ComPtr {
  public:
    ~ComPtr() {
        if (pointer_) {
            pointer_->Release();
        }
    }
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* operator->() const { return pointer_; }
    T* get() const { return pointer_; }
    T** put() { return &pointer_; }

  private:
    T* pointer_ = nullptr;
};

class ComApartment {
  public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComApartment() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }
    [[nodiscard]] bool ready() const { return SUCCEEDED(result_); }
    [[nodiscard]] HRESULT result() const { return result_; }

  private:
    HRESULT result_;
};

template <typename T> class CoMemory {
  public:
    ~CoMemory() { CoTaskMemFree(pointer); }
    T* pointer = nullptr;
};

class Bitmap {
  public:
    ~Bitmap() {
        if (handle) {
            DeleteObject(handle);
        }
    }
    HBITMAP handle = nullptr;
};

class Icon {
  public:
    ~Icon() {
        if (handle) {
            DestroyIcon(handle);
        }
    }
    HICON handle = nullptr;
};

QString errorDescription(HRESULT code) {
    wchar_t* message = nullptr;
    const DWORD systemCode =
        HRESULT_FACILITY(code) == FACILITY_WIN32 ? HRESULT_CODE(code) : static_cast<DWORD>(code);
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, systemCode, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    const QString description = length ? QString::fromWCharArray(message).trimmed() : QString();
    LocalFree(message);
    return description.isEmpty() ? QStringLiteral("系统错误 0x%1")
                                       .arg(static_cast<quint32>(code), 8, 16, QLatin1Char('0'))
                                 : description;
}

LaunchResult failure(const QString& code, const QString& description) {
    return {false, description, code};
}

bool accessDenied(HRESULT result) {
    return result == E_ACCESSDENIED || result == STG_E_ACCESSDENIED ||
           result == HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD);
}

LaunchResult fileUnavailable(DWORD error, const QString& missingCode,
                             const QString& missingDescription,
                             const QString& unavailableDescription) {
    const auto result = HRESULT_FROM_WIN32(error);
    if (accessDenied(result)) {
        return failure(QStringLiteral("access-denied"),
                       QStringLiteral("没有权限访问应用或快捷方式：") + errorDescription(result));
    }
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return failure(missingCode, missingDescription);
    }
    return failure(QStringLiteral("target-unavailable"),
                   unavailableDescription + errorDescription(result));
}

QString nativePath(const QString& path) { return QDir::toNativeSeparators(path); }

QString displayName(IShellItem* item, const QString& fallback) {
    CoMemory<wchar_t> name;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name.pointer)) && name.pointer) {
        const QString result = QString::fromWCharArray(name.pointer).trimmed();
        if (!result.isEmpty()) {
            return result;
        }
    }
    return fallback;
}

HRESULT shellItem(const LaunchTarget& target, IShellItem** item) {
    const QString parsingName = target.type == QStringLiteral("packaged")
                                    ? QStringLiteral("shell:AppsFolder\\") + target.aumid
                                    : nativePath(target.shortcutPath);
    return SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(parsingName.utf16()), nullptr,
                                       IID_PPV_ARGS(item));
}

LaunchResult shortcutAvailable(const LaunchTarget& target) {
    const QString path = nativePath(target.shortcutPath);
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return fileUnavailable(GetLastError(), QStringLiteral("shortcut-missing"),
                               QStringLiteral("快捷方式已移动或删除。"),
                               QStringLiteral("无法访问快捷方式："));
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        return failure(QStringLiteral("shortcut-invalid"),
                       QStringLiteral("快捷方式路径指向目录，不能启动。"));
    }
    ComPtr<IShellLinkW> link;
    HRESULT result =
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put()));
    ComPtr<IPersistFile> fileLink;
    if (SUCCEEDED(result)) {
        result = link->QueryInterface(IID_PPV_ARGS(fileLink.put()));
    }
    if (SUCCEEDED(result)) {
        result = fileLink->Load(reinterpret_cast<LPCWSTR>(path.utf16()), STGM_READ);
    }
    if (FAILED(result)) {
        return failure(accessDenied(result) ? QStringLiteral("access-denied")
                                            : QStringLiteral("shortcut-invalid"),
                       QStringLiteral("无法读取快捷方式：") + errorDescription(result));
    }
    wchar_t rawPath[32768] = {};
    WIN32_FIND_DATAW data = {};
    if (SUCCEEDED(link->GetPath(rawPath, 32768, &data, SLGP_RAWPATH)) && rawPath[0]) {
        wchar_t expandedPath[32768] = {};
        const DWORD length = ExpandEnvironmentStringsW(rawPath, expandedPath, 32768);
        if (!length || length > 32768) {
            return failure(QStringLiteral("shortcut-invalid"),
                           QStringLiteral("快捷方式目标路径无法展开。"));
        }
        const auto targetAttributes = GetFileAttributesW(expandedPath);
        if (targetAttributes == INVALID_FILE_ATTRIBUTES) {
            return fileUnavailable(GetLastError(), QStringLiteral("target-missing"),
                                   QStringLiteral("快捷方式指向的应用已移动或删除。"),
                                   QStringLiteral("无法访问快捷方式指向的应用："));
        }
        const auto suffix = QFileInfo(QString::fromWCharArray(expandedPath)).suffix();
        if (!(targetAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            (suffix.compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0 ||
             suffix.compare(QStringLiteral("com"), Qt::CaseInsensitive) == 0)) {
            // ShellExecute can collapse an execution DACL denial into E_FAIL.
            // A definite native execute denial is useful before dispatch; other
            // open errors still belong to Shell (including app execution aliases).
            const HANDLE executable = CreateFileW(
                expandedPath, FILE_EXECUTE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (executable == INVALID_HANDLE_VALUE) {
                const auto error = HRESULT_FROM_WIN32(GetLastError());
                if (accessDenied(error))
                    return failure(QStringLiteral("access-denied"),
                                   QStringLiteral("没有权限启动快捷方式指向的应用：") +
                                       errorDescription(error));
            } else {
                CloseHandle(executable);
            }
        }
    } else {
        CoMemory<ITEMIDLIST> idList;
        ComPtr<IShellItem> item;
        if (FAILED(link->GetIDList(&idList.pointer)) || !idList.pointer ||
            FAILED(SHCreateItemFromIDList(idList.pointer, IID_PPV_ARGS(item.put())))) {
            return failure(QStringLiteral("target-missing"),
                           QStringLiteral("快捷方式没有可用的启动目标。"));
        }
    }
    return {true, QStringLiteral("应用可用。"), {}};
}

LaunchResult packagedAvailable(const LaunchTarget& target) {
    ComPtr<IShellItem> item;
    const HRESULT result = shellItem(target, item.put());
    if (FAILED(result)) {
        return failure(accessDenied(result) ? QStringLiteral("access-denied")
                                            : QStringLiteral("application-missing"),
                       QStringLiteral("打包应用已卸载或不可访问：") + errorDescription(result));
    }
    return {true, QStringLiteral("应用可用。"), {}};
}

QImage bitmapImage(HBITMAP bitmap) {
    BITMAP properties = {};
    if (!GetObjectW(bitmap, sizeof(properties), &properties) || properties.bmWidth <= 0 ||
        properties.bmHeight <= 0 || properties.bmWidth > 1024 || properties.bmHeight > 1024) {
        return {};
    }
    QImage image(properties.bmWidth, properties.bmHeight, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return {};
    }
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = properties.bmWidth;
    info.bmiHeader.biHeight = -properties.bmHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC context = GetDC(nullptr);
    if (!context) {
        return {};
    }
    const int rows = GetDIBits(context, bitmap, 0, static_cast<UINT>(properties.bmHeight),
                               image.bits(), &info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, context);
    if (rows != properties.bmHeight) {
        return {};
    }
    bool hasAlpha = false;
    for (int y = 0; y < image.height() && !hasAlpha; ++y) {
        const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            hasAlpha = hasAlpha || qAlpha(pixels[x]) != 0;
        }
    }
    if (!hasAlpha) {
        for (int y = 0; y < image.height(); ++y) {
            auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                pixels[x] |= 0xff000000u;
            }
        }
    }
    return image;
}

QImage iconImage(HICON icon, int size) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC context = CreateCompatibleDC(nullptr);
    Bitmap bitmap;
    bitmap.handle = CreateDIBSection(context, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!context || !bitmap.handle || !pixels) {
        if (context) {
            DeleteDC(context);
        }
        return {};
    }
    const HGDIOBJ previous = SelectObject(context, bitmap.handle);
    std::memset(pixels, 0, static_cast<size_t>(image.sizeInBytes()));
    const bool drawn = DrawIconEx(context, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL) != FALSE;
    if (drawn) {
        std::memcpy(image.bits(), pixels, static_cast<size_t>(image.sizeInBytes()));
        bool alphaPresent = false;
        const auto* values = reinterpret_cast<const QRgb*>(image.constBits());
        for (int i = 0; i < size * size; ++i) {
            alphaPresent = alphaPresent || qAlpha(values[i]) != 0;
        }
        if (!alphaPresent) {
            DrawIconEx(context, 0, 0, icon, size, size, 0, nullptr, DI_MASK);
            auto* output = reinterpret_cast<QRgb*>(image.bits());
            const auto* mask = static_cast<const QRgb*>(pixels);
            for (int i = 0; i < size * size; ++i) {
                output[i] = (mask[i] & 0x00ffffffu) ? 0 : (output[i] | 0xff000000u);
            }
        }
    }
    SelectObject(context, previous);
    DeleteDC(context);
    return drawn ? image : QImage();
}

bool scanShortcutDirectory(const QString& path, int depth, int& directories, int& entriesInspected,
                           bool& truncated, CatalogSnapshot& snapshot, QSet<QString>& visited,
                           QHash<QString, qsizetype>& folders, const QString& parentFolderId = {},
                           const QString& relativePath = {}) {
    if (depth > maxDepth || directories >= maxDirectories ||
        snapshot.applications.size() >= maxApplications || entriesInspected >= maxEntries) {
        truncated = true;
        return false;
    }
    const QString native = nativePath(path);
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        snapshot.warnings.append(QStringLiteral("无法读取开始菜单目录：") + path +
                                 QStringLiteral("：") +
                                 errorDescription(HRESULT_FROM_WIN32(GetLastError())));
        return false;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return false;
    const QString identity = QDir::cleanPath(path).toCaseFolded();
    if (visited.contains(identity))
        return true;
    ++directories;
    const QString pattern = nativePath(QDir(path).filePath(QStringLiteral("*")));
    WIN32_FIND_DATAW data = {};
    const HANDLE search = FindFirstFileW(reinterpret_cast<LPCWSTR>(pattern.utf16()), &data);
    if (search == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            visited.insert(identity);
            return true;
        }
        snapshot.warnings.append(QStringLiteral("无法枚举开始菜单目录：") + path +
                                 QStringLiteral("：") +
                                 errorDescription(HRESULT_FROM_WIN32(error)));
        return false;
    }
    struct SearchHandle {
        HANDLE value;
        ~SearchHandle() { FindClose(value); }
    } searchHandle{search};
    visited.insert(identity);
    while (true) {
        const QString name = QString::fromWCharArray(data.cFileName);
        if (name != QLatin1String(".") && name != QLatin1String("..")) {
            if (snapshot.applications.size() >= maxApplications || entriesInspected >= maxEntries) {
                truncated = true;
                break;
            }
            ++entriesInspected;
            const QFileInfo entry(QDir(path).filePath(name));
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                entry.suffix().compare(QStringLiteral("lnk"), Qt::CaseInsensitive) == 0) {
                LaunchTarget target;
                target.type = QStringLiteral("shortcut");
                target.shortcutPath = QDir::fromNativeSeparators(entry.absoluteFilePath());
                target.id = applicationTargetId(target.type, target.shortcutPath);
                ComPtr<IShellItem> item;
                target.name = SUCCEEDED(shellItem(target, item.put()))
                                  ? displayName(item.get(), entry.completeBaseName())
                                  : entry.completeBaseName();
                target.symbol = target.name.first(1);
                target.iconKey = target.id;
                target.parentFolderId = parentFolderId;
                snapshot.applications.append(std::move(target));
            } else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
                       !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                LaunchTarget folder;
                folder.type = QStringLiteral("folder");
                folder.symbol = QStringLiteral("▱");
                folder.shortcutPath = QDir::fromNativeSeparators(entry.absoluteFilePath());
                ComPtr<IShellItem> item;
                folder.name =
                    SUCCEEDED(shellItem(folder, item.put())) ? displayName(item.get(), name) : name;
                folder.id = applicationTargetId(folder.type, folder.shortcutPath);
                folder.iconKey = folder.id;
                folder.parentFolderId = parentFolderId;
                const auto relative =
                    relativePath.isEmpty() ? name : relativePath + QLatin1Char('/') + name;
                const auto key = QDir::cleanPath(relative).toCaseFolded();
                const auto previous = folders.constFind(key);
                if (previous != folders.cend()) {
                    auto& representative = snapshot.applications[*previous];
                    // Shell display names are presentation only. Merge by relative parsing path,
                    // never by a localized name, and keep the first real folder's stable identity.
                    if (representative.name == QFileInfo(representative.shortcutPath).fileName() &&
                        folder.name != name)
                        representative.name = folder.name;
                    folder.id = representative.id;
                } else {
                    folders.insert(key, snapshot.applications.size());
                    snapshot.applications.append(folder);
                }
                scanShortcutDirectory(entry.absoluteFilePath(), depth + 1, directories,
                                      entriesInspected, truncated, snapshot, visited, folders,
                                      folder.id, relative);
            }
        }
        if (!FindNextFileW(search, &data)) {
            const auto error = GetLastError();
            if (error != ERROR_NO_MORE_FILES)
                snapshot.warnings.append(QStringLiteral("开始菜单目录读取不完整：") + path +
                                         QStringLiteral("：") +
                                         errorDescription(HRESULT_FROM_WIN32(error)));
            break;
        }
    }
    return true;
}

bool scanPackagedApplications(CatalogSnapshot& snapshot) {
    ComPtr<IShellItem> folder;
    HRESULT result = SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr,
                                          IID_PPV_ARGS(folder.put()));
    ComPtr<IEnumShellItems> enumerator;
    if (SUCCEEDED(result)) {
        result = folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(enumerator.put()));
    }
    if (FAILED(result)) {
        snapshot.warnings.append(QStringLiteral("无法读取打包应用目录：") +
                                 errorDescription(result));
        return false;
    }
    int inspected = 0;
    while (snapshot.applications.size() < maxApplications && inspected++ < maxApplications * 2) {
        ComPtr<IShellItem> item;
        ULONG fetched = 0;
        result = enumerator->Next(1, item.put(), &fetched);
        if (result != S_OK || fetched != 1) {
            if (FAILED(result)) {
                snapshot.warnings.append(QStringLiteral("打包应用目录读取不完整：") +
                                         errorDescription(result));
            }
            break;
        }
        ComPtr<IShellItem2> properties;
        CoMemory<wchar_t> aumid;
        if (FAILED(item->QueryInterface(IID_PPV_ARGS(properties.put()))) ||
            FAILED(properties->GetString(PKEY_AppUserModel_ID, &aumid.pointer)) || !aumid.pointer) {
            continue;
        }
        LaunchTarget target;
        target.type = QStringLiteral("packaged");
        target.aumid = QString::fromWCharArray(aumid.pointer);
        target.id = applicationTargetId(target.type, target.aumid);
        target.name = displayName(item.get(), target.aumid);
        target.symbol = target.name.first(1);
        target.iconKey = target.id;
        if (isValidApplicationTarget(target)) {
            snapshot.applications.append(std::move(target));
        }
    }
    return true;
}

} // namespace

WindowsApplicationPlatform::WindowsApplicationPlatform(QStringList shortcutRoots,
                                                       bool includePackaged)
    : shortcutRoots_(std::move(shortcutRoots)), explicitRoots_(true),
      includePackaged_(includePackaged) {}

CatalogSnapshot WindowsApplicationPlatform::scanApplications() {
    CatalogSnapshot snapshot;
    ComApartment apartment;
    if (!apartment.ready()) {
        snapshot.warnings.append(QStringLiteral("无法初始化应用目录线程：") +
                                 errorDescription(apartment.result()));
        return snapshot;
    }
    QStringList roots = shortcutRoots_;
    if (!explicitRoots_) {
        for (const KNOWNFOLDERID* identifier : {&FOLDERID_Programs, &FOLDERID_CommonPrograms}) {
            CoMemory<wchar_t> path;
            const HRESULT result =
                SHGetKnownFolderPath(*identifier, KF_FLAG_DEFAULT, nullptr, &path.pointer);
            if (SUCCEEDED(result) && path.pointer) {
                roots.append(QString::fromWCharArray(path.pointer));
            } else {
                snapshot.warnings.append(QStringLiteral("无法读取开始菜单目录：") +
                                         errorDescription(result));
            }
        }
    }
    int directories = 0;
    int entriesInspected = 0;
    bool truncated = false;
    QSet<QString> visited;
    QHash<QString, qsizetype> folders;
    bool readableRoot = false;
    for (const QString& root : roots) {
        if (!QFileInfo(root).isDir()) {
            snapshot.warnings.append(QStringLiteral("开始菜单目录不可用：") + root);
            continue;
        }
        readableRoot = scanShortcutDirectory(root, 0, directories, entriesInspected, truncated,
                                             snapshot, visited, folders) ||
                       readableRoot;
    }
    const bool packagedRead = includePackaged_ && scanPackagedApplications(snapshot);
    snapshot.success = readableRoot || packagedRead;
    if (truncated || snapshot.applications.size() >= maxApplications) {
        snapshot.warnings.append(QStringLiteral("应用目录达到扫描上限，部分项目未载入。"));
    }
    snapshot.applications = normalizedApplications(snapshot.applications);
    return snapshot;
}

QImage WindowsApplicationPlatform::loadIcon(const LaunchTarget& target, int size) {
    if (!isValidCatalogEntry(target) || size < 16 || size > 256) {
        return {};
    }
    ComApartment apartment;
    if (!apartment.ready()) {
        return {};
    }
    ComPtr<IShellItem> item;
    ComPtr<IShellItemImageFactory> factory;
    Bitmap bitmap;
    if (SUCCEEDED(shellItem(target, item.put())) &&
        SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(factory.put()))) &&
        SUCCEEDED(factory->GetImage({size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK,
                                    &bitmap.handle))) {
        QImage image = bitmapImage(bitmap.handle);
        if (!image.isNull()) {
            return image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    if (target.type == QStringLiteral("shortcut")) {
        const QString path = nativePath(target.shortcutPath);
        SHFILEINFOW info = {};
        if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(path.utf16()), 0, &info, sizeof(info),
                           SHGFI_ICON | SHGFI_LARGEICON)) {
            Icon icon;
            icon.handle = info.hIcon;
            return iconImage(icon.handle, size);
        }
    }
    return {};
}

LaunchResult WindowsApplicationPlatform::checkAvailability(const LaunchTarget& target) {
    if (!isValidApplicationTarget(target)) {
        return failure(QStringLiteral("invalid-target"), QStringLiteral("应用启动描述无效。"));
    }
    ComApartment apartment;
    if (!apartment.ready()) {
        return failure(QStringLiteral("com-initialization"),
                       QStringLiteral("无法初始化应用线程：") +
                           errorDescription(apartment.result()));
    }
    return target.type == QStringLiteral("shortcut") ? shortcutAvailable(target)
                                                     : packagedAvailable(target);
}

LaunchResult WindowsApplicationPlatform::launch(const LaunchTarget& target) {
    if (!isValidApplicationTarget(target)) {
        return failure(QStringLiteral("invalid-target"), QStringLiteral("应用启动描述无效。"));
    }
    ComApartment apartment;
    if (!apartment.ready()) {
        return failure(QStringLiteral("com-initialization"),
                       QStringLiteral("无法初始化应用启动线程：") +
                           errorDescription(apartment.result()));
    }
    const LaunchResult available = target.type == QStringLiteral("shortcut")
                                       ? shortcutAvailable(target)
                                       : packagedAvailable(target);
    if (!available.success) {
        return available;
    }
    if (target.type == QStringLiteral("shortcut")) {
        const QString path = nativePath(target.shortcutPath);
        SHELLEXECUTEINFOW execution = {};
        execution.cbSize = sizeof(execution);
        execution.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        execution.lpVerb = L"open";
        execution.lpFile = reinterpret_cast<LPCWSTR>(path.utf16());
        execution.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&execution)) {
            const auto result = HRESULT_FROM_WIN32(GetLastError());
            return failure(accessDenied(result) ? QStringLiteral("access-denied")
                                                : QStringLiteral("shell-execute"),
                           QStringLiteral("应用启动失败：") + errorDescription(result));
        }
    } else {
        ComPtr<IApplicationActivationManager> activation;
        HRESULT result = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
                                          CLSCTX_INPROC_SERVER, IID_PPV_ARGS(activation.put()));
        DWORD process = 0;
        if (SUCCEEDED(result)) {
            result = activation->ActivateApplication(
                reinterpret_cast<LPCWSTR>(target.aumid.utf16()), nullptr, AO_NOERRORUI, &process);
        }
        if (FAILED(result)) {
            return failure(accessDenied(result) ? QStringLiteral("access-denied")
                                                : QStringLiteral("application-activation"),
                           QStringLiteral("打包应用启动失败：") + errorDescription(result));
        }
    }
    return {true, QStringLiteral("已请求启动：") + target.name, {}};
}

} // namespace xdstarter
