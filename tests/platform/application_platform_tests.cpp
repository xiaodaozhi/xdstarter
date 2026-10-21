#include "platform/application_platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>
#include <string_view>

#ifdef Q_OS_WIN
#include "platform/windows/windows_application_platform.h"

#include <windows.h>

#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace {

class TestContext {
  public:
    void expect(bool condition, std::string_view description) {
        if (!condition) {
            std::cerr << "FAIL: " << description << '\n';
            ++failures;
        }
    }
    int failures = 0;
};

xdstarter::LaunchTarget shortcut(const QString& path, const QString& name) {
    xdstarter::LaunchTarget target;
    target.type = QStringLiteral("shortcut");
    target.name = name;
    target.shortcutPath = QDir::fromNativeSeparators(path);
    target.id = xdstarter::applicationTargetId(target.type, target.shortcutPath);
    target.iconKey = target.id;
    return target;
}

#ifdef Q_OS_WIN
bool createShortcut(const QString& path, const QString& executable, const QString& arguments,
                    const QString& workingDirectory) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        return false;
    }
    IShellLinkW* link = nullptr;
    HRESULT result =
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (SUCCEEDED(result)) {
        const QString executablePath = QDir::toNativeSeparators(executable);
        const QString directory = QDir::toNativeSeparators(workingDirectory);
        result = link->SetPath(reinterpret_cast<LPCWSTR>(executablePath.utf16()));
        if (SUCCEEDED(result)) {
            result = link->SetArguments(reinterpret_cast<LPCWSTR>(arguments.utf16()));
        }
        if (SUCCEEDED(result)) {
            result = link->SetWorkingDirectory(reinterpret_cast<LPCWSTR>(directory.utf16()));
        }
        IPersistFile* file = nullptr;
        if (SUCCEEDED(result)) {
            result = link->QueryInterface(IID_PPV_ARGS(&file));
        }
        if (SUCCEEDED(result)) {
            const QString native = QDir::toNativeSeparators(path);
            result = file->Save(reinterpret_cast<LPCWSTR>(native.utf16()), TRUE);
        }
        if (file) {
            file->Release();
        }
        link->Release();
    }
    CoUninitialize();
    return SUCCEEDED(result);
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    TestContext test;
    auto first = shortcut(QStringLiteral("C:/目录/应用.lnk"), QStringLiteral("应用"));
    auto same = shortcut(QStringLiteral("c:\\目录\\应用.LNK"), QStringLiteral("重复名称"));
    auto other =
        shortcut(QStringLiteral("C:/目录/应用第二参数.lnk"), QStringLiteral("同程序不同参数"));
    test.expect(first.id == same.id, "identity normalizes case and native separators");
    test.expect(first.id != other.id, "different shortcut paths retain separate identities");
    test.expect(xdstarter::isValidApplicationTarget(first), "Chinese absolute shortcut is valid");
    const auto normalized = xdstarter::normalizedApplications({first, same, other});
    test.expect(normalized.size() == 2,
                "catalog deduplicates identities without merging shortcuts");
    auto invalid = first;
    invalid.shortcutPath = QStringLiteral("cmd.exe /c calc.exe");
    test.expect(!xdstarter::isValidApplicationTarget(invalid), "raw command is rejected");
    invalid = first;
    invalid.shortcutPath = QStringLiteral("shell:AppsFolder\\wrong.lnk");
    test.expect(!xdstarter::isValidApplicationTarget(invalid), "shell parsing string is rejected");
    invalid = first;
    invalid.id = QStringLiteral("unrelated-id");
    test.expect(!xdstarter::isValidApplicationTarget(invalid), "mismatched identity is rejected");
    xdstarter::LaunchTarget packaged;
    packaged.type = QStringLiteral("packaged");
    packaged.name = QStringLiteral("打包应用");
    packaged.aumid = QStringLiteral("Microsoft.WindowsCalculator_8wekyb3d8bbwe!App");
    packaged.id = xdstarter::applicationTargetId(packaged.type, packaged.aumid);
    test.expect(xdstarter::isValidApplicationTarget(packaged),
                "structured packaged AUMID is valid");
    auto trailingNewline = packaged;
    trailingNewline.aumid.append(QLatin1Char('\n'));
    trailingNewline.id = xdstarter::applicationTargetId(trailingNewline.type, trailingNewline.aumid);
    test.expect(!xdstarter::isValidApplicationTarget(trailingNewline),
                "AUMID does not accept a trailing newline");
    packaged.aumid = QStringLiteral("shell:AppsFolder\\anything");
    packaged.id = xdstarter::applicationTargetId(packaged.type, packaged.aumid);
    test.expect(!xdstarter::isValidApplicationTarget(packaged),
                "AUMID cannot become a shell command");

#ifdef Q_OS_WIN
    test.expect(application.arguments().size() == 2, "controlled launch helper supplied");
    if (application.arguments().size() != 2) {
        return 1;
    }
    QTemporaryDir fixture(QDir::current().filePath(QStringLiteral("platform-fixture-XXXXXX")));
    test.expect(fixture.isValid(), "isolated fixture directory available");
    if (!fixture.isValid()) {
        return 1;
    }
    const QString root = fixture.filePath(QStringLiteral("中文 空格 & 目录"));
    test.expect(QDir().mkpath(root), "special character fixture directory created");
    const QString helper = application.arguments().at(1);
    const QString marker = fixture.filePath(QStringLiteral("启动 结果 &.json"));
    const QString linkPath = QDir(root).filePath(QStringLiteral("受控启动.lnk"));
    const QString secondPath = QDir(root).filePath(QStringLiteral("不同参数.lnk"));
    const QString missingPath = QDir(root).filePath(QStringLiteral("已删除应用.lnk"));
    const QString arguments = QStringLiteral("\"%1\" \"中文 参数\" \"--literal=one & two\"")
                                  .arg(QDir::toNativeSeparators(marker));
    test.expect(createShortcut(linkPath, helper, arguments, root), "fixture shortcut created");
    test.expect(createShortcut(secondPath, helper, QStringLiteral("different-argument"), root),
                "second shortcut retains distinct argument");
    test.expect(
        createShortcut(missingPath, QDir(root).filePath(QStringLiteral("missing.exe")), {}, root),
        "missing-target fixture created");
    xdstarter::WindowsApplicationPlatform platform({root, root});
    const auto snapshot = platform.scanApplications();
    test.expect(snapshot.success, "isolated Start Menu scan succeeds");
    test.expect(snapshot.applications.size() == 3,
                "duplicate roots deduplicated; all shortcuts retained");
    const auto target = shortcut(linkPath, QStringLiteral("受控启动"));
    test.expect(platform.checkAvailability(target).success, "valid shortcut availability resolves");
    const auto missing = shortcut(missingPath, QStringLiteral("已删除应用"));
    const auto missingResult = platform.checkAvailability(missing);
    test.expect(!missingResult.success &&
                    missingResult.errorCode == QStringLiteral("target-missing"),
                "broken shortcut reports structured unavailable state");
    test.expect(!platform.launch(missing).success, "broken shortcut cannot launch");
    test.expect(!platform.launch(invalid).success, "mismatched identity cannot launch");
    auto uninstalled = packaged;
    uninstalled.aumid = QStringLiteral("XDStarter.MissingPackage_0000000000000!App");
    uninstalled.id = xdstarter::applicationTargetId(uninstalled.type, uninstalled.aumid);
    test.expect(!platform.checkAvailability(uninstalled).success,
                "uninstalled packaged application is unavailable");
    test.expect(!platform.loadIcon(target, 64).isNull(), "Shell icon loads without Qt Widgets");
    test.expect(platform.loadIcon(target, 4096).isNull(), "icon extraction has a bounded size");
    test.expect(platform.launch(target).success, "controlled helper starts via original shortcut");
    QElapsedTimer elapsed;
    elapsed.start();
    while (!QFileInfo::exists(marker) && elapsed.elapsed() < 5000) {
        QThread::msleep(10);
    }
    QFile output(marker);
    test.expect(output.open(QIODevice::ReadOnly), "launched helper created marker");
    const auto result = QJsonDocument::fromJson(output.readAll()).object();
    const QJsonArray actualArguments = result.value(QStringLiteral("arguments")).toArray();
    test.expect(actualArguments ==
                    QJsonArray{QStringLiteral("中文 参数"), QStringLiteral("--literal=one & two")},
                "shortcut Unicode arguments and shell metacharacters preserved literally");
    test.expect(QDir::cleanPath(result.value(QStringLiteral("workingDirectory")).toString()) ==
                    QDir::cleanPath(root),
                "shortcut working directory preserved");
    output.close();
    test.expect(QFile::remove(linkPath), "fixture shortcut removed");
    test.expect(!platform.checkAvailability(target).success,
                "removed shortcut becomes unavailable");
#else
    auto platform = xdstarter::createApplicationPlatform();
    test.expect(platform->scanApplications().applications.isEmpty(),
                "stub has no fake real applications");
    test.expect(!platform->launch(first).success, "stub never launches Windows targets");
#endif
    if (test.failures == 0) {
        std::cout << "PASS: application identity, catalog fixtures, availability, icons and "
                     "controlled launch.\n";
    }
    return test.failures == 0 ? 0 : 1;
}
