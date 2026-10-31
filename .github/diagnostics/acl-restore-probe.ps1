$ErrorActionPreference = 'Stop'
if (!$IsWindows -or $env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'This probe runs only on a GitHub-hosted Windows runner.'
}
$workspace = (Resolve-Path -LiteralPath $env:GITHUB_WORKSPACE).Path
. (Join-Path $workspace 'scripts/windows_access_descriptor.ps1')
$workspaceParent = (Resolve-Path -LiteralPath (Join-Path $workspace 'build')).Path
$profileParent = (Resolve-Path -LiteralPath (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp')).Path
$parents = @(@{ name = 'workspace'; path = $workspaceParent }, @{ name = 'profile'; path = $profileParent })
$section = [Security.AccessControl.AccessControlSections]::Access

function Read-Access([string]$path) {
    return (Get-Acl -LiteralPath $path).GetSecurityDescriptorSddlForm($section)
}
function Write-Access([string]$path, [string]$root, [string]$descriptor) {
    $resolved = (Resolve-Path -LiteralPath $path).Path
    if (!$resolved.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        ((Get-Item -LiteralPath $resolved -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Probe target is outside its owned root or is a reparse point.'
    }
    $item = Get-Item -LiteralPath $resolved -Force
    $acl = Get-Acl -LiteralPath $resolved
    $acl.SetSecurityDescriptorSddlForm($descriptor, $section)
    if ($item -is [IO.DirectoryInfo]) {
        [IO.FileSystemAclExtensions]::SetAccessControl([IO.DirectoryInfo]$item, $acl)
    } else {
        [IO.FileSystemAclExtensions]::SetAccessControl([IO.FileInfo]$item, $acl)
    }
}
function Hash-Text([string]$value) {
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($value))).ToLowerInvariant()
}
function Describe-Access([string]$descriptor) {
    $raw = [Security.AccessControl.RawSecurityDescriptor]::new($descriptor)
    $aces = @()
    foreach ($ace in $raw.DiscretionaryAcl) {
        $bytes = [byte[]]::new($ace.BinaryLength)
        $ace.GetBinaryForm($bytes, 0)
        $row = @{ type = [string]$ace.AceType; flags = [int]$ace.AceFlags;
                  sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant() }
        if ($ace -is [Security.AccessControl.KnownAce]) {
            $row.mask = [int]$ace.AccessMask
            $row.sidSha256 = Hash-Text $ace.SecurityIdentifier.Value
        }
        $aces += $row
    }
    return @{ sha256 = (Hash-Text $descriptor); controlFlags = [string]$raw.ControlFlags;
              prefix = $descriptor.Split('(')[0]; aces = $aces }
}

foreach ($parent in $parents) {
    if ((Get-Item -LiteralPath $parent.path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Refusing a reparse-point parent.'
    }
    $leaf = 'xdstarter-acl-diagnostic-' + [Guid]::NewGuid().ToString('N')
    $root = Join-Path $parent.path $leaf
    if (Test-Path -LiteralPath $root) { throw 'Probe root already exists.' }
    New-Item -ItemType Directory -Path $root | Out-Null
    try {
        $root = (Resolve-Path -LiteralPath $root).Path
        $sourceFolder = Join-Path $root 'source'
        New-Item -ItemType Directory -Path $sourceFolder | Out-Null
        $sourceFile = Join-Path $sourceFolder 'desktop.ini'
        [IO.File]::WriteAllText($sourceFile, 'fixture')
        foreach ($kind in @('folder', 'file')) {
            $source = if ($kind -eq 'folder') { $sourceFolder } else { $sourceFile }
            $probe = Join-Path $root ('probe-' + $kind)
            if ($kind -eq 'folder') { New-Item -ItemType Directory -Path $probe | Out-Null }
            else { [IO.File]::WriteAllText($probe, '') }
            $original = Read-Access $probe
            $sourceAcl = Get-Acl -LiteralPath $source
            $sourceAcl.SetAccessRuleProtection($true, $true)
            $protected = $sourceAcl.GetSecurityDescriptorSddlForm($section)
            Write-Access $probe $root $protected
            $applied = Read-Access $probe
            Write-Access $probe $root $original
            $restored = Read-Access $probe
            $rulesRestored = Test-RestoredAccess $original $restored
            @{ location = $parent.name; kind = $kind; exactRestore = ($original -eq $restored);
               rulesRestored = $rulesRestored; before = (Describe-Access $original); applied = (Describe-Access $applied);
               after = (Describe-Access $restored) } | ConvertTo-Json -Depth 8 -Compress | Write-Output
            if (!$rulesRestored) { throw 'Restored access rules differ.' }
        }
    } finally {
        $expected = [IO.Path]::GetFullPath((Join-Path $parent.path $leaf))
        $resolved = (Resolve-Path -LiteralPath $root).Path
        if (!$resolved.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected probe root.' }
        $entries = @(Get-Item -LiteralPath $resolved -Force) + @(Get-ChildItem -LiteralPath $resolved -Force -Recurse)
        foreach ($entry in $entries) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unexpected probe reparse point.' }
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
        if (Test-Path -LiteralPath $resolved) { throw 'Probe root remains after cleanup.' }
        Write-Output ('CLEANED ' + $parent.name)
    }
}
