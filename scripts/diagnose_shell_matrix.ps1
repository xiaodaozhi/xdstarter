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
. (Join-Path $PSScriptRoot 'windows_access_descriptor.ps1')
. (Join-Path $PSScriptRoot 'shell_path_facts.ps1')
. (Join-Path $PSScriptRoot 'shell_junction_probe.ps1')

$workspace = (Resolve-Path -LiteralPath $env:GITHUB_WORKSPACE).Path
$workParent = (Resolve-Path -LiteralPath (Join-Path $workspace 'build')).Path
$profileLocalParent = (Resolve-Path -LiteralPath ([Environment]::GetFolderPath('LocalApplicationData'))).Path
$profileParent = (Resolve-Path -LiteralPath (Join-Path $profileLocalParent 'Temp')).Path
if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { throw 'Missing runner temporary directory.' }
$runnerTempParent = (Resolve-Path -LiteralPath $env:RUNNER_TEMP).Path
foreach ($parent in @($workParent, $profileParent, $runnerTempParent, $profileLocalParent)) {
    if ((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Refusing to create a fixture directly beneath a reparse point.'
    }
}
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
    $zones = @(Get-Item -LiteralPath $Path -Stream '*' -Force -ErrorAction Stop | Where-Object Stream -eq 'Zone.Identifier')
    return @{ attributes = [int]$item.Attributes; aclSha256 = $aclHash;
              aclProtected = [bool]$acl.AreAccessRulesProtected; zonePresent = ($zones.Count -gt 0) }
}

function Get-AclHash([string]$Path) {
    $bytes = [Text.Encoding]::UTF8.GetBytes((Get-Acl -LiteralPath $Path).Sddl)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}

function Get-PathHash([string]$Path) {
    # This compares resolved path spellings, not underlying directory identity.
    $normalized = [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar).ToUpperInvariant()
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
        [Text.Encoding]::UTF8.GetBytes($normalized))).ToLowerInvariant()
}

function Get-AccessDescriptor([string]$Path) {
    return (Get-Acl -LiteralPath $Path).GetSecurityDescriptorSddlForm(
        [Security.AccessControl.AccessControlSections]::Access)
}

function Get-DescriptorHash([string]$Descriptor) {
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
        [Text.Encoding]::UTF8.GetBytes($Descriptor))).ToLowerInvariant()
}

function New-AccessTemplate([string]$Path, [string]$Root) {
    # The ACL object is a copy; do not change the source fixture.
    $acl = Get-Acl -LiteralPath $Path
    $acl.SetAccessRuleProtection($true, $true)
    $descriptor = $acl.GetSecurityDescriptorSddlForm([Security.AccessControl.AccessControlSections]::Access)
    # Windows canonicalizes inherited ACEs when persisting protected access
    # rules. Read the persisted descriptor from a separate owned object before
    # comparing it with target observations.
    $probe = Join-Path $Root ('acl-template-' + [Guid]::NewGuid().ToString('N'))
    if ((Get-Item -LiteralPath $Path -Force) -is [IO.DirectoryInfo]) {
        New-Item -ItemType Directory -Path $probe | Out-Null
    } else {
        [IO.File]::WriteAllText($probe, '')
    }
    $original = Get-AccessDescriptor $probe
    try {
        Set-OwnedAccess $probe $Root $descriptor
        $persisted = Get-AccessDescriptor $probe
    } finally {
        Set-OwnedAccess $probe $Root $original
        if (!(Test-RestoredAccess $original (Get-AccessDescriptor $probe))) {
            throw 'Access template probe rules were not restored.'
        }
        Remove-Item -LiteralPath $probe -Force
    }
    return @{ descriptor = $persisted; sha256 = (Get-DescriptorHash $persisted) }
}

function Set-OwnedAccess([string]$Path, [string]$Root, [string]$Descriptor) {
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $expected = [IO.Path]::GetFullPath($Path)
    $prefix = $resolvedRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (!$resolved.Equals($expected, [StringComparison]::OrdinalIgnoreCase) -or
        !$resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
        ((Get-Item -LiteralPath $resolved -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Refusing to change access on a path outside the owned fixture root.'
    }
    # Preserve the target owner and group; modify only its access descriptor.
    $item = Get-Item -LiteralPath $resolved -Force
    $acl = Get-Acl -LiteralPath $resolved
    $acl.SetSecurityDescriptorSddlForm($Descriptor, [Security.AccessControl.AccessControlSections]::Access)
    if ($item -is [IO.DirectoryInfo]) {
        [IO.FileSystemAclExtensions]::SetAccessControl([IO.DirectoryInfo]$item, $acl)
    } else {
        [IO.FileSystemAclExtensions]::SetAccessControl([IO.FileInfo]$item, $acl)
    }
}

function New-ShellObservation($FolderRoot, $ModuleRoot, [string]$Physical, $AccessTemplate = $null) {
    $folder = Join-Path $FolderRoot.path $Physical
    New-Item -ItemType Directory -Path $folder | Out-Null
    if (![XDStarterShellMatrix]::PathMakeSystemFolderW($folder)) { throw 'Cannot set owned folder attributes.' }
    $ini = Join-Path $folder 'desktop.ini'
    $reference = '@' + $ModuleRoot.module + ',-5301'
    $content = "[.ShellClassInfo]`r`nLocalizedResourceName=$reference`r`n"
    [IO.File]::WriteAllText($ini, $content, [Text.UnicodeEncoding]::new($false, $true))
    $originalFolderAccess = $null
    $originalIniAccess = $null
    if ($null -ne $AccessTemplate) {
        $originalFolderAccess = Get-AccessDescriptor $folder
        $originalIniAccess = Get-AccessDescriptor $ini
    }
    try {
        if ($null -ne $AccessTemplate) {
            Set-OwnedAccess $folder $FolderRoot.path $AccessTemplate.folder.descriptor
            Set-OwnedAccess $ini $FolderRoot.path $AccessTemplate.ini.descriptor
        }
        $configured = [XDStarterShellMatrix]::SHSetLocalizedName($folder, $ModuleRoot.module, 5301)
        if ($configured -lt 0) { throw "SHSetLocalizedName failed: $configured" }
        [IO.File]::SetAttributes($ini, [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System)
        [XDStarterShellMatrix]::SHChangeNotify(0x2000, 0x1005, $folder, [IntPtr]::Zero)
        $indirect = [Text.StringBuilder]::new(256)
        $indirectResult = [XDStarterShellMatrix]::SHLoadIndirectString($reference, $indirect, 256, [IntPtr]::Zero)
        $info = [XDStarterShellMatrix+SHFILEINFO]::new()
        $size = [Runtime.InteropServices.Marshal]::SizeOf([type][XDStarterShellMatrix+SHFILEINFO])
        $displayResult = [XDStarterShellMatrix]::SHGetFileInfoW($folder, 0, [ref]$info, $size, 0x200)
        $folderDrive = [IO.Path]::GetPathRoot($folder)
        $moduleDrive = [IO.Path]::GetPathRoot($ModuleRoot.module)
        $observation = @{ folderDrive = $folderDrive; moduleDrive = $moduleDrive; physicalName = $Physical;
                  folderFileSystem = [IO.DriveInfo]::new($folderDrive).DriveFormat;
                  moduleFileSystem = [IO.DriveInfo]::new($moduleDrive).DriveFormat;
                  moduleSha256 = $ModuleRoot.moduleSha256; modulePathSha256 = (Get-PathHash $ModuleRoot.module);
                  module = (Get-FileFacts $ModuleRoot.module);
                  ini = (Get-FileFacts $ini); folderAttributes = [int](Get-Item -LiteralPath $folder -Force).Attributes;
                  folderPathFacts = (Get-XDStarterPathFacts $folder); iniPathFacts = (Get-XDStarterPathFacts $ini);
                  folderAclSha256 = (Get-AclHash $folder); setLocalizedName = $configured;
                  indirectResult = $indirectResult; indirectName = $indirect.ToString();
                  shellDisplayAvailable = ($displayResult -ne [IntPtr]::Zero); shellDisplayName = $info.displayName }
        if ($null -ne $AccessTemplate) {
            $observation.folderAccessProtected = [bool](Get-Acl -LiteralPath $folder).AreAccessRulesProtected
            $observation.folderAccessSha256 = Get-DescriptorHash (Get-AccessDescriptor $folder)
            $observation.iniAccessSha256 = Get-DescriptorHash (Get-AccessDescriptor $ini)
            $observation.folderTemplateSha256 = $AccessTemplate.folder.sha256
            $observation.iniTemplateSha256 = $AccessTemplate.ini.sha256
        }
        return $observation
    } finally {
        if ($null -ne $AccessTemplate) {
            # Restore the folder first so its child is traversable, then restore the file.
            Set-OwnedAccess $folder $FolderRoot.path $originalFolderAccess
            Set-OwnedAccess $ini $FolderRoot.path $originalIniAccess
            if (!(Test-RestoredAccess $originalFolderAccess (Get-AccessDescriptor $folder)) -or
                !(Test-RestoredAccess $originalIniAccess (Get-AccessDescriptor $ini))) {
                throw 'The owned fixture access rules were not restored.'
            }
        }
    }
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

function Get-ShellDisplay([string]$Path) {
    $info = [XDStarterShellMatrix+SHFILEINFO]::new()
    $size = [Runtime.InteropServices.Marshal]::SizeOf([type][XDStarterShellMatrix+SHFILEINFO])
    $result = [XDStarterShellMatrix]::SHGetFileInfoW($Path, 0, [ref]$info, $size, 0x200)
    return @{ available = ($result -ne [IntPtr]::Zero); name = $info.displayName }
}

function Get-JunctionObservation($Spec) {
    Confirm-XDStarterFixtureJunction $Spec
    $physical = 'parent-' + $Spec.target.name
    $direct = Join-Path $Spec.targetPath $physical
    $alias = Join-Path $Spec.path $physical
    $directFolderIdentity = Get-XDStarterIdentityHash $direct
    $aliasFolderIdentity = Get-XDStarterIdentityHash $alias
    $directIniIdentity = Get-XDStarterIdentityHash (Join-Path $direct 'desktop.ini')
    $aliasIniIdentity = Get-XDStarterIdentityHash (Join-Path $alias 'desktop.ini')
    if ($directFolderIdentity -ne $aliasFolderIdentity -or $directIniIdentity -ne $aliasIniIdentity) {
        throw 'Direct and junction paths do not identify the same fixture objects.'
    }
    # Observe the direct spelling on both sides of each alias query so a change
    # during the sequence remains visible in the raw evidence.
    $before = Get-ShellDisplay $direct
    $viaJunction = Get-ShellDisplay $alias
    $after = Get-ShellDisplay $direct
    $probeScript = Join-Path $PSScriptRoot 'shell_name_probe.ps1'
    $directFresh = Get-XDStarterFreshShellName $direct $probeScript
    $aliasFresh = Get-XDStarterFreshShellName $alias $probeScript
    if ($directFresh.folderIdentitySha256 -ne $directFolderIdentity -or
        $aliasFresh.folderIdentitySha256 -ne $directFolderIdentity -or
        $directFresh.iniIdentitySha256 -ne $directIniIdentity -or
        $aliasFresh.iniIdentitySha256 -ne $directIniIdentity) {
        throw 'Fresh Shell name probes did not read the same fixture objects.'
    }
    return @{ hostLocation = $Spec.host.name; targetLocation = $Spec.target.name;
              physicalName = $physical; folderIdentitySha256 = $directFolderIdentity;
              aliasFolderIdentitySha256 = $aliasFolderIdentity; iniIdentitySha256 = $directIniIdentity;
              aliasIniIdentitySha256 = $aliasIniIdentity; aliasPathFacts = (Get-XDStarterPathFacts $alias);
              directBefore = $before; alias = $viaJunction; directAfter = $after;
              directFresh = $directFresh; aliasFresh = $aliasFresh }
}

$leaf = 'xdstarter-shell-matrix-' + $env:GITHUB_RUN_ID + '-' + $env:GITHUB_RUN_ATTEMPT + '-' + [Guid]::NewGuid().ToString('N')
function New-RootSpec([string]$Name, [string]$Parent) {
    $rootLeaf = $leaf + '-' + $Name
    return @{ name = $Name; parent = $Parent; leaf = $rootLeaf; path = (Join-Path $Parent $rootLeaf) }
}
$roots = @((New-RootSpec 'workspace' $workParent), (New-RootSpec 'profile' $profileParent))
$additionalRoots = @((New-RootSpec 'runner-temp' $runnerTempParent),
                     (New-RootSpec 'profile-local' $profileLocalParent))
$created = @()
$cases = @()
$parentCases = @()
$parentComparisons = @()
$aclCases = @()
$junctionCases = @()
$junctions = @()
$aclTemplates = @{}
$errors = @()
$cleanupErrors = @()
$comResult = $null
try {
    $comResult = [XDStarterShellMatrix]::CoInitializeEx([IntPtr]::Zero, 2)
    if ($comResult -lt 0) { throw "COM initialization failed: $comResult" }
    foreach ($root in ($roots + $additionalRoots)) {
        if (Test-Path -LiteralPath $root.path) { throw 'Fixture root already exists.' }
        New-Item -ItemType Directory -Path $root.path | Out-Null
        $created += $root
        if ($root.name -in @('workspace', 'profile')) {
            $module = Join-Path $root.path '本地化 资源 & helper.exe'
            Copy-Item -LiteralPath $helperPath -Destination $module
            $root.module = $module
            $root.moduleSha256 = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    if ($roots[0].moduleSha256 -ne $roots[1].moduleSha256) { throw 'Fixture modules differ.' }
    foreach ($folderRoot in $roots) {
        foreach ($moduleRoot in $roots) {
            $physical = 'matrix-' + $folderRoot.name + '-' + $moduleRoot.name
            $observation = New-ShellObservation $folderRoot $moduleRoot $physical
            $observation.folderLocation = $folderRoot.name
            $observation.moduleLocation = $moduleRoot.name
            $cases += $observation
        }
    }
    # Keep the resource module fixed while comparing different parent paths.
    foreach ($root in ($roots + $additionalRoots)) {
        $observation = New-ShellObservation $root $roots[0] ('parent-' + $root.name)
        $observation.parentName = $root.name
        $observation.parentPathSha256 = Get-PathHash $root.parent
        $observation.parentAttributes = [int](Get-Item -LiteralPath $root.parent -Force).Attributes
        $observation.parentAclSha256 = Get-AclHash $root.parent
        $observation.rootAclSha256 = Get-AclHash $root.path
        $parentCases += $observation
    }
    foreach ($pair in @(@('workspace', 'runner-temp'), @('profile', 'profile-local'))) {
        $left = @($parentCases | Where-Object parentName -eq $pair[0])[0]
        $right = @($parentCases | Where-Object parentName -eq $pair[1])[0]
        $parentComparisons += @{ left = $pair[0]; right = $pair[1];
            sameVolumeRoot = $left.folderDrive.Equals($right.folderDrive, [StringComparison]::OrdinalIgnoreCase);
            distinctParentPaths = $left.parentPathSha256 -ne $right.parentPathSha256;
            sameParentAcl = $left.parentAclSha256 -eq $right.parentAclSha256;
            resourceResolved = ($left.indirectResult -ge 0 -and $right.indirectResult -ge 0 -and
                                $left.indirectName -eq '本地化工具' -and $right.indirectName -eq '本地化工具') }
    }
    # Compare protected copies of each source fixture's access rules at each
    # target location. Only the newly-created target folder and desktop.ini
    # are modified; parent folders, policy and the source fixtures are untouched.
    foreach ($source in $roots) {
        $sourceFolder = Join-Path $source.path ('parent-' + $source.name)
        $aclTemplates[$source.name] = @{
            folder = New-AccessTemplate $sourceFolder $source.path
            ini = New-AccessTemplate (Join-Path $sourceFolder 'desktop.ini') $source.path
        }
    }
    foreach ($target in $roots) {
        foreach ($source in $roots) {
            $physical = 'acl-' + $target.name + '-' + $source.name
            $observation = New-ShellObservation $target $roots[0] $physical $aclTemplates[$source.name]
            $observation.targetLocation = $target.name
            $observation.templateLocation = $source.name
            $aclCases += $observation
        }
    }
    # Each alias names an already-owned target through a parent junction.
    # Same-volume junctions control for the reparse point itself; cross-volume
    # aliases change the visible path prefix without changing either object.
    foreach ($hostRoot in $roots) {
        foreach ($targetRoot in $roots) {
            $spec = New-XDStarterJunctionSpec $hostRoot $targetRoot
            $junctions += $spec # Register before creation, including on failure.
            Install-XDStarterFixtureJunction $spec
            $junctionCases += Get-JunctionObservation $spec
        }
    }
} catch { $errors += $_.Exception.Message }
finally {
    if ($null -ne $comResult -and $comResult -ge 0) { [XDStarterShellMatrix]::CoUninitialize() }
    foreach ($junction in $junctions) {
        try { Remove-XDStarterFixtureJunction $junction } catch {
            $cleanupErrors += $_.Exception.Message
            $errors += $_.Exception.Message
        }
    }
    foreach ($root in $created) {
        try { Remove-OwnedRoot $root.path $root.parent $root.leaf } catch {
            $cleanupErrors += $_.Exception.Message
            $errors += $_.Exception.Message
        }
    }
}
$remaining = @($created | Where-Object { Test-Path -LiteralPath $_.path })
$report = @{ schemaVersion = 6; cases = $cases; parentCases = $parentCases; parentComparisons = $parentComparisons;
             aclCases = $aclCases;
             junctionCases = $junctionCases;
             comInitializationResult = $comResult;
             cleanupComplete = ($cleanupErrors.Count -eq 0 -and $remaining.Count -eq 0);
             identity = @{ sha = $env:GITHUB_SHA; runId = $env:GITHUB_RUN_ID; attempt = $env:GITHUB_RUN_ATTEMPT;
                           runnerOs = $env:RUNNER_OS; runnerArch = $env:RUNNER_ARCH };
             osVersion = [Environment]::OSVersion.VersionString;
             errors = $errors; status = $(if ($errors.Count -eq 0 -and $cases.Count -eq 4 -and
                                             $parentCases.Count -eq 4 -and $parentComparisons.Count -eq 2 -and
                                             $aclCases.Count -eq 4 -and $junctionCases.Count -eq 4) { 'passed' } else { 'failed' });
             limitations = @('Fixture compares native Shell observations, not the cause of historical failures.',
                             'URL zone mapping is not a documented equivalent of the Shell desktop.ini trust decision.',
                             'Junction paths may themselves affect Shell behavior; file identity alone does not identify the cause.',
                             'Fresh processes can still observe shared system Shell caches and follow earlier parent queries.',
                             'A volume GUID comparison identifies a mounted volume, not a physical disk or trust policy.',
                             'Volume roots and resolved parent path spellings do not establish physical volume or directory identity.',
                             'ACL summaries do not establish effective permissions or parent-path effects.',
                             'Access templates apply to owned children, not parent directories or inherited-path policy.',
                             'Descriptor hashes do not prove that Windows evaluates two access tokens identically.') }
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outputPath -Encoding utf8NoBOM
if ($report.status -ne 'passed') { throw ('Shell cross-location fixture failed: ' + ($errors -join '; ')) }
Write-Host "Shell fixture collected $($cases.Count) cross-location, $($parentCases.Count) parent, $($aclCases.Count) access and $($junctionCases.Count) junction observations."
