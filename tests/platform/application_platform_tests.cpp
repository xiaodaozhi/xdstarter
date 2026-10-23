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
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>
#include <string_view>

#ifdef Q_OS_WIN
#include "platform/windows/windows_application_platform.h"

#include <windows.h>

#include "restricted_fixture.h"
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
    QJsonObject evidence{
        {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("scope"), QStringLiteral("真实 Windows API 的 build 内隔离快捷方式、受控 "
                                                 "helper 与 DACL；不修改用户应用或开始菜单。")}};
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
    trailingNewline.id =
        xdstarter::applicationTargetId(trailingNewline.type, trailingNewline.aumid);
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
    const auto currentDirectory = QDir::fromNativeSeparators(QDir::current().canonicalPath());
    const bool ownedBuildRoot =
        !currentDirectory.isEmpty() &&
        currentDirectory.contains(QStringLiteral("/build/"), Qt::CaseInsensitive);
    test.expect(ownedBuildRoot, "native ACL fixtures require an absolute build working directory");
    if (!ownedBuildRoot)
        return 1;
    evidence.insert(QStringLiteral("ownedBuildFixtureValidated"), true);
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

    const auto userRoot = fixture.filePath(QStringLiteral("catalog-user"));
    const auto commonRoot = fixture.filePath(QStringLiteral("catalog-common"));
    const auto userFolder = QDir(userRoot).filePath(QStringLiteral("Tools"));
    const auto commonFolder = QDir(commonRoot).filePath(QStringLiteral("tools"));
    const auto userNested = QDir(userFolder).filePath(QStringLiteral("Nested"));
    const auto commonNested = QDir(commonFolder).filePath(QStringLiteral("NESTED"));
    const auto separateFolder = QDir(commonRoot).filePath(QStringLiteral("Other"));
    test.expect(QDir().mkpath(userNested) && QDir().mkpath(commonNested) &&
                    QDir().mkpath(separateFolder),
                "two owned Programs roots and nested folders created");
    const auto userLink = QDir(userFolder).filePath(QStringLiteral("Same.lnk"));
    const auto commonLink = QDir(commonFolder).filePath(QStringLiteral("Same.lnk"));
    test.expect(
        createShortcut(userLink, helper, QStringLiteral("user-argument"), userRoot) &&
            createShortcut(commonLink, helper, QStringLiteral("common-argument"), commonRoot) &&
            createShortcut(QDir(userNested).filePath(QStringLiteral("User.lnk")), helper, {},
                           userRoot) &&
            createShortcut(QDir(commonNested).filePath(QStringLiteral("Common.lnk")), helper, {},
                           commonRoot),
        "different arguments and nested complementary children created");
    const QString localizedName = QStringLiteral("本地化工具");
    const auto localizeFixture = [&](const QString& directory) {
        QFile ini(QDir(directory).filePath(QStringLiteral("desktop.ini")));
        if (!ini.open(QIODevice::WriteOnly))
            return false;
        const auto content = QStringLiteral("[.ShellClassInfo]\r\nLocalizedResourceName=") +
                             localizedName + QStringLiteral("\r\n");
        if (ini.write(QByteArray("\xff\xfe", 2)) != 2 ||
            ini.write(reinterpret_cast<const char*>(content.utf16()), content.size() * 2) !=
                content.size() * 2)
            return false;
        ini.close();
        const auto nativeDirectory = QDir::toNativeSeparators(directory);
        const auto nativeIni = QDir::toNativeSeparators(ini.fileName());
        return SetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeDirectory.utf16()),
                                  FILE_ATTRIBUTE_READONLY) &&
               SetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeIni.utf16()),
                                  FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    };
    test.expect(localizeFixture(userFolder) && localizeFixture(separateFolder),
                "localized names configured only inside owned temporary roots");
    xdstarter::WindowsApplicationPlatform mergedPlatform({userRoot, commonRoot});
    const auto merged = mergedPlatform.scanApplications();
    const auto folderId = xdstarter::applicationTargetId(QStringLiteral("folder"), userFolder);
    const auto nestedId = xdstarter::applicationTargetId(QStringLiteral("folder"), userNested);
    int rootFolders = 0;
    int nestedFolders = 0;
    int sameNamedShortcuts = 0;
    int nestedChildren = 0;
    bool localizedFolder = false;
    bool sameDisplayNameDistinctPath = false;
    for (const auto& entry : merged.applications) {
        if (entry.type == QLatin1String("folder") && entry.parentFolderId.isEmpty())
            ++rootFolders;
        if (entry.type == QLatin1String("folder") && entry.parentFolderId == folderId)
            ++nestedFolders;
        if (entry.type == QLatin1String("shortcut") && entry.parentFolderId == folderId)
            ++sameNamedShortcuts;
        if (entry.type == QLatin1String("shortcut") && entry.parentFolderId == nestedId)
            ++nestedChildren;
        localizedFolder = localizedFolder || (entry.id == folderId && entry.name == localizedName);
        sameDisplayNameDistinctPath =
            sameDisplayNameDistinctPath ||
            (entry.shortcutPath == separateFolder && entry.name == localizedName);
    }
    test.expect(merged.success && merged.applications.size() == 7 && rootFolders == 2 &&
                    nestedFolders == 1 && nestedChildren == 2,
                "relative folders merge recursively and both sources retain their children");
    test.expect(sameNamedShortcuts == 2 &&
                    xdstarter::applicationTargetId(QStringLiteral("shortcut"), userLink) !=
                        xdstarter::applicationTargetId(QStringLiteral("shortcut"), commonLink),
                "same named shortcuts with different parameters retain complete path identities");
    test.expect(localizedFolder && sameDisplayNameDistinctPath,
                "real Shell localized folder names display without merging different paths");
    test.expect(QDir(userNested).removeRecursively(), "owned user nested folder removed");
    const auto remaining = mergedPlatform.scanApplications();
    bool commonNestedRetained = false;
    for (const auto& entry : remaining.applications)
        if (entry.type == QLatin1String("folder") && entry.parentFolderId == folderId &&
            entry.name == QLatin1String("NESTED"))
            commonNestedRetained = true;
    test.expect(remaining.success && commonNestedRetained,
                "removing one nested source retains the other source's real folder");
    for (const auto& localizedDirectory : {userFolder, separateFolder}) {
        const auto nativeDirectory = QDir::toNativeSeparators(localizedDirectory);
        const auto nativeIni = QDir::toNativeSeparators(
            QDir(localizedDirectory).filePath(QStringLiteral("desktop.ini")));
        test.expect(SetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeDirectory.utf16()),
                                       FILE_ATTRIBUTE_NORMAL) &&
                        SetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeIni.utf16()),
                                           FILE_ATTRIBUTE_NORMAL),
                    "owned localized fixture attributes restored before temporary cleanup");
    }
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
    const auto packageMissing = platform.checkAvailability(uninstalled);
    test.expect(!packageMissing.success &&
                    packageMissing.errorCode == QStringLiteral("application-missing"),
                "uninstalled packaged application is unavailable");
    test.expect(!platform.launch(uninstalled).success,
                "uninstalled packaged application cannot activate");
    evidence.insert(QStringLiteral("missingTargetCode"), missingResult.errorCode);
    evidence.insert(QStringLiteral("uninstalledEquivalentAumidCode"), packageMissing.errorCode);
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
    evidence.insert(QStringLiteral("removedShortcutCode"),
                    platform.checkAvailability(target).errorCode);

    // Read denial is real NTFS access enforcement, rather than a fake platform result.
    const auto restrictedLink = QDir(root).filePath(QStringLiteral("权限拒绝.lnk"));
    const auto deniedReadTarget = shortcut(restrictedLink, QStringLiteral("权限拒绝"));
    test.expect(createShortcut(restrictedLink, helper, {}, root), "read-denial shortcut created");
    test.expect(platform.checkAvailability(deniedReadTarget).success,
                "read-denial target initially available");
    bool allRestored = true;
    {
        RestrictedFixture restriction(fixture.path(), restrictedLink, FILE_READ_DATA);
        test.expect(restriction.pathValidated() && restriction.active(),
                    "read denial restricted to owned build fixture");
        if (restriction.active()) {
            const auto native = QDir::toNativeSeparators(restrictedLink);
            const HANDLE handle =
                CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            const auto nativeError =
                handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
            test.expect(nativeError == ERROR_ACCESS_DENIED,
                        "native shortcut read reports ERROR_ACCESS_DENIED");
            const auto available = platform.checkAvailability(deniedReadTarget);
            const auto launched = platform.launch(deniedReadTarget);
            test.expect(!available.success &&
                            available.errorCode == QStringLiteral("access-denied"),
                        "read denial is not reported as missing or invalid");
            test.expect(!launched.success && launched.errorCode == QStringLiteral("access-denied"),
                        "read denial prevents launch");
            evidence.insert(QStringLiteral("shortcutReadDenial"),
                            QJsonObject{{QStringLiteral("nativeError"), int(nativeError)},
                                        {QStringLiteral("availabilityCode"), available.errorCode},
                                        {QStringLiteral("launchCode"), launched.errorCode}});
        }
        const bool restored = restriction.restore();
        allRestored = allRestored && restored;
        test.expect(restored, "shortcut DACL restored");
    }
    test.expect(platform.checkAvailability(deniedReadTarget).success,
                "shortcut becomes readable after DACL restoration");

    const auto documentPath = fixture.filePath(QStringLiteral("关联文档.txt"));
    QFile document(documentPath);
    test.expect(document.open(QIODevice::WriteOnly), "association fixture document created");
    test.expect(document.write("fixture") == 7, "association fixture content written");
    document.close();
    const auto documentLink = QDir(root).filePath(QStringLiteral("关联文档.lnk"));
    test.expect(createShortcut(documentLink, documentPath, {}, root),
                "association fixture shortcut created");
    {
        RestrictedFixture restriction(fixture.path(), documentPath, FILE_EXECUTE);
        test.expect(restriction.pathValidated() && restriction.active(),
                    "document execute restriction confined to fixture");
        if (restriction.active()) {
            const bool associationAvailable =
                platform.checkAvailability(shortcut(documentLink, QStringLiteral("关联文档")))
                    .success;
            test.expect(associationAvailable,
                        "document association does not require executable access");
            evidence.insert(QStringLiteral("documentAssociationAvailabilityPreserved"),
                            associationAvailable);
        }
        const bool restored = restriction.restore();
        allRestored = allRestored && restored;
        test.expect(restored, "document DACL restored without opening its associated application");
    }

    const auto copiedHelper = fixture.filePath(QStringLiteral("受控程序.exe"));
    const auto executableLink = QDir(root).filePath(QStringLiteral("执行拒绝.lnk"));
    const auto deniedMarker = fixture.filePath(QStringLiteral("must-not-launch.json"));
    test.expect(QFile::copy(helper, copiedHelper), "controlled helper copied inside build fixture");
    test.expect(createShortcut(executableLink, copiedHelper,
                               QStringLiteral("\"%1\"").arg(deniedMarker), root),
                "execution-denial shortcut created");
    const auto deniedExecuteTarget = shortcut(executableLink, QStringLiteral("执行拒绝"));
    {
        RestrictedFixture restriction(fixture.path(), copiedHelper, FILE_EXECUTE);
        test.expect(restriction.pathValidated() && restriction.active(),
                    "execution denial restricted to copied helper");
        if (restriction.active()) {
            const auto native = QDir::toNativeSeparators(copiedHelper);
            const HANDLE handle =
                CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_EXECUTE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            const auto nativeError =
                handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
            test.expect(nativeError == ERROR_ACCESS_DENIED,
                        "native execute open reports ERROR_ACCESS_DENIED");
            test.expect(GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16())) !=
                            INVALID_FILE_ATTRIBUTES,
                        "execution-denied target still has readable metadata");
            const auto availability = platform.checkAvailability(deniedExecuteTarget);
            test.expect(!availability.success &&
                            availability.errorCode == QStringLiteral("access-denied"),
                        "native executable access denial is classified before Shell dispatch");
            const auto launchResult = platform.launch(deniedExecuteTarget);
            test.expect(!launchResult.success &&
                            launchResult.errorCode == QStringLiteral("access-denied"),
                        "execution-denied helper cannot launch");
            test.expect(!QFileInfo::exists(deniedMarker), "denied helper did not execute");
            evidence.insert(
                QStringLiteral("executeDenial"),
                QJsonObject{
                    {QStringLiteral("nativeError"), int(nativeError)},
                    {QStringLiteral("availabilityCode"), availability.errorCode},
                    {QStringLiteral("launchCode"), launchResult.errorCode},
                    {QStringLiteral("helperMarkerAbsent"), !QFileInfo::exists(deniedMarker)}});
        }
        const bool restored = restriction.restore();
        allRestored = allRestored && restored;
        test.expect(restored, "copied helper DACL restored");
    }

    const auto readable = fixture.filePath(QStringLiteral("readable-scan"));
    const auto denied = QDir(readable).filePath(QStringLiteral("denied-child"));
    test.expect(QDir().mkpath(readable) && QDir().mkpath(denied), "isolated scan roots created");
    test.expect(
        createShortcut(QDir(readable).filePath(QStringLiteral("可读.lnk")), helper, {}, root) &&
            createShortcut(QDir(denied).filePath(QStringLiteral("受限.lnk")), helper, {}, root),
        "scan-root fixture shortcuts created");
    {
        RestrictedFixture restriction(fixture.path(), denied, FILE_LIST_DIRECTORY);
        test.expect(restriction.pathValidated() && restriction.active(),
                    "directory listing denial restricted to owned build fixture");
        if (restriction.active()) {
            const auto native = QDir::toNativeSeparators(denied);
            const HANDLE handle =
                CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_LIST_DIRECTORY,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            const auto nativeError =
                handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
            test.expect(nativeError == ERROR_ACCESS_DENIED,
                        "native directory listing reports ERROR_ACCESS_DENIED");
            const auto partial =
                xdstarter::WindowsApplicationPlatform({readable}).scanApplications();
            const auto failed =
                xdstarter::WindowsApplicationPlatform({denied, denied}).scanApplications();
            const auto mixed =
                xdstarter::WindowsApplicationPlatform({denied, readable}).scanApplications();
            test.expect(partial.success && partial.applications.size() == 2 &&
                            !partial.warnings.isEmpty(),
                        "partially denied scan keeps readable applications and warning");
            test.expect(mixed.success && mixed.applications.size() == 2 &&
                            !mixed.warnings.isEmpty(),
                        "mixed denied and readable roots keep usable applications");
            test.expect(!failed.success && failed.applications.isEmpty() &&
                            !failed.warnings.isEmpty(),
                        "all-denied roots never masquerade as successful empty scan");
            evidence.insert(
                QStringLiteral("directoryDenial"),
                QJsonObject{{QStringLiteral("nativeError"), int(nativeError)},
                            {QStringLiteral("partialSuccess"), partial.success},
                            {QStringLiteral("partialApplications"), partial.applications.size()},
                            {QStringLiteral("partialWarnings"), partial.warnings.size()},
                            {QStringLiteral("allDeniedSuccess"), failed.success},
                            {QStringLiteral("allDeniedWarnings"), failed.warnings.size()},
                            {QStringLiteral("mixedRootSuccess"), mixed.success}});
        }
        const bool restored = restriction.restore();
        allRestored = allRestored && restored;
        test.expect(restored, "directory DACL restored");
    }
    const auto recovered = xdstarter::WindowsApplicationPlatform({denied}).scanApplications();
    test.expect(recovered.success && recovered.applications.size() == 1 &&
                    recovered.warnings.isEmpty(),
                "restored directory scans normally");
    evidence.insert(QStringLiteral("allFixtureDaclsRestored"), allRestored);
    if (allRestored) {
        const bool cleaned = fixture.remove();
        test.expect(cleaned, "owned build fixture cleaned after DACL restoration");
        evidence.insert(QStringLiteral("fixtureCleaned"), cleaned);
    } else {
        fixture.setAutoRemove(false);
        evidence.insert(QStringLiteral("fixtureCleaned"), false);
    }
#else
    auto platform = xdstarter::createApplicationPlatform();
    test.expect(platform->scanApplications().applications.isEmpty(),
                "stub has no fake real applications");
    test.expect(!platform->launch(first).success, "stub never launches Windows targets");
#endif
    const auto evidenceDirectory = qEnvironmentVariable("XDSTARTER_CATALOG_VALIDATION_DIR");
    if (!evidenceDirectory.isEmpty()) {
        test.expect(QDir::isAbsolutePath(evidenceDirectory) && QDir().mkpath(evidenceDirectory),
                    "absolute evidence directory available");
        evidence.insert(QStringLiteral("passed"), test.failures == 0);
        QFile evidenceFile(QDir(evidenceDirectory).filePath(QStringLiteral("catalog-errors.json")));
        const auto bytes = QJsonDocument(evidence).toJson();
        test.expect(evidenceFile.open(QIODevice::WriteOnly) &&
                        evidenceFile.write(bytes) == bytes.size(),
                    "catalog error evidence written");
    }
    if (test.failures == 0) {
        std::cout << "PASS: application identity, catalog fixtures, availability, icons and "
                     "controlled launch.\n";
    }
    return test.failures == 0 ? 0 : 1;
}
