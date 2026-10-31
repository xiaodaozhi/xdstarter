# Read one owned fixture path in a fresh, noninteractive process. The path is
# passed in the child environment so it never needs shell command interpolation.
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$path = $env:XDSTARTER_SHELL_NAME_PROBE_PATH
if (!$IsWindows -or [string]::IsNullOrWhiteSpace($path) -or
    ![IO.Path]::IsPathFullyQualified($path) -or ![IO.Directory]::Exists($path)) {
    throw 'A fully qualified fixture directory is required.'
}
. (Join-Path $PSScriptRoot 'shell_junction_probe.ps1')

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class XDStarterFreshShellName {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct SHFILEINFO {
        public IntPtr hIcon;
        public int iIcon;
        public uint dwAttributes;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string displayName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=80)] public string typeName;
    }
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr SHGetFileInfoW(string path, uint attributes, ref SHFILEINFO info, uint size, uint flags);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
    public static extern int SHGetLocalizedName(string path, StringBuilder module, uint length, out int resourceId);
    [DllImport("ole32.dll")] public static extern int CoInitializeEx(IntPtr reserved, uint flags);
    [DllImport("ole32.dll")] public static extern void CoUninitialize();
}
'@

$com = [XDStarterFreshShellName]::CoInitializeEx([IntPtr]::Zero, 2)
if ($com -lt 0) { throw "COM initialization failed: $com" }
try {
    $folderIdentity = Get-XDStarterIdentityHash $path
    $iniIdentity = Get-XDStarterIdentityHash (Join-Path $path 'desktop.ini')
    # Read the display name first so metadata lookup cannot precede it in this process.
    $info = [XDStarterFreshShellName+SHFILEINFO]::new()
    $size = [Runtime.InteropServices.Marshal]::SizeOf([type][XDStarterFreshShellName+SHFILEINFO])
    $display = [XDStarterFreshShellName]::SHGetFileInfoW($path, 0, [ref]$info, $size, 0x200)
    $module = [Text.StringBuilder]::new(32768)
    $resourceId = 0
    $result = [XDStarterFreshShellName]::SHGetLocalizedName($path, $module, 32768, [ref]$resourceId)
    $moduleHash = $null
    $returnedId = $null
    if ($result -ge 0) {
        $moduleHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
            [Text.Encoding]::UTF8.GetBytes($module.ToString()))).ToLowerInvariant()
        $returnedId = $resourceId
    }
    $observation = @{ processId = $PID; comInitializationResult = $com; folderIdentitySha256 = $folderIdentity;
                      iniIdentitySha256 = $iniIdentity;
                      display = @{ available = ($display -ne [IntPtr]::Zero); name = [string]$info.displayName };
                      localized = @{ hresult = $result; resourceModuleSha256 = $moduleHash; resourceId = $returnedId } }
} finally {
    [XDStarterFreshShellName]::CoUninitialize()
}
$observation | ConvertTo-Json -Depth 5 -Compress
