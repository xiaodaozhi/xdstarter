[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Procmon)

$ErrorActionPreference = 'Stop'
if (!$IsWindows -or $env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted' -or
    $env:GITHUB_RUN_ID -notmatch '^[1-9][0-9]*$' -or $env:GITHUB_RUN_ATTEMPT -notmatch '^[1-9][0-9]*$') {
    throw 'This experiment is restricted to a disposable GitHub-hosted Windows runner.'
}
if (!(New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'The disposable runner must be elevated.' }
if (Get-Process -Name 'Procmon*' -ErrorAction SilentlyContinue) { throw 'Refusing to interfere with an existing Procmon.' }
. (Join-Path $PSScriptRoot 'shell_junction_probe.ps1')

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Issue56Setup {
    [DllImport("shlwapi.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)] public static extern bool PathMakeSystemFolderW(string path);
    [DllImport("shlwapi.dll", CharSet=CharSet.Unicode)]
    public static extern int SHLoadIndirectString(string source, StringBuilder output, uint length, IntPtr reserved);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern int SHSetLocalizedName(string path, string resource, int resourceId);
    [DllImport("ole32.dll")] public static extern int CoInitializeEx(IntPtr reserved, uint flags);
    [DllImport("ole32.dll")] public static extern void CoUninitialize();
}
'@

function Start-Tool([string]$Executable, [string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    foreach ($arg in $Arguments) { $info.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::Start($info)
    if (!$process) { throw 'Unable to start diagnostic process.' }
    return $process
}
function Wait-Tool($Process, [int]$Seconds, [string]$Operation) {
    if (!$Process.WaitForExit($Seconds * 1000)) {
        $Process.Kill($true)
        $Process.WaitForExit()
        throw "$Operation timed out."
    }
    if ($Process.ExitCode -ne 0) { throw "$Operation failed: $($Process.ExitCode)" }
}
function Remove-OwnedRoot($Root) {
    $resolved = Get-XDStarterOwnedRoot $Root
    $entries = @(Get-Item -LiteralPath $resolved -Force) + @(Get-ChildItem -LiteralPath $resolved -Force -Recurse)
    foreach ($entry in $entries) {
        $absolute = [IO.Path]::GetFullPath($entry.FullName)
        if ((!$absolute.Equals($resolved, [StringComparison]::OrdinalIgnoreCase) -and
             !$absolute.StartsWith($resolved + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) -or
            ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unexpected cleanup path.' }
    }
    foreach ($entry in $entries) { [IO.File]::SetAttributes($entry.FullName, [IO.FileAttributes]::Normal) }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

$workspace = (Resolve-Path -LiteralPath $env:GITHUB_WORKSPACE).Path
$build = Join-Path $workspace 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$profileParent = (Resolve-Path -LiteralPath (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp')).Path
$workParent = (Resolve-Path -LiteralPath $build).Path
$leaf = 'xdstarter-shell-matrix-' + $env:GITHUB_RUN_ID + '-' + $env:GITHUB_RUN_ATTEMPT + '-' + [Guid]::NewGuid().ToString('N')
$roots = @(
    @{name='workspace'; parent=$workParent; leaf=($leaf+'-workspace'); path=(Join-Path $workParent ($leaf+'-workspace'))},
    @{name='profile'; parent=$profileParent; leaf=($leaf+'-profile'); path=(Join-Path $profileParent ($leaf+'-profile'))}
)
$artifact = Join-Path $build 'issue56-trace-artifact'
$staging = Join-Path $build 'issue56-trace-staging'
if ((Test-Path -LiteralPath $artifact) -or (Test-Path -LiteralPath $staging)) { throw 'Output already exists.' }
New-Item -ItemType Directory -Path $artifact,$staging | Out-Null
$created = @()
$junctions = @()
$monitor = $null
$com = $null
try {
    $com = [Issue56Setup]::CoInitializeEx([IntPtr]::Zero, 2)
    if ($com -lt 0) { throw "COM setup failed: $com" }
    foreach ($root in $roots) {
        if ((Get-Item -LiteralPath $root.parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Fixture parent is a reparse point.'
        }
        if (Test-Path -LiteralPath $root.path) { throw 'Fixture path exists.' }
        New-Item -ItemType Directory -Path $root.path | Out-Null
        $created += $root
        $folder = Join-Path $root.path ('target-' + $root.name)
        New-Item -ItemType Directory -Path $folder | Out-Null
        if (![Issue56Setup]::PathMakeSystemFolderW($folder)) { throw 'Folder setup failed.' }
        $module = Join-Path $env:WINDIR 'System32/shell32.dll'
        $result = [Issue56Setup]::SHSetLocalizedName($folder, $module, 21799)
        if ($result -lt 0) { throw "SHSetLocalizedName failed: $result" }
        [IO.File]::SetAttributes((Join-Path $folder 'desktop.ini'),
            [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System)
    }
    foreach ($pair in @(@($roots[0],$roots[1]), @($roots[1],$roots[0]))) {
        $spec = New-XDStarterJunctionSpec $pair[0] $pair[1]
        $junctions += $spec
        Install-XDStarterFixtureJunction $spec
    }
    $probe = Join-Path $staging 'shell-name-probe.exe'
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    & $compiler /nologo /target:exe ("/out:$probe") (Join-Path $PSScriptRoot 'experimental_issue56_probe.cs')
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $probe)) { throw 'Probe compilation failed.' }

    $queries = @(
        @{label='workspace-direct'; path=(Join-Path $roots[0].path 'target-workspace')},
        @{label='workspace-via-profile'; path=(Join-Path $junctions[1].path 'target-workspace')},
        @{label='profile-direct'; path=(Join-Path $roots[1].path 'target-profile')},
        @{label='profile-via-workspace'; path=(Join-Path $junctions[0].path 'target-profile')}
    )
    foreach ($q in $queries) {
        $q.folderId = Get-XDStarterIdentityHash $q.path
        $q.iniId = Get-XDStarterIdentityHash (Join-Path $q.path 'desktop.ini')
    }
    if ($queries[0].folderId -ne $queries[1].folderId -or $queries[0].iniId -ne $queries[1].iniId -or
        $queries[2].folderId -ne $queries[3].folderId -or $queries[2].iniId -ne $queries[3].iniId) {
        throw 'Aliases do not identify the same owned objects.'
    }
    $pml = Join-Path $staging 'capture.pml'
    $csv = Join-Path $staging 'capture.csv'
    $monitor = Start-Tool $Procmon @('/AcceptEula','/Quiet','/Minimized','/NoFilter','/Runtime','50','/BackingFile',$pml)
    Wait-Tool (Start-Tool $Procmon @('/WaitForIdle','/Quiet')) 30 'Procmon readiness'
    foreach ($q in $queries) {
        # One process makes only this one Shell name query; no earlier parent display query.
        $lines = @(& $probe $q.path)
        if ($LASTEXITCODE -ne 0 -or $lines.Count -ne 1) { throw "Shell probe failed: $($q.label)" }
        $parts = $lines[0] -split '\|', 3
        if ($parts.Count -ne 3 -or $parts[0] -notmatch '^[1-9][0-9]*$' -or $parts[1] -ne '1') {
            throw "Malformed Shell probe result: $($q.label)"
        }
        $q.pid = [int]$parts[0]
        $q.name = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($parts[2]))
    }
    Wait-Tool (Start-Tool $Procmon @('/Terminate','/Quiet')) 30 'Procmon termination'
    if (!$monitor.WaitForExit(30000)) { throw 'Procmon capture did not terminate.' }
    # Procmon 4.12 can return 1 after an explicit /Terminate. Record it and
    # require a readable export with events from all four probes below.
    $captureExitCode = $monitor.ExitCode
    $monitor = $null
    if (!(Test-Path -LiteralPath $pml)) { throw 'Procmon capture is missing.' }
    Wait-Tool (Start-Tool $Procmon @('/OpenLog',$pml,'/SaveAs',$csv,'/Quiet','/NoConnect')) 120 'Procmon export'
    if (!(Test-Path -LiteralPath $csv)) { throw 'Procmon export is missing.' }

    $indirect = [Text.StringBuilder]::new(1024)
    $indirectResult = [Issue56Setup]::SHLoadIndirectString(('@'+$module+',-21799'), $indirect, 1024, [IntPtr]::Zero)
    if ($indirectResult -lt 0 -or $indirect.Length -eq 0) { throw 'System resource did not resolve.' }
    $publicQueries = @($queries | ForEach-Object { @{label=$_.label; pid=$_.pid; name=$_.name;
        folderId=$_.folderId; iniId=$_.iniId} })
    $report = @{sha=$env:GITHUB_SHA; runId=$env:GITHUB_RUN_ID; attempt=$env:GITHUB_RUN_ATTEMPT;
        os=[Environment]::OSVersion.VersionString; expectedName=$indirect.ToString();
        workspaceDrive=[IO.Path]::GetPathRoot($roots[0].path); profileDrive=[IO.Path]::GetPathRoot($roots[1].path);
        probesSha256=(Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash.ToLowerInvariant();
        captureExitCode=$captureExitCode; queries=$publicQueries}
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $artifact 'probes.json') -Encoding utf8NoBOM
    @{work=$roots[0].path; profile=$roots[1].path; workspace=$workspace;
      userprofile=$env:USERPROFILE; windir=$env:WINDIR} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $staging 'roots.json') -Encoding utf8NoBOM
    & python (Join-Path $PSScriptRoot 'experimental_issue56_filter.py') $csv (Join-Path $staging 'roots.json') (Join-Path $artifact 'probes.json') $artifact
    if ($LASTEXITCODE -ne 0) { throw 'Could not filter probe events.' }
} finally {
    if ($monitor -and !$monitor.HasExited) {
        try { Wait-Tool (Start-Tool $Procmon @('/Terminate','/Quiet')) 20 'Procmon cleanup' } catch { Write-Warning $_ }
        if (!$monitor.WaitForExit(20000)) { $monitor.Kill($true); $monitor.WaitForExit() }
    }
    if ($null -ne $com -and $com -ge 0) { [Issue56Setup]::CoUninitialize() }
    foreach ($junction in $junctions) { Remove-XDStarterFixtureJunction $junction }
    foreach ($root in $created) { Remove-OwnedRoot $root }
}
Write-Host 'Completed the bounded owned-process Shell trace; raw system-wide capture remains on the disposable runner.'
