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
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

// The SDK property keys require the Shell property interface definitions first.
#include <propkey.h>
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
struct ShortcutOptions {
    bool runAs = false;
    int showCommand = SW_SHOWNORMAL;
    WORD hotKey = 0;
    QString aumid;
    QString description;
    QString iconPath;
};

bool createShortcut(const QString& path, const QString& executable, const QString& arguments,
                    const QString& workingDirectory, const ShortcutOptions& options = {}) {
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
        if (SUCCEEDED(result))
            result = link->SetShowCmd(options.showCommand);
        if (SUCCEEDED(result))
            result = link->SetHotkey(options.hotKey);
        if (SUCCEEDED(result) && !options.description.isEmpty())
            result = link->SetDescription(reinterpret_cast<LPCWSTR>(options.description.utf16()));
        if (SUCCEEDED(result) && !options.iconPath.isEmpty()) {
            const auto icon = QDir::toNativeSeparators(options.iconPath);
            result = link->SetIconLocation(reinterpret_cast<LPCWSTR>(icon.utf16()), 1);
        }
        if (SUCCEEDED(result) && options.runAs) {
            IShellLinkDataList* data = nullptr;
            result = link->QueryInterface(IID_PPV_ARGS(&data));
            DWORD flags = 0;
            if (SUCCEEDED(result))
                result = data->GetFlags(&flags);
            if (SUCCEEDED(result))
                result = data->SetFlags(flags | SLDF_RUNAS_USER);
            if (data)
                data->Release();
        }
        if (SUCCEEDED(result) && !options.aumid.isEmpty()) {
            IPropertyStore* properties = nullptr;
            result = link->QueryInterface(IID_PPV_ARGS(&properties));
            if (SUCCEEDED(result)) {
                // SetValue copies this borrowed UTF-16 string; no PROPVARIANT owns it.
                PROPVARIANT value{};
                value.vt = VT_LPWSTR;
                value.pwszVal =
                    reinterpret_cast<LPWSTR>(const_cast<ushort*>(options.aumid.utf16()));
                result = properties->SetValue(PKEY_AppUserModel_ID, value);
                if (SUCCEEDED(result))
                    result = properties->Commit();
                properties->Release();
            }
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

int catalogProbe(const QString& filter) {
    const auto snapshot = xdstarter::WindowsApplicationPlatform{}.scanApplications();
    QJsonArray entries;
    for (const auto& entry : snapshot.applications) {
        if (!filter.isEmpty() && !entry.name.contains(filter, Qt::CaseInsensitive))
            continue;
        entries.append(QJsonObject{{QStringLiteral("id"), entry.id},
                                   {QStringLiteral("name"), entry.name},
                                   {QStringLiteral("type"), entry.type},
                                   {QStringLiteral("shortcutPath"), entry.shortcutPath},
                                   {QStringLiteral("aumid"), entry.aumid},
                                   {QStringLiteral("parentFolderId"), entry.parentFolderId}});
    }
    QJsonObject equivalentTargetIds;
    for (auto it = snapshot.equivalentTargetIds.cbegin(); it != snapshot.equivalentTargetIds.cend();
         ++it)
        equivalentTargetIds.insert(it.key(), it.value());
    const auto bytes =
        QJsonDocument(
            QJsonObject{
                {QStringLiteral("scope"),
                 QStringLiteral("read-only real catalog scan; no launch or fixtures")},
                {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
                {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
                {QStringLiteral("success"), snapshot.success},
                {QStringLiteral("totalEntries"), snapshot.applications.size()},
                {QStringLiteral("filter"), filter},
                {QStringLiteral("entries"), entries},
                {QStringLiteral("equivalentTargetIds"), equivalentTargetIds},
                {QStringLiteral("warnings"), QJsonArray::fromStringList(snapshot.warnings)}})
            .toJson();
    std::cout.write(bytes.constData(), bytes.size());
    return snapshot.success ? 0 : 1;
}

void checkEquivalentShortcutMerging(TestContext& test, const QString& fixtureRoot,
                                    const QString& helper) {
    const auto user = QDir(fixtureRoot).filePath(QStringLiteral("equivalent-user"));
    const auto common = QDir(fixtureRoot).filePath(QStringLiteral("equivalent-common"));
    const auto alternateDirectory = QDir(fixtureRoot).filePath(QStringLiteral("alternate-cwd"));
    test.expect(QDir().mkpath(user) && QDir().mkpath(common) && QDir().mkpath(alternateDirectory),
                "equivalence roots and alternate working directory created");
    const auto secondExecutable = QDir(fixtureRoot).filePath(QStringLiteral("second-helper.exe"));
    test.expect(QFile::copy(helper, secondExecutable), "different existing executable copied");
    const auto nonExecutable = QDir(fixtureRoot).filePath(QStringLiteral("target.dat"));
    QFile dataFile(nonExecutable);
    test.expect(dataFile.open(QIODevice::WriteOnly) && dataFile.write("fixture") == 7,
                "ordinary non-executable target fixture created");
    dataFile.close();
    const auto path = [](const QString& root, const QString& name) {
        return QDir(root).filePath(name + QStringLiteral(".lnk"));
    };
    const auto pair =
        [&](const QString& name, const QString& firstTarget, const QString& secondTarget,
            const QString& firstArguments = {}, const QString& secondArguments = {},
            const QString& firstDirectory = {}, const QString& secondDirectory = {},
            const ShortcutOptions& firstOptions = {}, const ShortcutOptions& secondOptions = {}) {
            test.expect(createShortcut(path(user, name), firstTarget, firstArguments,
                                       firstDirectory, firstOptions) &&
                            createShortcut(path(common, name), secondTarget, secondArguments,
                                           secondDirectory, secondOptions),
                        "controlled cross-root shortcut pair created");
        };
    ShortcutOptions userMetadata;
    userMetadata.aumid = QStringLiteral("com.bitwarden.desktop");
    userMetadata.description = QStringLiteral("user metadata");
    ShortcutOptions commonMetadata;
    commonMetadata.aumid = QStringLiteral("electron.app.Bitwarden");
    commonMetadata.description = QStringLiteral("common metadata");
    commonMetadata.iconPath = helper;
    pair(QStringLiteral("Equivalent"), helper, helper, {}, {}, user, user, userMetadata,
         commonMetadata);
    pair(QStringLiteral("Arguments"), helper, helper, QStringLiteral("--profile=one"),
         QStringLiteral("--profile=two"));
    pair(QStringLiteral("LongArguments"), helper, helper,
         QString(4096, QLatin1Char('a')) + QStringLiteral("first"),
         QString(4096, QLatin1Char('a')) + QStringLiteral("second"));
    pair(QStringLiteral("WorkingDirectory"), helper, helper, {}, {}, user, alternateDirectory);
    ShortcutOptions elevated;
    elevated.runAs = true;
    pair(QStringLiteral("RunAs"), helper, helper, {}, {}, {}, {}, {}, elevated);
    ShortcutOptions minimized;
    minimized.showCommand = SW_SHOWMINNOACTIVE;
    pair(QStringLiteral("ShowCommand"), helper, helper, {}, {}, {}, {}, {}, minimized);
    ShortcutOptions hotKey;
    hotKey.hotKey = 0x0277; // Ctrl+F8, stored only; the fixture is outside system Programs.
    pair(QStringLiteral("HotKey"), helper, helper, {}, {}, {}, {}, {}, hotKey);
    pair(QStringLiteral("DifferentTarget"), helper, secondExecutable);
    pair(QStringLiteral("NotExecutable"), nonExecutable, nonExecutable);
    const auto missing = QDir(fixtureRoot).filePath(QStringLiteral("never-created.exe"));
    pair(QStringLiteral("MissingTarget"), missing, missing);
    ShortcutOptions packagedMetadata;
    packagedMetadata.aumid = QStringLiteral("Example.Package!App");
    pair(QStringLiteral("PackagedMetadata"), helper, helper, {}, {}, {}, {}, packagedMetadata,
         packagedMetadata);
    pair(QStringLiteral("UnknownData"), helper, helper);
    QFile unknown(path(common, QStringLiteral("UnknownData")));
    test.expect(unknown.open(QIODevice::ReadWrite), "unknown-data fixture opened");
    auto unknownBytes = unknown.readAll();
    const QByteArray terminal(4, '\0');
    test.expect(unknownBytes.endsWith(terminal), "Shell link has terminal block");
    if (unknownBytes.endsWith(terminal)) {
        unknownBytes.chop(4);
        unknownBytes += QByteArray::fromHex("08000000efbeadde");
        unknownBytes += terminal;
        test.expect(unknown.seek(0) && unknown.write(unknownBytes) == unknownBytes.size() &&
                        unknown.resize(unknownBytes.size()),
                    "unknown launch block retained for conservative comparison");
    }
    unknown.close();
    for (const auto& root : {user, common}) {
        QFile broken(path(root, QStringLiteral("Broken")));
        test.expect(broken.open(QIODevice::WriteOnly) && broken.write("not-a-link") == 10,
                    "unparseable shortcut retained as independent entry");
    }
    // Same executable and arguments at a different relative location/name must never merge.
    test.expect(createShortcut(path(user, QStringLiteral("UserAlias")), helper, {}, {}) &&
                    createShortcut(path(common, QStringLiteral("CommonAlias")), helper, {}, {}),
                "different relative aliases created");
    test.expect(createShortcut(path(user, QStringLiteral("CaseName")), helper, {}, {}) &&
                    createShortcut(path(common, QStringLiteral("casename")), helper, {}, {}),
                "case-different display names created");
    const auto userNested = QDir(user).filePath(QStringLiteral("Tools/Nested"));
    const auto commonNested = QDir(common).filePath(QStringLiteral("tools/NESTED"));
    test.expect(QDir().mkpath(userNested) && QDir().mkpath(commonNested) &&
                    createShortcut(path(userNested, QStringLiteral("NestedApp")), helper, {}, {}) &&
                    createShortcut(path(commonNested, QStringLiteral("NestedApp")), helper, {}, {}),
                "case-normalized nested relative paths created");
    const auto scan = [&] {
        return xdstarter::WindowsApplicationPlatform({user, common}).scanApplications();
    };
    const auto snapshot = scan();
    test.expect(snapshot.success && snapshot.warnings.isEmpty(),
                "equivalence scan succeeds without truncation");
    QSet<QString> visibleIds;
    for (const auto& entry : snapshot.applications)
        visibleIds.insert(entry.id);
    const auto named = [&](const QString& name) {
        QVector<xdstarter::LaunchTarget> matches;
        for (const auto& entry : snapshot.applications) {
            if (entry.type == QStringLiteral("shortcut") && entry.name == name)
                matches.append(entry);
        }
        return matches;
    };
    const auto equivalent = named(QStringLiteral("Equivalent"));
    test.expect(
        equivalent.size() == 1 &&
            equivalent.first().id == shortcut(path(user, QStringLiteral("Equivalent")), {}).id,
        "equivalent cross-root desktop links merge despite distinct AUMID/icon/description");
    for (const auto& name : {QStringLiteral("Arguments"), QStringLiteral("LongArguments"),
                             QStringLiteral("WorkingDirectory"), QStringLiteral("RunAs"),
                             QStringLiteral("ShowCommand"), QStringLiteral("HotKey"),
                             QStringLiteral("DifferentTarget"), QStringLiteral("NotExecutable"),
                             QStringLiteral("MissingTarget"), QStringLiteral("PackagedMetadata"),
                             QStringLiteral("UnknownData"), QStringLiteral("Broken")}) {
        test.expect(
            visibleIds.contains(shortcut(path(user, name), {}).id) &&
                visibleIds.contains(shortcut(path(common, name), {}).id),
            (QStringLiteral("different or unproven launch semantics preserve both: ") + name)
                .toStdString());
    }
    test.expect(named(QStringLiteral("UserAlias")).size() == 1 &&
                    named(QStringLiteral("CommonAlias")).size() == 1 &&
                    named(QStringLiteral("CaseName")).size() == 1 &&
                    named(QStringLiteral("casename")).size() == 1,
                "relative aliases and case-different display names remain separate");
    const auto nested = named(QStringLiteral("NestedApp"));
    test.expect(nested.size() == 1 &&
                    nested.first().shortcutPath ==
                        QDir::fromNativeSeparators(path(userNested, QStringLiteral("NestedApp"))),
                "nested path normalization merges equivalent entries with user identity");
    const auto commonTarget =
        shortcut(path(common, QStringLiteral("Equivalent")), QStringLiteral("Equivalent"));
    const auto userTarget =
        shortcut(path(user, QStringLiteral("Equivalent")), QStringLiteral("Equivalent"));
    test.expect(snapshot.equivalentTargetIds.value(commonTarget.id) == userTarget.id &&
                    snapshot.equivalentTargetIds.size() == 2,
                "scan exposes only proven hidden-to-representative aliases including nested link");
    test.expect(
        xdstarter::isValidApplicationTarget(commonTarget) &&
            xdstarter::WindowsApplicationPlatform{}.checkAvailability(commonTarget).success,
        "suppressed common shortcut remains a valid available original target for saved tiles");
    test.expect(QFile::remove(path(user, QStringLiteral("Equivalent"))),
                "representative fixture link removed without touching common source");
    const auto afterRemoval = scan();
    int commonRepresentatives = 0;
    for (const auto& entry : afterRemoval.applications) {
        if (entry.name == QStringLiteral("Equivalent") && entry.id == commonTarget.id)
            ++commonRepresentatives;
    }
    test.expect(afterRemoval.success && commonRepresentatives == 1,
                "refresh exposes original common identity after preferred source disappears");
    test.expect(!afterRemoval.equivalentTargetIds.contains(commonTarget.id) &&
                    afterRemoval.equivalentTargetIds.size() == 1,
                "fresh scan drops stale alias when the preferred source disappears");
}

void checkMergedScanBudget(TestContext& test, const QString& fixtureRoot, const QString& helper) {
    const auto user = QDir(fixtureRoot).filePath(QStringLiteral("budget-user"));
    const auto common = QDir(fixtureRoot).filePath(QStringLiteral("budget-common"));
    test.expect(QDir().mkpath(user) && QDir().mkpath(common), "bounded merge roots created");
    const auto seed = QDir(fixtureRoot).filePath(QStringLiteral("budget-seed.lnk"));
    test.expect(createShortcut(seed, helper, {}, {}), "bounded merge link seed created");
    bool copied = true;
    for (int index = 0; index < 2050 && copied; ++index) {
        const auto name = QStringLiteral("App%1.lnk").arg(index, 4, 10, QLatin1Char('0'));
        copied = QFile::copy(seed, QDir(user).filePath(name)) &&
                 QFile::copy(seed, QDir(common).filePath(name));
    }
    test.expect(copied, "bounded merge input exceeds 4096 real link candidates");
    if (!copied)
        return;
    const auto snapshot = xdstarter::WindowsApplicationPlatform({user, common}).scanApplications();
    test.expect(
        snapshot.success && snapshot.applications.size() == 2050 &&
            snapshot.equivalentTargetIds.size() == 2046 &&
            snapshot.warnings.contains(QStringLiteral("应用目录达到扫描上限，部分项目未载入。")),
        "4096 candidate budget counts suppressed duplicates before merging display entries");
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
#ifdef Q_OS_WIN
    // Explicit diagnostic mode bypasses all fixture creation and controlled launch tests.
    if (application.arguments().size() >= 2 &&
        application.arguments().at(1) == QStringLiteral("--catalog-probe")) {
        if (application.arguments().size() > 3)
            return 2;
        return catalogProbe(application.arguments().value(2));
    }
#endif
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
    checkEquivalentShortcutMerging(test, fixture.path(), helper);
    checkMergedScanBudget(test, fixture.path(), helper);
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
    const QString localizedName = QStringLiteral("本地化工具");
    const auto localizeFixture = [&](const QString& directory) {
        const auto nativeDirectory = QDir::toNativeSeparators(directory);
        if (!PathMakeSystemFolderW(reinterpret_cast<LPCWSTR>(nativeDirectory.utf16())))
            return false;
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
        const auto nativeIni = QDir::toNativeSeparators(ini.fileName());
        wchar_t profileName[256]{};
        const auto profileLength =
            GetPrivateProfileStringW(L".ShellClassInfo", L"LocalizedResourceName", L"", profileName,
                                     256, reinterpret_cast<LPCWSTR>(nativeIni.utf16()));
        const auto profileValue =
            QString::fromWCharArray(profileName, static_cast<int>(profileLength));
        test.expect(profileValue == localizedName,
                    "Windows profile API reads exact Chinese from UTF-16LE desktop.ini");
        if (profileValue != localizedName) {
            const QJsonObject diagnostics{{QStringLiteral("expected"), localizedName},
                                          {QStringLiteral("profileValue"), profileValue}};
            std::cerr << "Localization fixture: "
                      << QJsonDocument(diagnostics).toJson(QJsonDocument::Compact).constData()
                      << '\n';
        }
        return SetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeIni.utf16()),
                                  FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM) != FALSE;
    };
    test.expect(localizeFixture(userFolder) && localizeFixture(separateFolder),
                "localized names configured only inside owned temporary roots");
    // Configure display metadata before any Shell COM object can cache a
    // physical folder name while creating the shortcuts in these directories.
    test.expect(
        createShortcut(userLink, helper, QStringLiteral("user-argument"), userRoot) &&
            createShortcut(commonLink, helper, QStringLiteral("common-argument"), commonRoot) &&
            createShortcut(QDir(userNested).filePath(QStringLiteral("User.lnk")), helper, {},
                           userRoot) &&
            createShortcut(QDir(commonNested).filePath(QStringLiteral("Common.lnk")), helper, {},
                           commonRoot),
        "different arguments and nested complementary children created");
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
    if (!localizedFolder || !sameDisplayNameDistinctPath) {
        QJsonArray folderNames;
        for (const auto& entry : merged.applications) {
            if (entry.type == QLatin1String("folder")) {
                folderNames.append(QJsonObject{{QStringLiteral("name"), entry.name},
                                               {QStringLiteral("path"), entry.shortcutPath}});
            }
        }
        QJsonArray nativeFolders;
        const auto diagnosticCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        for (const auto& directory :
             {userFolder, commonFolder, userNested, commonNested, separateFolder}) {
            const auto nativeDirectory = QDir::toNativeSeparators(directory);
            const auto nativeIni =
                QDir::toNativeSeparators(QDir(directory).filePath(QStringLiteral("desktop.ini")));
            SHFILEINFOW info{};
            SetLastError(ERROR_SUCCESS);
            const auto displayResult =
                SHGetFileInfoW(reinterpret_cast<LPCWSTR>(nativeDirectory.utf16()), 0, &info,
                               static_cast<UINT>(sizeof(info)), SHGFI_DISPLAYNAME);
            const auto displayError = GetLastError();
            const auto directoryAttributes =
                GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeDirectory.utf16()));
            const auto iniAttributes =
                GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeIni.utf16()));
            nativeFolders.append(QJsonObject{
                {QStringLiteral("path"), directory},
                {QStringLiteral("displayNameAvailable"), displayResult != 0},
                {QStringLiteral("displayName"), QString::fromWCharArray(info.szDisplayName)},
                {QStringLiteral("lastError"), static_cast<int>(displayError)},
                {QStringLiteral("directoryAttributesHex"),
                 QString::number(directoryAttributes, 16)},
                {QStringLiteral("iniAttributesHex"), QString::number(iniAttributes, 16)}});
        }
        if (SUCCEEDED(diagnosticCom))
            CoUninitialize();
        const QJsonObject diagnostics{{QStringLiteral("expected"), localizedName},
                                      {QStringLiteral("folders"), folderNames},
                                      {QStringLiteral("nativeFolders"), nativeFolders},
                                      {QStringLiteral("diagnosticComResultHex"),
                                       QString::number(static_cast<quint32>(diagnosticCom), 16)},
                                      {QStringLiteral("ansiCodePage"), static_cast<int>(GetACP())},
                                      {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()}};
        std::cerr << "Localized folder scan: "
                  << QJsonDocument(diagnostics).toJson(QJsonDocument::Compact).constData() << '\n';
    }
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
