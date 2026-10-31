// Read only the UI Automation tree of the QtTest process that launched us.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <cwchar>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
DWORD parentProcessId() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD parent = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == GetCurrentProcessId()) {
                parent = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return parent;
}

bool isOwnedTestProcess(DWORD pid) {
    if (!pid || parentProcessId() != pid) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{};
    DWORD length = DWORD(std::size(path));
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    const bool known = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    const auto* name = wcsrchr(path, L'\\');
    return alive && known && name && _wcsicmp(name + 1, L"xdstarter_ui_tests.exe") == 0;
}

struct OwnedWindows {
    DWORD pid;
    std::vector<HWND> handles;
};

BOOL CALLBACK collectOwned(HWND hwnd, LPARAM parameter) {
    auto& owned = *reinterpret_cast<OwnedWindows*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == owned.pid) owned.handles.push_back(hwnd);
    return TRUE;
}

int inspect(DWORD pid, const std::wstring& field) {
    const wchar_t* name = nullptr;
    const wchar_t* expected = nullptr;
    if (field == L"search") {
        name = L"搜索应用";
        expected = L"输入名称、拼音或首字母。向下键选择结果，Enter 启动，Esc 清空搜索。";
    } else if (field == L"group") {
        name = L"分组名称";
        expected = L"输入名称后按 Enter 保存，Esc 取消。允许重名。";
    } else if (field == L"tile") {
        name = L"磁贴显示名称";
        expected = L"输入名称后按 Enter 保存，Esc 取消。原名称保留为搜索别名。";
    } else {
        return 2;
    }

    ComPtr<IUIAutomation> automation;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&automation));
    if (FAILED(hr)) return 3;
    ComPtr<IUIAutomation2> bounded;
    if (SUCCEEDED(automation.As(&bounded))) {
        bounded->put_ConnectionTimeout(2000);
        bounded->put_TransactionTimeout(2000);
    }
    VARIANT searchName;
    VariantInit(&searchName);
    searchName.vt = VT_BSTR;
    searchName.bstrVal = SysAllocString(name);
    if (!searchName.bstrVal) return 4;
    ComPtr<IUIAutomationCondition> condition;
    hr = automation->CreatePropertyCondition(UIA_NamePropertyId, searchName, &condition);
    VariantClear(&searchName);
    if (FAILED(hr)) return 5;

    bool found = false, matched = false;
    HRESULT descriptionResult = E_FAIL;
    int valueType = -1;
    CONTROLTYPEID controlType = 0;
    for (int attempt = 0; attempt < 40 && !found; ++attempt) {
        OwnedWindows owned{pid, {}};
        EnumWindows(collectOwned, reinterpret_cast<LPARAM>(&owned));
        for (const HWND hwnd : owned.handles) {
            ComPtr<IUIAutomationElement> root, result;
            if (FAILED(automation->ElementFromHandle(hwnd, &root)) || !root) continue;
            int rootPid = 0, resultPid = 0;
            if (FAILED(root->get_CurrentProcessId(&rootPid)) || DWORD(rootPid) != pid ||
                FAILED(root->FindFirst(TreeScope_Descendants, condition.Get(), &result)) || !result ||
                FAILED(result->get_CurrentProcessId(&resultPid)) || DWORD(resultPid) != pid)
                continue;
            result->get_CurrentControlType(&controlType);
            VARIANT description;
            VariantInit(&description);
            descriptionResult = result->GetCurrentPropertyValue(UIA_FullDescriptionPropertyId, &description);
            valueType = description.vt;
            matched = SUCCEEDED(descriptionResult) && description.vt == VT_BSTR && description.bstrVal &&
                      std::wstring(description.bstrVal, SysStringLen(description.bstrVal)) == expected;
            VariantClear(&description);
            found = true;
            break;
        }
        if (!found) Sleep(80);
    }
    std::cout << "matched=" << matched << " found=" << found << " descriptionHresult="
              << std::hex << descriptionResult << std::dec << " variantType=" << valueType
              << " controlType=" << controlType << '\n';
    return found && matched && controlType == UIA_EditControlTypeId ? 0 : 1;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    wchar_t* end = nullptr;
    const auto pid = wcstoul(argv[1], &end, 10);
    if (!end || *end || !isOwnedTestProcess(pid)) return 2;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 3;
    const int result = inspect(pid, argv[2]);
    CoUninitialize();
    return result;
}
