# Helpers for temporary junctions between the Shell matrix's owned roots.
# The caller must remove every registered junction before recursively cleaning a root.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class XDStarterFileIdentity {
    [StructLayout(LayoutKind.Sequential)]
    private struct FileTime { public uint Low; public uint High; }
    [StructLayout(LayoutKind.Sequential)]
    private struct FileInformation {
        public uint Attributes;
        public FileTime CreationTime;
        public FileTime LastAccessTime;
        public FileTime LastWriteTime;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool GetFileInformationByHandle(SafeFileHandle handle, out FileInformation info);

    public static string Read(string path) {
        if (!Path.IsPathFullyQualified(path)) throw new ArgumentException("A fully qualified path is required.");
        string extendedPath = path.StartsWith(@"\\?\", StringComparison.Ordinal) ? path :
            path.StartsWith(@"\\", StringComparison.Ordinal) ? @"\\?\UNC\" + path.Substring(2) : @"\\?\" + path;
        using (var handle = CreateFileW(extendedPath, 0, 7, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero)) {
            if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            FileInformation info;
            if (!GetFileInformationByHandle(handle, out info))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return String.Format("{0:X8}:{1:X8}{2:X8}", info.VolumeSerialNumber,
                                 info.FileIndexHigh, info.FileIndexLow);
        }
    }
}
'@

function Get-XDStarterOwnedRoot($Root) {
    if ($Root.name -notin @('workspace', 'profile') -or
        $Root.leaf -notmatch ('^xdstarter-shell-matrix-[1-9][0-9]*-[1-9][0-9]*-[a-f0-9]{32}-' + $Root.name + '$')) {
        throw 'Unexpected Shell fixture root specification.'
    }
    $parent = (Resolve-Path -LiteralPath $Root.parent).Path
    $expected = [IO.Path]::GetFullPath((Join-Path $parent $Root.leaf))
    $actual = (Resolve-Path -LiteralPath $Root.path).Path
    $prefix = $parent.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (!$actual.Equals($expected, [StringComparison]::OrdinalIgnoreCase) -or
        !$actual.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
        ((Get-Item -LiteralPath $actual -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Refusing a fixture root outside its validated parent or through a reparse point.'
    }
    return $actual
}

function New-XDStarterJunctionSpec($HostRoot, $TargetRoot) {
    $hostPath = Get-XDStarterOwnedRoot $HostRoot
    $targetPath = Get-XDStarterOwnedRoot $TargetRoot
    $path = Join-Path $hostPath ('junction-' + $HostRoot.name + '-' + $TargetRoot.name)
    return @{ host = $HostRoot; target = $TargetRoot; path = $path; targetPath = $targetPath; created = $false }
}

function Test-XDStarterJunctionSpec($Spec) {
    $expected = New-XDStarterJunctionSpec $Spec.host $Spec.target
    if (!$Spec.path.Equals($expected.path, [StringComparison]::OrdinalIgnoreCase) -or
        !$Spec.targetPath.Equals($expected.targetPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Unexpected Shell fixture junction specification.'
    }
    return $expected
}

function Install-XDStarterFixtureJunction($Spec) {
    $expected = Test-XDStarterJunctionSpec $Spec
    if ($null -ne (Get-Item -LiteralPath $expected.path -Force -ErrorAction SilentlyContinue)) {
        throw 'Fixture junction already exists.'
    }
    New-Item -ItemType Junction -Path $expected.path -Target $expected.targetPath -ErrorAction Stop | Out-Null
    $Spec.created = $true
    Confirm-XDStarterFixtureJunction $Spec
}

function Confirm-XDStarterFixtureJunction($Spec) {
    $expected = Test-XDStarterJunctionSpec $Spec
    $item = Get-Item -LiteralPath $expected.path -Force
    if ($item.LinkType -ne 'Junction' -or
        !([IO.Path]::GetFullPath($item.Target)).Equals($expected.targetPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing an unexpected fixture junction or target.'
    }
}

function Remove-XDStarterFixtureJunction($Spec) {
    $expected = Test-XDStarterJunctionSpec $Spec
    if (!$Spec.created) { return }
    if ($null -eq (Get-Item -LiteralPath $expected.path -Force -ErrorAction SilentlyContinue)) { return }
    Confirm-XDStarterFixtureJunction $Spec
    # Never traverse a junction during cleanup, including on a failed observation.
    [IO.Directory]::Delete($expected.path, $false)
    if ((Test-Path -LiteralPath $expected.path) -or !(Test-Path -LiteralPath $expected.targetPath)) {
        throw 'Fixture junction removal did not preserve its target.'
    }
}

function Get-XDStarterIdentityHash([string]$Path) {
    $identity = [XDStarterFileIdentity]::Read($Path)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
        [Text.Encoding]::UTF8.GetBytes($identity))).ToLowerInvariant()
}

function Get-XDStarterFreshShellName([string]$Path, [string]$ProbeScript, [int]$TimeoutMilliseconds = 30000) {
    if (![IO.Path]::IsPathFullyQualified($Path) -or ![IO.Directory]::Exists($Path)) {
        throw 'A fully qualified fixture directory is required for the fresh process.'
    }
    if ($TimeoutMilliseconds -lt 1 -or $TimeoutMilliseconds -gt 30000) {
        throw 'Fresh Shell name probe timeout is outside the allowed range.'
    }
    $script = (Resolve-Path -LiteralPath $ProbeScript).Path
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', $script)) {
        $start.ArgumentList.Add($argument)
    }
    $start.Environment['XDSTARTER_SHELL_NAME_PROBE_PATH'] = $Path
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        if (!$process.Start()) { throw 'Cannot start the fresh Shell name probe.' }
        $started = $true
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit($TimeoutMilliseconds)) {
            $process.Kill($true)
            if (!$process.WaitForExit(5000)) { throw 'Timed-out Shell probe did not exit after termination.' }
            throw 'Fresh Shell name probe timed out.'
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0 -or ![string]::IsNullOrWhiteSpace($stderr)) {
            throw "Fresh Shell name probe failed with exit code $($process.ExitCode): $stderr"
        }
        $observation = $stdout | ConvertFrom-Json -AsHashtable -ErrorAction Stop
        if ($observation.processId -ne $process.Id -or $observation.processId -eq $PID) {
            throw 'Fresh Shell name probe did not report its child process identity.'
        }
        $observation.parentProcessId = $PID
        return $observation
    } finally {
        try {
            if ($started -and !$process.HasExited) {
                $process.Kill($true)
                if (!$process.WaitForExit(5000)) { throw 'Fresh Shell probe remained running during cleanup.' }
            }
        } finally {
            $process.Dispose()
        }
    }
}
