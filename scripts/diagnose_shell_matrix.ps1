[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Helper,
    [Parameter(Mandatory = $true)][string]$Output
)

$ErrorActionPreference = 'Stop'
if (!$IsWindows -or $env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or
    $env:GITHUB_RUN_ID -notmatch '^[1-9][0-9]*$' -or $env:GITHUB_RUN_ATTEMPT -notmatch '^[1-9][0-9]*$') {
    throw 'The cross-location Shell fixture runs only on a GitHub-hosted Windows runner.'
}

$workspace = (Resolve-Path -LiteralPath $env:GITHUB_WORKSPACE).Path
$workParent = (Resolve-Path -LiteralPath (Join-Path $workspace 'build')).Path
$profileParent = (Resolve-Path -LiteralPath (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp')).Path
$helperPath = (Resolve-Path -LiteralPath $Helper).Path
$expectedHelper = [IO.Path]::GetFullPath((Join-Path $workspace 'build/app-release/xdstarter_launch_fixture.exe'))
$outputPath = [IO.Path]::GetFullPath($Output)
$expectedOutput = [IO.Path]::GetFullPath((Join-Path $workspace 'build/app-release/shell-localization-history/cross-matrix.json'))
if (!$helperPath.Equals($expectedHelper, [StringComparison]::OrdinalIgnoreCase) -or
    !$outputPath.Equals($expectedOutput, [StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $outputPath)) {
    throw 'Unexpected fixture helper or output path.'
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class XDStarterShellMatrix {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct SHFILEINFO {
        public IntPtr hIcon;
        public int iIcon;
        public uint dwAttributes;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string displayName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=80)] public string typeName;
    }
    [DllImport("shlwapi.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)] public static extern bool PathMakeSystemFolderW(string path);
    [DllImport("shlwapi.dll", CharSet=CharSet.Unicode)]
    public static extern int SHLoadIndirectString(string source, StringBuilder output, uint length, IntPtr reserved);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern int SHSetLocalizedName(string path, string resource, int resourceId);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr SHGetFileInfoW(string path, uint attributes, ref SHFILEINFO info, uint size, uint flags);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern void SHChangeNotify(int eventId, uint flags, string item1, IntPtr item2);
    [DllImport("ole32.dll")] public static extern int CoInitializeEx(IntPtr reserved, uint flags);
    [DllImport("ole32.dll")] public static extern void CoUninitialize();
}
'@

function Get-FileFacts([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    $acl = Get-Acl -LiteralPath $Path
    $sddlBytes = [Text.Encoding]::UTF8.GetBytes($acl.Sddl)
    $aclHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($sddlBytes)).ToLowerInvariant()
    $zones = @(Get-Item -LiteralPath $Path -Stream '*' -ErrorAction Stop | Where-Object Stream -eq 'Zone.Identifier')
    return @{ attributes = [int]$item.Attributes; aclSha256 = $aclHash;
              aclProtected = [bool]$acl.AreAccessRulesProtected; zonePresent = ($zones.Count -gt 0) }
}

function Remove-OwnedRoot([string]$Root, [string]$Parent, [string]$Leaf) {
    $resolved = (Resolve-Path -LiteralPath $Root).Path
    $expected = [IO.Path]::GetFullPath((Join-Path $Parent $Leaf))
    $prefix = $Parent.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (!$resolved.Equals($expected, [StringComparison]::OrdinalIgnoreCase) -or
        !$resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to clean a root outside the validated fixture parent.'
    }
    $entries = @(Get-Item -LiteralPath $resolved -Force) + @(Get-ChildItem -LiteralPath $resolved -Force -Recurse)
    foreach ($entry in $entries) {
        $absolute = [IO.Path]::GetFullPath($entry.FullName)
        if ((!$absolute.Equals($resolved, [StringComparison]::OrdinalIgnoreCase) -and
             !$absolute.StartsWith($resolved + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) -or
            ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Refusing to clean an unexpected path or reparse point.'
        }
    }
    foreach ($entry in $entries) { [IO.File]::SetAttributes($entry.FullName, [IO.FileAttributes]::Normal) }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

$leaf = 'xdstarter-shell-matrix-' + $env:GITHUB_RUN_ID + '-' + $env:GITHUB_RUN_ATTEMPT + '-' + [Guid]::NewGuid().ToString('N')
$roots = @(@{ name = 'workspace'; parent = $workParent; path = (Join-Path $workParent $leaf) },
           @{ name = 'profile'; parent = $profileParent; path = (Join-Path $profileParent $leaf) })
$created = @()
$cases = @()
$errors = @()
$cleanupErrors = @()
$comResult = $null
try {
    $comResult = [XDStarterShellMatrix]::CoInitializeEx([IntPtr]::Zero, 2)
    if ($comResult -lt 0) { throw "COM initialization failed: $comResult" }
    foreach ($root in $roots) {
        if (Test-Path -LiteralPath $root.path) { throw 'Fixture root already exists.' }
        New-Item -ItemType Directory -Path $root.path | Out-Null
        $created += $root
        $module = Join-Path $root.path '本地化 资源 & helper.exe'
        Copy-Item -LiteralPath $helperPath -Destination $module
        $root.module = $module
        $root.moduleSha256 = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    if ($roots[0].moduleSha256 -ne $roots[1].moduleSha256) { throw 'Fixture modules differ.' }
    foreach ($folderRoot in $roots) {
        foreach ($moduleRoot in $roots) {
            $physical = 'matrix-' + $folderRoot.name + '-' + $moduleRoot.name
            $folder = Join-Path $folderRoot.path $physical
            New-Item -ItemType Directory -Path $folder | Out-Null
            if (![XDStarterShellMatrix]::PathMakeSystemFolderW($folder)) { throw 'Cannot set owned folder attributes.' }
            $ini = Join-Path $folder 'desktop.ini'
            $reference = '@' + $moduleRoot.module + ',-5301'
            $content = "[.ShellClassInfo]`r`nLocalizedResourceName=$reference`r`n"
            [IO.File]::WriteAllText($ini, $content, [Text.UnicodeEncoding]::new($false, $true))
            $configured = [XDStarterShellMatrix]::SHSetLocalizedName($folder, $moduleRoot.module, 5301)
            if ($configured -lt 0) { throw "SHSetLocalizedName failed: $configured" }
            [IO.File]::SetAttributes($ini, [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System)
            [XDStarterShellMatrix]::SHChangeNotify(0x2000, 0x1005, $folder, [IntPtr]::Zero)
            $indirect = [Text.StringBuilder]::new(256)
            $indirectResult = [XDStarterShellMatrix]::SHLoadIndirectString($reference, $indirect, 256, [IntPtr]::Zero)
            $info = [XDStarterShellMatrix+SHFILEINFO]::new()
            $size = [Runtime.InteropServices.Marshal]::SizeOf([type][XDStarterShellMatrix+SHFILEINFO])
            $displayResult = [XDStarterShellMatrix]::SHGetFileInfoW($folder, 0, [ref]$info, $size, 0x200)
            $folderDrive = [IO.Path]::GetPathRoot($folder)
            $moduleDrive = [IO.Path]::GetPathRoot($moduleRoot.module)
            $cases += @{ folderLocation = $folderRoot.name; moduleLocation = $moduleRoot.name;
                         folderDrive = $folderDrive; moduleDrive = $moduleDrive; physicalName = $physical;
                         folderFileSystem = [IO.DriveInfo]::new($folderDrive).DriveFormat;
                         moduleFileSystem = [IO.DriveInfo]::new($moduleDrive).DriveFormat;
                         moduleSha256 = $moduleRoot.moduleSha256; module = (Get-FileFacts $moduleRoot.module);
                         ini = (Get-FileFacts $ini); folderAttributes = [int](Get-Item -LiteralPath $folder -Force).Attributes;
                         folderAclSha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
                             [Text.Encoding]::UTF8.GetBytes((Get-Acl -LiteralPath $folder).Sddl))).ToLowerInvariant();
                         setLocalizedName = $configured; indirectResult = $indirectResult;
                         indirectName = $indirect.ToString(); shellDisplayAvailable = ($displayResult -ne [IntPtr]::Zero);
                         shellDisplayName = $info.displayName }
        }
    }
} catch { $errors += $_.Exception.Message }
finally {
    if ($null -ne $comResult -and $comResult -ge 0) { [XDStarterShellMatrix]::CoUninitialize() }
    foreach ($root in $created) {
        try { Remove-OwnedRoot $root.path $root.parent $leaf } catch {
            $cleanupErrors += $_.Exception.Message
            $errors += $_.Exception.Message
        }
    }
}
$remaining = @($created | Where-Object { Test-Path -LiteralPath $_.path })
$report = @{ schemaVersion = 1; cases = $cases; comInitializationResult = $comResult;
             cleanupComplete = ($cleanupErrors.Count -eq 0 -and $remaining.Count -eq 0);
             identity = @{ sha = $env:GITHUB_SHA; runId = $env:GITHUB_RUN_ID; attempt = $env:GITHUB_RUN_ATTEMPT;
                           runnerOs = $env:RUNNER_OS; runnerArch = $env:RUNNER_ARCH };
             osVersion = [Environment]::OSVersion.VersionString;
             errors = $errors; status = $(if ($errors.Count -eq 0 -and $cases.Count -eq 4) { 'passed' } else { 'failed' });
             limitations = @('Fixture compares native Shell observations, not the cause of historical failures.',
                             'Locations may resolve to the same volume on some runners.') }
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outputPath -Encoding utf8NoBOM
if ($report.status -ne 'passed') { throw ('Shell cross-location fixture failed: ' + ($errors -join '; ')) }
Write-Host "Shell cross-location fixture collected $($cases.Count) cases."
