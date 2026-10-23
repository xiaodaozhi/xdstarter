# scripts/verify-taskbar-integration.ps1

param(
    [string]$BuildDirectory = 'build/app-release',
    [string]$OutputDirectory = 'build/validation/issue45',
    [switch]$AlignmentMatrix
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$buildRoot = (Resolve-Path -LiteralPath (Join-Path $repository $BuildDirectory)).Path
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repository $OutputDirectory))
if (!$outputRoot.StartsWith($repository + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Validation output must stay inside the repository.'
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$nativeProbe = Join-Path $buildRoot 'xdstarter_native_taskbar_probe.exe'
$anchorProbe = Join-Path $buildRoot 'xdstarter_desktop_platform_tests.exe'
$oldPath = $env:PATH
$env:PATH = (Join-Path $repository 'build/Qt/6.8.2/msvc2022_64/bin') + [IO.Path]::PathSeparator + $env:PATH
$key = $null
$original = $null
$kind = $null
$hadValue = $false
$changed = $false
$records = [Collections.Generic.List[object]]::new()

function Save-Probe([string]$Name) {
    $placement = & $nativeProbe --run
    $placementExit = $LASTEXITCODE
    $placement | Set-Content -LiteralPath (Join-Path $outputRoot "$Name-placement.json") -Encoding utf8
    if ($placementExit -ne 0) { throw "Native placement probe failed: $placementExit" }
    $anchor = & $anchorProbe --probe-anchor
    if ($LASTEXITCODE -ne 0) { throw "Start anchor probe failed: $LASTEXITCODE" }
    $anchor | Set-Content -LiteralPath (Join-Path $outputRoot "$Name-anchor.json") -Encoding utf8
    $records.Add([ordered]@{ phase = $Name; anchor = ($anchor -join "`n" | ConvertFrom-Json) })
}

try {
    Save-Probe 'original'
    if ($AlignmentMatrix) {
        # Only this documented alignment preference is changed. Never restart
        # Explorer, rewrite StuckRects blobs, or infer unsupported positions.
        $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(
            'Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced', $true)
        if (!$key) { throw 'Explorer alignment preference is unavailable.' }
        $hadValue = $key.GetValueNames() -contains 'TaskbarAl'
        if ($hadValue) {
            $original = $key.GetValue('TaskbarAl')
            $kind = $key.GetValueKind('TaskbarAl')
            if ($kind -ne [Microsoft.Win32.RegistryValueKind]::DWord) {
                throw 'Unexpected TaskbarAl type; no settings changed.'
            }
        }
        [ordered]@{ hadValue = $hadValue; value = $original; kind = [string]$kind } |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'alignment-original.json') -Encoding utf8
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class XDStarterTaskbarSettings {
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint message, UIntPtr wparam,
        string lparam, uint flags, uint timeout, out UIntPtr result);
    public static void Notify() {
        UIntPtr result;
        SendMessageTimeout(new IntPtr(0xffff), 0x1a, UIntPtr.Zero,
            @"Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced", 2, 500, out result);
    }
}
'@
        foreach ($alignment in @(0, 1)) {
            $changed = $true
            $key.SetValue('TaskbarAl', $alignment, [Microsoft.Win32.RegistryValueKind]::DWord)
            [XDStarterTaskbarSettings]::Notify()
            Start-Sleep -Milliseconds 1500
            Save-Probe "alignment-$alignment"
        }
    }
} finally {
    try {
        if ($changed) {
            if ($hadValue) { $key.SetValue('TaskbarAl', $original, $kind) }
            else { $key.DeleteValue('TaskbarAl', $false) }
            [XDStarterTaskbarSettings]::Notify()
            Start-Sleep -Milliseconds 1500
            Save-Probe 'restored'
            $restored = ($key.GetValueNames() -contains 'TaskbarAl') -eq $hadValue -and
                (!$hadValue -or ($key.GetValue('TaskbarAl') -eq $original -and $key.GetValueKind('TaskbarAl') -eq $kind))
            if (!$restored) { throw 'Taskbar alignment restoration did not verify.' }
        }
        [ordered]@{
            schemaVersion = 1
            alignmentMatrixRequested = [bool]$AlignmentMatrix
            preferenceRestored = !$changed -or $restored
            scope = 'Native own-window geometry; alignment is confirmed only when live UIA Start bounds change. Synthetic dock cases do not replace actual Explorer position/physical input tests.'
            records = $records.ToArray()
        } | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $outputRoot 'taskbar-matrix.json') -Encoding utf8
    } finally {
        if ($key) { $key.Dispose() }
        $env:PATH = $oldPath
    }
}
