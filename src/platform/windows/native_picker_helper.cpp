#include "native_picker_protocol.h"

#include <objbase.h>
#include <shobjidl.h>

#include <cerrno>
#include <cstdlib>
#include <cwchar>

namespace xdstarter::native_picker {
namespace {

bool bounded(const wchar_t* text, std::size_t capacity) {
    return text && wmemchr(text, L'\0', capacity) != nullptr;
}

} // namespace

int runHelper(const char* mappingHandle) {
    if (!mappingHandle || !*mappingHandle || *mappingHandle == '-')
        return 2;
    char* end = nullptr;
    errno = 0;
    const auto raw = std::strtoull(mappingHandle, &end, 10);
    if (errno || !end || *end || !raw || raw > UINTPTR_MAX)
        return 2;
    const HANDLE mapping = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(raw));
    auto* shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (!shared)
        return 2;
    const auto finish = [shared](Result result, HRESULT error) {
        shared->error = static_cast<LONG>(error);
        InterlockedExchange(&shared->result, static_cast<LONG>(result));
        UnmapViewOfFile(shared);
        return result == Result::Failed ? 1 : 0;
    };
    if (shared->signature != magic || shared->size != sizeof(Shared) ||
        shared->result != static_cast<LONG>(Result::Pending) ||
        !bounded(shared->title, textCapacity) || !bounded(shared->jsonFilter, textCapacity) ||
        !bounded(shared->allFilter, textCapacity))
        return finish(Result::Failed, E_INVALIDARG);

    const HWND owner = reinterpret_cast<HWND>(shared->owner);
    const HANDLE parent = reinterpret_cast<HANDLE>(shared->parentProcess);
    DWORD ownerPid = 0;
    const DWORD parentPid = GetProcessId(parent);
    if (!parentPid || WaitForSingleObject(parent, 0) != WAIT_TIMEOUT || !IsWindow(owner) ||
        !GetWindowThreadProcessId(owner, &ownerPid) || ownerPid != parentPid)
        return finish(Result::Failed, E_INVALIDARG);

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized))
        return finish(Result::Failed, initialized);
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr)) hr = dialog->SetTitle(shared->title);
    if (SUCCEEDED(hr)) hr = dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                                                FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR);
    const COMDLG_FILTERSPEC filters[] = {{shared->jsonFilter, L"*.json"},
                                         {shared->allFilter, L"*"}};
    if (SUCCEEDED(hr)) hr = dialog->SetFileTypes(2, filters);
    if (SUCCEEDED(hr)) hr = dialog->Show(owner);

    Result result = Result::Failed;
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        result = Result::Rejected;
    } else if (hr == S_OK) {
        IShellItem* item = nullptr;
        PWSTR path = nullptr;
        hr = dialog->GetResult(&item);
        if (SUCCEEDED(hr) && item) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        if (SUCCEEDED(hr) && path && bounded(path, pathCapacity) && *path &&
            wcscpy_s(shared->path, pathCapacity, path) == 0) {
            result = Result::Accepted;
        } else if (SUCCEEDED(hr)) {
            hr = E_FAIL;
        }
        if (path) CoTaskMemFree(path);
        if (item) item->Release();
    }
    if (dialog) dialog->Release();
    CoUninitialize();
    return finish(result, hr);
}

} // namespace xdstarter::native_picker
