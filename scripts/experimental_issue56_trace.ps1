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
using Microsoft.Win32.SafeHandles;
public static class Issue56Setup {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    private struct SHFILEINFO {
        public IntPtr icon;
        public int iconIndex;
        public uint attributes;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string display;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=80)] public string type;
    }
    public sealed class StorageFacts {
        public int openError;
        public int queryError;
        public uint returned;
        public int deviceType;
        public int removableMedia;
        public uint busType;
        public string vendor;
        public string product;
        public int hotplugError;
        public uint hotplugBytes;
        public int hotplugMediaRemovable;
        public int mediaHotplug;
        public int deviceHotplug;
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeviceIoControl(SafeFileHandle handle, uint control,
        [In] byte[] input, uint inputSize, [Out] byte[] output, uint outputSize, out uint returned, IntPtr overlapped);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    private static extern IntPtr SHGetFileInfoW(string path, uint attributes, ref SHFILEINFO info, uint size, uint flags);
    private static string Field(byte[] data, uint count, uint offset) {
        if (offset == 0 || offset >= count) return null;
        int start = (int)offset, end = start;
        while (end < count && end - start < 128 && data[end] != 0) ++end;
        return Encoding.ASCII.GetString(data, start, end - start).Trim();
    }
    public static StorageFacts QueryStorage(string root) {
        var result = new StorageFacts();
        using (var handle = CreateFileW(@"\\.\" + root.Substring(0, 2), 0, 7, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if (handle.IsInvalid) { result.openError = Marshal.GetLastWin32Error(); return result; }
            var query = new byte[12]; var data = new byte[4096]; uint count;
            if (!DeviceIoControl(handle, 0x002D1400, query, (uint)query.Length, data, (uint)data.Length,
                                 out count, IntPtr.Zero)) {
                result.queryError = Marshal.GetLastWin32Error(); return result;
            }
            result.returned = count;
            if (count >= 36) {
                result.deviceType = data[8]; result.removableMedia = data[10];
                result.busType = BitConverter.ToUInt32(data, 28);
                result.vendor = Field(data, count, BitConverter.ToUInt32(data, 12));
                result.product = Field(data, count, BitConverter.ToUInt32(data, 16));
            }
            // IOCTL_STORAGE_GET_HOTPLUG_INFO is read-only; no device property is set.
            var hotplug = new byte[8]; hotplug[0] = 8; uint hotplugCount;
            if (!DeviceIoControl(handle, 0x002D0C14, null, 0, hotplug, (uint)hotplug.Length,
                                 out hotplugCount, IntPtr.Zero)) {
                result.hotplugError = Marshal.GetLastWin32Error();
            } else {
                result.hotplugBytes = hotplugCount;
                if (hotplugCount >= 8) {
                    result.hotplugMediaRemovable = hotplug[4]; result.mediaHotplug = hotplug[5];
                    result.deviceHotplug = hotplug[6];
                }
            }
            return result;
        }
    }
    public static long[] ShellDriveAttributes(string root) {
        var info = new SHFILEINFO();
        info.attributes = 0x62000000; // FILESYSTEM, FOLDER, REMOVABLE
        var result = SHGetFileInfoW(root, 0, ref info, (uint)Marshal.SizeOf(typeof(SHFILEINFO)), 0x20800);
        return new long[] {result == IntPtr.Zero ? 0 : 1, info.attributes};
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern uint GetDriveTypeW(string root);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetVolumeInformationW(string root, StringBuilder name, uint nameSize,
        out uint serial, out uint maxComponent, out uint flags, StringBuilder filesystem, uint filesystemSize);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)] public static extern bool SetVolumeLabelW(string root, string label);
    [DllImport("shlwapi.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)] public static extern bool PathMakeSystemFolderW(string path);
    [DllImport("shlwapi.dll", CharSet=CharSet.Unicode)]
    public static extern int SHLoadIndirectString(string source, StringBuilder output, uint length, IntPtr reserved);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern int SHSetLocalizedName(string path, string resource, int resourceId);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern void SHChangeNotify(int eventId, uint flags, string path, IntPtr unused);
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
function Invoke-Probe([string]$Executable, [string]$Path) {
    $info = [Diagnostics.ProcessStartInfo]::new($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $info.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    $info.ArgumentList.Add($Path)
    $process = [Diagnostics.Process]::Start($info)
    if (!$process) { throw 'Unable to start the Shell probe.' }
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(15000)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw 'Shell probe timed out.'
        }
        $output = $stdout.GetAwaiter().GetResult().Trim()
        $errorText = $stderr.GetAwaiter().GetResult().Trim()
        if ($process.ExitCode -ne 0 -or $errorText) { throw "Shell probe failed ($($process.ExitCode)): $errorText" }
        return $output
    } finally { $process.Dispose() }
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
function Get-VolumeLabel([string]$Root) {
    $name = [Text.StringBuilder]::new(261)
    $filesystem = [Text.StringBuilder]::new(261)
    [uint32]$serial = 0; [uint32]$component = 0; [uint32]$flags = 0
    if (![Issue56Setup]::GetVolumeInformationW($Root, $name, 261, [ref]$serial,
            [ref]$component, [ref]$flags, $filesystem, 261)) {
        throw "Cannot read volume label: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    return $name.ToString()
}
function Set-TemporaryVolumeLabel([string]$Root, [string]$Label) {
    if (![Issue56Setup]::SetVolumeLabelW($Root, $Label)) {
        throw "Cannot set volume label: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    [Issue56Setup]::SHChangeNotify(0x2000, 0x1005, $Root, [IntPtr]::Zero)
    if ((Get-VolumeLabel $Root) -ne $Label) { throw 'Volume label readback did not match.' }
}
function Restore-OwnedNamespacePolicy($State) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($State.path, $true)
    if (!$key) { throw 'Temporary namespace policy key disappeared.' }
    try {
        if ($State.written) {
            if ($key.GetValue($State.name, $null) -ne 1 -or
                $key.GetValueKind($State.name) -ne [Microsoft.Win32.RegistryValueKind]::DWord) {
                throw 'Temporary namespace policy value changed unexpectedly.'
            }
            if ($State.valueExisted) { $key.SetValue($State.name, $State.oldValue, $State.oldKind) }
            else { $key.DeleteValue($State.name, $false) }
            $key.Flush()
        }
        $empty = $key.GetValueNames().Count -eq 0 -and $key.GetSubKeyNames().Count -eq 0
    } finally { $key.Dispose() }
    if (!$State.keyExisted -and $empty) {
        [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKey($State.path, $false)
    }
    [Issue56Setup]::SHChangeNotify(0x08000000, 0x1000, $null, [IntPtr]::Zero)
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
$labelChanged = $false
$labelRoot = $null
$originalLabel = $null
$namespaceState = $null
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
        foreach ($kind in @('target', 'marked')) {
            $folder = Join-Path $root.path ($kind + '-' + $root.name)
            New-Item -ItemType Directory -Path $folder | Out-Null
            if (![Issue56Setup]::PathMakeSystemFolderW($folder)) { throw 'Folder setup failed.' }
            $module = Join-Path $env:WINDIR 'System32/shell32.dll'
            $result = [Issue56Setup]::SHSetLocalizedName($folder, $module, 21799)
            if ($result -lt 0) { throw "SHSetLocalizedName failed: $result" }
            $ini = Join-Path $folder 'desktop.ini'
            [IO.File]::SetAttributes($ini, [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System)
            if ($kind -eq 'marked') {
                Set-Content -LiteralPath $ini -Stream 'Zone.Identifier' -Encoding ascii -Value "[ZoneTransfer]`r`nZoneId=3"
                if ((Get-Content -LiteralPath $ini -Stream 'Zone.Identifier' -Raw) -notmatch 'ZoneId=3') {
                    throw 'Owned source marker did not persist.'
                }
            } elseif (@(Get-Item -LiteralPath $ini -Stream '*' -Force | Where-Object Stream -eq 'Zone.Identifier').Count -ne 0) {
                throw 'The unmarked owned fixture unexpectedly has a source marker.'
            }
        }
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
    function Read-Query($q) {
        $line = Invoke-Probe $probe $q.path
        $parts = $line -split '\|', 6
        if ($parts.Count -ne 6 -or $parts[0] -notmatch '^[1-9][0-9]*$' -or $parts[1] -ne '1') {
            throw "Malformed Shell probe result: $($q.label)"
        }
        $q.pid = [int]$parts[0]
        $q.name = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($parts[2]))
        $route = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($parts[3]))
        $computer = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($parts[4]))
        $computerParts = $computer -split "`t", 2
        $q.computerName = $computerParts[0]
        $itemNames = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($parts[5])) -split "`t", 2
        if ($itemNames.Count -ne 2 -or $itemNames[0] -like '<HRESULT:*' -or $itemNames[1] -like '<HRESULT:*') {
            throw "Shell item query failed: $($q.label)"
        }
        $q.itemName = $itemNames[0]
        $q.computerItemName = $itemNames[1]
        $q.computerSameObject = $false
        if ($computerParts.Count -eq 2 -and [IO.Directory]::Exists($computerParts[1])) {
            $q.computerSameObject = (Get-XDStarterIdentityHash $computerParts[1]) -eq $q.folderId
        }
        foreach ($substitution in @(@($roots[0].path, '$WORK'), @($roots[1].path, '$PROFILE'),
                                     @($workspace, '$WORKSPACE'), @($env:USERPROFILE, '$USERPROFILE'))) {
            $route = $route.Replace($substitution[0], $substitution[1], [StringComparison]::OrdinalIgnoreCase)
        }
        $q.route = $route.Trim()
    }
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
        # Each fresh process calls SHGetFileInfo first and then records the
        # Shell hierarchy and the application's IShellItem API; the parent has
        # not queried a display name for these objects.
        Read-Query $q
    }
    # Allow the bounded capture consumer to flush the last completed probe.
    # Completeness is still checked independently from the exported events.
    Start-Sleep -Seconds 3
    Wait-Tool (Start-Tool $Procmon @('/Terminate','/Quiet')) 30 'Procmon termination'
    if (!$monitor.WaitForExit(30000)) { throw 'Procmon capture did not terminate.' }
    # Procmon 4.12 can return 1 after an explicit /Terminate. Record it and
    # require a readable export with events from all four probes below.
    $captureExitCode = $monitor.ExitCode
    $monitor = $null
    if (!(Test-Path -LiteralPath $pml)) { throw 'Procmon capture is missing.' }
    Wait-Tool (Start-Tool $Procmon @('/OpenLog',$pml,'/SaveAs',$csv,'/Quiet','/NoConnect')) 120 'Procmon export'
    if (!(Test-Path -LiteralPath $csv)) { throw 'Procmon export is missing.' }

    # Compare the same two namespace entry points on freshly-created marked
    # fixtures. These separate probes occur after the captured unmarked cases.
    $marked = @(
        @{label='marked-workspace-direct'; path=(Join-Path $roots[0].path 'marked-workspace')},
        @{label='marked-workspace-via-profile'; path=(Join-Path $junctions[1].path 'marked-workspace')},
        @{label='marked-profile-direct'; path=(Join-Path $roots[1].path 'marked-profile')},
        @{label='marked-profile-via-workspace'; path=(Join-Path $junctions[0].path 'marked-profile')}
    )
    foreach ($q in $marked) {
        $q.folderId = Get-XDStarterIdentityHash $q.path
        $q.iniId = Get-XDStarterIdentityHash (Join-Path $q.path 'desktop.ini')
        Read-Query $q
    }
    if ($marked[0].folderId -ne $marked[1].folderId -or $marked[0].iniId -ne $marked[1].iniId -or
        $marked[2].folderId -ne $marked[3].folderId -or $marked[2].iniId -ne $marked[3].iniId) {
        throw 'Marked aliases do not identify the same owned objects.'
    }

    # Only the disposable runner's workspace volume can be tested. This never
    # touches the system drive and restores its original label on every exit.
    $labelRoot = [IO.Path]::GetPathRoot($roots[0].path)
    $systemRoot = [IO.Path]::GetPathRoot($env:WINDIR)
    $labelExperiment = @{ status='skipped'; reason='Workspace and system volume are not distinct fixed drives.' }
    if (!$labelRoot.Equals($systemRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [Issue56Setup]::GetDriveTypeW($labelRoot) -eq 3 -and
        $workspace.StartsWith($labelRoot, [StringComparison]::OrdinalIgnoreCase)) {
        $originalLabel = Get-VolumeLabel $labelRoot
        $alternateLabel = if ($originalLabel -eq 'Temp') { 'Temporary Storage' } else { 'Temp' }
        # Register restoration before making the first mutation.
        $labelChanged = $true
        Set-TemporaryVolumeLabel $labelRoot $alternateLabel
        $alternateQueries = @()
        foreach ($original in $queries) {
            $q = @{label=$original.label; path=$original.path; folderId=$original.folderId; iniId=$original.iniId}
            Read-Query $q
            $alternateQueries += @{label=$q.label; name=$q.name; route=$q.route;
                computerName=$q.computerName; computerSameObject=$q.computerSameObject;
                itemName=$q.itemName; computerItemName=$q.computerItemName}
        }
        Set-TemporaryVolumeLabel $labelRoot $originalLabel
        $labelChanged = $false
        $restoredQueries = @()
        foreach ($original in $queries) {
            $q = @{label=$original.label; path=$original.path; folderId=$original.folderId; iniId=$original.iniId}
            Read-Query $q
            $restoredQueries += @{label=$q.label; name=$q.name; route=$q.route;
                computerName=$q.computerName; computerSameObject=$q.computerSameObject;
                itemName=$q.itemName; computerItemName=$q.computerItemName}
        }
        $labelExperiment = @{status='completed'; original=$originalLabel; alternate=$alternateLabel;
                             restored=(Get-VolumeLabel $labelRoot); alternateQueries=$alternateQueries;
                             restoredQueries=$restoredQueries}
    }

    # Only the disposable runner account's Explorer namespace visibility is
    # changed. Preserve and restore the exact previous value and type; do not
    # touch source-zone or security policy.
    $policyPath = 'Software\Microsoft\Windows\CurrentVersion\Policies\NonEnum'
    $policyName = '{F5FB2C77-0E2F-4A16-A381-3E560C68BC83}'
    $policyParent = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(
        'Software\Microsoft\Windows\CurrentVersion\Policies', $false)
    if (!$policyParent) { throw 'Expected disposable namespace policy parent is missing.' }
    $policyParent.Dispose()
    $policyKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($policyPath, $true)
    $policyKeyExisted = $null -ne $policyKey
    if (!$policyKey) { $policyKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($policyPath) }
    if (!$policyKey) { throw 'Unable to open the disposable namespace policy key.' }
    try {
        $valueExisted = @($policyKey.GetValueNames()) -contains $policyName
        $oldValue = if ($valueExisted) { $policyKey.GetValue($policyName, $null,
            [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) } else { $null }
        $oldKind = if ($valueExisted) { $policyKey.GetValueKind($policyName) } else { $null }
        $namespaceState = @{path=$policyPath; name=$policyName; keyExisted=$policyKeyExisted;
                            valueExisted=$valueExisted; oldValue=$oldValue; oldKind=$oldKind; written=$false}
        $policyKey.SetValue($policyName, 1, [Microsoft.Win32.RegistryValueKind]::DWord)
        $namespaceState.written = $true
        $policyKey.Flush()
    } finally { $policyKey.Dispose() }
    [Issue56Setup]::SHChangeNotify(0x08000000, 0x1000, $null, [IntPtr]::Zero)
    $hiddenNamespaceQueries = @()
    foreach ($original in $queries) {
        $q = @{label=$original.label; path=$original.path; folderId=$original.folderId; iniId=$original.iniId}
        Read-Query $q
        $hiddenNamespaceQueries += @{label=$q.label; name=$q.name; route=$q.route;
            computerName=$q.computerName; computerSameObject=$q.computerSameObject;
            itemName=$q.itemName; computerItemName=$q.computerItemName}
    }
    Restore-OwnedNamespacePolicy $namespaceState
    $namespaceState = $null
    $restoredNamespaceQueries = @()
    foreach ($original in $queries) {
        $q = @{label=$original.label; path=$original.path; folderId=$original.folderId; iniId=$original.iniId}
        Read-Query $q
        $restoredNamespaceQueries += @{label=$q.label; name=$q.name; route=$q.route;
            computerName=$q.computerName; computerSameObject=$q.computerSameObject;
            itemName=$q.itemName; computerItemName=$q.computerItemName}
    }
    $namespaceExperiment = @{status='completed'; keyPreviouslyExisted=$policyKeyExisted;
                             valuePreviouslyExisted=$valueExisted; hiddenQueries=$hiddenNamespaceQueries;
                             restoredQueries=$restoredNamespaceQueries}

    $indirect = [Text.StringBuilder]::new(1024)
    $indirectResult = [Issue56Setup]::SHLoadIndirectString(('@'+$module+',-21799'), $indirect, 1024, [IntPtr]::Zero)
    if ($indirectResult -lt 0 -or $indirect.Length -eq 0) { throw 'System resource did not resolve.' }
    $publicQueries = @($queries | ForEach-Object { @{label=$_.label; pid=$_.pid; name=$_.name; route=$_.route;
        computerName=$_.computerName; computerSameObject=$_.computerSameObject; itemName=$_.itemName;
        computerItemName=$_.computerItemName; folderId=$_.folderId; iniId=$_.iniId} })
    $publicMarked = @($marked | ForEach-Object { @{label=$_.label; pid=$_.pid; name=$_.name; route=$_.route;
        computerName=$_.computerName; computerSameObject=$_.computerSameObject; itemName=$_.itemName;
        computerItemName=$_.computerItemName; folderId=$_.folderId; iniId=$_.iniId} })
    $workDrive = [IO.Path]::GetPathRoot($roots[0].path)
    $profileDrive = [IO.Path]::GetPathRoot($roots[1].path)
    $report = @{sha=$env:GITHUB_SHA; runId=$env:GITHUB_RUN_ID; attempt=$env:GITHUB_RUN_ATTEMPT;
        os=[Environment]::OSVersion.VersionString; expectedName=$indirect.ToString();
        workspaceDrive=$workDrive; profileDrive=$profileDrive;
        workspaceDriveType=[Issue56Setup]::GetDriveTypeW($workDrive);
        profileDriveType=[Issue56Setup]::GetDriveTypeW($profileDrive);
        workspaceStorage=[Issue56Setup]::QueryStorage($workDrive);
        profileStorage=[Issue56Setup]::QueryStorage($profileDrive);
        workspaceShellAttributes=[Issue56Setup]::ShellDriveAttributes($workDrive);
        profileShellAttributes=[Issue56Setup]::ShellDriveAttributes($profileDrive);
        namespaceLabel=[Microsoft.Win32.Registry]::GetValue('HKEY_CLASSES_ROOT\CLSID\{F5FB2C77-0E2F-4A16-A381-3E560C68BC83}', '', $null);
        probesSha256=(Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash.ToLowerInvariant();
        captureExitCode=$captureExitCode; queries=$publicQueries; markedZoneId=3; markedQueries=$publicMarked;
        volumeLabelExperiment=$labelExperiment; namespaceVisibilityExperiment=$namespaceExperiment}
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
    try { if ($namespaceState) { Restore-OwnedNamespacePolicy $namespaceState } }
    finally {
        try { if ($labelChanged) { Set-TemporaryVolumeLabel $labelRoot $originalLabel } }
        finally {
            if ($null -ne $com -and $com -ge 0) { [Issue56Setup]::CoUninitialize() }
            foreach ($junction in $junctions) { Remove-XDStarterFixtureJunction $junction }
            foreach ($root in $created) { Remove-OwnedRoot $root }
        }
    }
}
Write-Host 'Completed the bounded owned-process Shell trace; raw system-wide capture remains on the disposable runner.'
