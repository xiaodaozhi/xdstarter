// Disposable hosted-only UI Automation driver. A separate process bounds any
// provider call that blocks while the selected dialog is closing.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>

#include <cstdint>
#include <cwchar>
#include <iostream>
#include <string>

namespace {
bool hosted() {
    wchar_t actions[16]{}, runner[32]{};
    GetEnvironmentVariableW(L"GITHUB_ACTIONS", actions, 16);
    GetEnvironmentVariableW(L"RUNNER_ENVIRONMENT", runner, 32);
    return std::wstring(actions) == L"true" && std::wstring(runner) == L"github-hosted";
}

template <typename T> struct Com {
    T* value = nullptr;
    ~Com() { if (value) value->Release(); }
};

std::string printableId(const wchar_t* text) {
    std::string result;
    if (!text) return result;
    for (std::size_t index = 0; text[index] && index < 128; ++index)
        result.push_back(text[index] >= 32 && text[index] < 127 ? static_cast<char>(text[index]) : '?');
    return result;
}

HRESULT findById(IUIAutomation* automation, IUIAutomationElement* root,
                 const wchar_t* id, DWORD pid, CONTROLTYPEID expected, IUIAutomationElement** result) {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(id);
    Com<IUIAutomationCondition> condition;
    HRESULT hr = value.bstrVal ? automation->CreatePropertyCondition(
        UIA_AutomationIdPropertyId, value, &condition.value) : E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) hr = root->FindFirst(TreeScope_Descendants, condition.value, result);
    int actualPid = 0;
    CONTROLTYPEID actualType = 0;
    if (SUCCEEDED(hr) && *result) hr = (*result)->get_CurrentProcessId(&actualPid);
    if (SUCCEEDED(hr) && *result) hr = (*result)->get_CurrentControlType(&actualType);
    std::cout << "find id=" << printableId(id) << " found=" << bool(*result) << " pid=" << actualPid
              << " type=" << actualType << " hr=0x" << std::hex << static_cast<unsigned long>(hr)
              << std::dec << std::endl;
    if (SUCCEEDED(hr) && (!*result || actualPid != static_cast<int>(pid) ||
                          (expected && actualType != expected)))
        hr = E_FAIL;
    VariantClear(&value);
    return hr;
}

void describeDescendants(IUIAutomation* automation, IUIAutomationElement* root) {
    Com<IUIAutomationCondition> all;
    Com<IUIAutomationElementArray> elements;
    if (FAILED(automation->CreateTrueCondition(&all.value)) ||
        FAILED(root->FindAll(TreeScope_Descendants, all.value, &elements.value)) || !elements.value)
        return;
    int count = 0;
    if (FAILED(elements.value->get_Length(&count))) return;
    std::cout << "uia-descendant-count=" << count << std::endl;
    for (int index = 0; index < count && index < 200; ++index) {
        Com<IUIAutomationElement> element;
        if (FAILED(elements.value->GetElement(index, &element.value)) || !element.value) continue;
        BSTR id = nullptr, className = nullptr;
        int pid = 0;
        CONTROLTYPEID type = 0;
        element.value->get_CurrentAutomationId(&id);
        element.value->get_CurrentClassName(&className);
        element.value->get_CurrentProcessId(&pid);
        element.value->get_CurrentControlType(&type);
        std::cout << "uia id=" << printableId(id) << " class=" << printableId(className)
                  << " pid=" << pid << " type=" << type << std::endl;
        if (id) SysFreeString(id);
        if (className) SysFreeString(className);
    }
}

HRESULT findChildEdit(IUIAutomation* automation, IUIAutomationElement* parent,
                      DWORD pid, IUIAutomationElement** result) {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = UIA_EditControlTypeId;
    Com<IUIAutomationCondition> condition;
    HRESULT hr = automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &condition.value);
    if (SUCCEEDED(hr)) hr = parent->FindFirst(TreeScope_Descendants, condition.value, result);
    int actualPid = 0;
    if (SUCCEEDED(hr) && *result) hr = (*result)->get_CurrentProcessId(&actualPid);
    if (SUCCEEDED(hr) && (!*result || actualPid != static_cast<int>(pid))) hr = E_FAIL;
    return hr;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (!hosted()) {
        std::cerr << "Only a disposable GitHub-hosted Windows runner may execute this fixture.\n";
        return 2;
    }
    if (argc != 6) return 2;
    wchar_t* end = nullptr;
    const auto rawDialog = _wcstoui64(argv[1], &end, 10);
    if (!end || *end || !rawDialog) return 2;
    const auto pid = wcstoul(argv[2], &end, 10);
    if (!end || *end || !pid) return 2;
    const auto rawOwner = _wcstoui64(argv[3], &end, 10);
    if (!end || *end || !rawOwner) return 2;
    const auto ownerPid = wcstoul(argv[4], &end, 10);
    if (!end || *end || !ownerPid || !*argv[5]) return 2;
    const HWND dialog = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(rawDialog));
    const HWND owner = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(rawOwner));
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    DWORD actualPid = 0, actualOwnerPid = 0;
    const bool valid = process && GetProcessId(process) == pid &&
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT && IsWindow(dialog) && IsWindow(owner) &&
        GetWindowThreadProcessId(dialog, &actualPid) && actualPid == pid &&
        GetWindow(dialog, GW_OWNER) == owner && GetWindowThreadProcessId(owner, &actualOwnerPid) &&
        actualOwnerPid == ownerPid;
    if (!valid) { if (process) CloseHandle(process); return 3; }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) { CloseHandle(process); return 4; }
    HRESULT hr = E_FAIL;
    {
        Com<IUIAutomation> automation;
        Com<IUIAutomationElement> root, fileName, edit, open;
        Com<IUIAutomationValuePattern> value;
        Com<IUIAutomationInvokePattern> invoke;
        const char* stage = "create-automation";
        hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation.value));
        if (SUCCEEDED(hr) && !automation.value) hr = E_FAIL;
        if (SUCCEEDED(hr)) stage = "dialog-root";
        if (SUCCEEDED(hr)) hr = automation.value->ElementFromHandle(dialog, &root.value);
        if (SUCCEEDED(hr) && !root.value) hr = E_FAIL;
        if (SUCCEEDED(hr)) stage = "filename-control";
        if (SUCCEEDED(hr)) hr = findById(automation.value, root.value, L"1148", pid,
                                         0, &fileName.value);
        if (SUCCEEDED(hr)) stage = "filename-value-pattern";
        if (SUCCEEDED(hr)) {
            hr = fileName.value->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value.value));
            if (SUCCEEDED(hr) && !value.value) hr = E_FAIL;
            if (FAILED(hr)) {
                hr = findChildEdit(automation.value, fileName.value, pid, &edit.value);
                if (SUCCEEDED(hr)) hr = edit.value->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value.value));
                if (SUCCEEDED(hr) && !value.value) hr = E_FAIL;
            }
        }
        if (SUCCEEDED(hr)) stage = "set-filename";
        if (SUCCEEDED(hr)) hr = value.value->SetValue(argv[5]);
        if (SUCCEEDED(hr)) stage = "open-control";
        if (SUCCEEDED(hr)) hr = findById(automation.value, root.value, L"1", pid,
                                         UIA_ButtonControlTypeId, &open.value);
        if (SUCCEEDED(hr)) stage = "open-invoke-pattern";
        if (SUCCEEDED(hr)) hr = open.value->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke.value));
        if (SUCCEEDED(hr) && !invoke.value) hr = E_FAIL;
        if (SUCCEEDED(hr)) stage = "invoke-open";
        if (SUCCEEDED(hr)) hr = invoke.value->Invoke();
        std::cout << "automation-stage=" << stage << " hr=0x" << std::hex
                  << static_cast<unsigned long>(hr) << std::dec << std::endl;
        if (FAILED(hr) && automation.value && root.value)
            describeDescendants(automation.value, root.value);
    }
    std::cout << "automation dialog=" << dialog << " pid=" << pid << " owner=" << owner
              << " result=0x" << std::hex << static_cast<unsigned long>(hr) << std::endl;
    CoUninitialize();
    CloseHandle(process);
    return SUCCEEDED(hr) ? 0 : 5;
}
