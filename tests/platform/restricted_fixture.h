// tests/platform/restricted_fixture.h

#pragma once

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <QByteArray>
#include <QDir>
#include <QFileInfo>

#include <windows.h>

#include <aclapi.h>

// Only an existing, non-reparse child of the test's own build fixture may be
// restricted. The original DACL remains available until restoration succeeds.
class RestrictedFixture final {
  public:
    RestrictedFixture(const QString& root, const QString& path, DWORD deniedAccess)
        : path_(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath())) {
        // QFileInfo::canonicalFilePath resolves Windows .lnk destinations. Open
        // the actual file first so the safety check cannot target its executable.
        const HANDLE file = CreateFileW(
            reinterpret_cast<LPCWSTR>(path_.utf16()), READ_CONTROL,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        wchar_t resolved[32768] = {};
        const auto length = GetFinalPathNameByHandleW(file, resolved, 32768, FILE_NAME_NORMALIZED);
        CloseHandle(file);
        if (!length || length >= 32768)
            return;
        path_ = QString::fromWCharArray(resolved);
        if (path_.startsWith(QStringLiteral("\\\\?\\")))
            path_.remove(0, 4);
        const auto current = QDir::fromNativeSeparators(QDir::current().canonicalPath());
        const auto owner = QDir::fromNativeSeparators(QFileInfo(root).canonicalFilePath());
        const auto target = QDir::fromNativeSeparators(path_);
        if (!current.contains(QStringLiteral("/build/"), Qt::CaseInsensitive) ||
            !owner.startsWith(current + QLatin1Char('/'), Qt::CaseInsensitive) ||
            !target.startsWith(owner + QLatin1Char('/'), Qt::CaseInsensitive))
            return;
        const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path_.utf16()));
        if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            return;
        pathValidated_ = true;
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return;
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        QByteArray information(static_cast<qsizetype>(size), Qt::Uninitialized);
        const bool tokenRead =
            GetTokenInformation(token, TokenUser, information.data(), size, &size);
        CloseHandle(token);
        if (!tokenRead)
            return;
        const auto user = reinterpret_cast<const TOKEN_USER*>(information.constData());
        auto* name = reinterpret_cast<LPWSTR>(path_.data());
        if (GetNamedSecurityInfoW(name, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                  &originalDacl_, nullptr, &descriptor_) != ERROR_SUCCESS)
            return;
        EXPLICIT_ACCESSW entry = {};
        entry.grfAccessPermissions = deniedAccess;
        entry.grfAccessMode = DENY_ACCESS;
        entry.grfInheritance = NO_INHERITANCE;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_USER;
        entry.Trustee.ptstrName = static_cast<LPWSTR>(user->User.Sid);
        PACL restricted = nullptr;
        const auto prepared = SetEntriesInAclW(1, &entry, originalDacl_, &restricted);
        if (prepared == ERROR_SUCCESS) {
            active_ = SetNamedSecurityInfoW(name, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                            nullptr, nullptr, restricted, nullptr) == ERROR_SUCCESS;
        }
        LocalFree(restricted);
    }

    ~RestrictedFixture() {
        restore();
        LocalFree(descriptor_);
    }
    RestrictedFixture(const RestrictedFixture&) = delete;
    RestrictedFixture& operator=(const RestrictedFixture&) = delete;
    bool pathValidated() const { return pathValidated_; }
    bool active() const { return active_; }
    bool restore() {
        if (!active_)
            return true;
        const auto result = SetNamedSecurityInfoW(reinterpret_cast<LPWSTR>(path_.data()),
                                                  SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                                  nullptr, nullptr, originalDacl_, nullptr);
        if (result == ERROR_SUCCESS)
            active_ = false;
        return result == ERROR_SUCCESS;
    }

  private:
    QString path_;
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    PACL originalDacl_ = nullptr;
    bool pathValidated_ = false;
    bool active_ = false;
};
#endif
