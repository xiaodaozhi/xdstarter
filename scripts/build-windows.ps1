param(
    [string]$Python = 'python',
    [string]$QtPrefix = $env:XDSTARTER_QT_ROOT,
    [ValidateSet('app-debug', 'app-release', 'core-debug', 'core-release')]
    [string]$Preset = 'app-release',
    [ValidateSet('Baseline', 'MSVC2022')]
    [string]$Toolchain = 'Baseline',
    [switch]$InstallTools,
    [switch]$DesignAssets,
    [switch]$Fresh
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ($DesignAssets -and $Preset.StartsWith('core-')) {
    throw '-DesignAssets requires an app preset and the declared Qt SDK.'
}
$manifest = Get-Content (Join-Path $repositoryRoot 'toolchain.json') -Raw | ConvertFrom-Json
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere.exe is required.' }
$baseline = $Toolchain -eq 'Baseline'
$versionRange = if ($baseline) { '[16.0,17.0)' } else { '[17.0,18.0)' }
$installation = & $vswhere -latest -products '*' -version $versionRange -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$installation) { throw "Visual Studio $versionRange C++ x64 build tools are required for $Toolchain." }
$arguments = '-arch=x64 -host_arch=x64'
if ($baseline) {
    $toolset = Join-Path $installation "VC/Tools/MSVC/$($manifest.windows.toolsetVersion)"
    $sdkInclude = Join-Path ${env:ProgramFiles(x86)} "Windows Kits/10/Include/$($manifest.windows.sdkVersion)/um/Windows.h"
    if (!(Test-Path -LiteralPath $toolset)) { throw "Missing declared MSVC toolset $($manifest.windows.toolsetVersion) at $installation." }
    if (!(Test-Path -LiteralPath $sdkInclude)) { throw "Missing declared Windows SDK $($manifest.windows.sdkVersion)." }
    $arguments += " -vcvars_ver=$($manifest.windows.toolsetVersion) -winsdk=$($manifest.windows.sdkVersion)"
}
Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments $arguments
if ($baseline) {
    if ($env:VCToolsVersion.TrimEnd('\') -ne $manifest.windows.toolsetVersion -or
        $env:WindowsSDKVersion.TrimEnd('\') -ne $manifest.windows.sdkVersion) {
        throw "Developer environment differs from toolchain.json: MSVC $env:VCToolsVersion; SDK $env:WindowsSDKVersion."
    }
}
$env:VSLANG = '1033'
$toolScript = Join-Path $PSScriptRoot 'toolchain.py'
$commonArguments = @()
if ($QtPrefix) { $commonArguments += @('--qt-prefix', $QtPrefix) }
if ($Preset.StartsWith('core-')) { $commonArguments += '--core-only' }
Push-Location $repositoryRoot
try {
    if ($InstallTools) {
        & $Python $toolScript install @commonArguments
        if ($LASTEXITCODE -ne 0) { throw "Tool acquisition failed: $LASTEXITCODE" }
    }
    $projectPython = Join-Path $repositoryRoot "$($manifest.cache.tools)/Scripts/python.exe"
    if (!(Test-Path -LiteralPath $projectPython)) { throw 'Project tools are missing; pass -InstallTools and -Python pointing to the declared Python version.' }
    $buildArguments = @('build', '--preset', $Preset) + $commonArguments
    if ($Fresh) { $buildArguments += '--fresh' }
    if (!$baseline) { $buildArguments += '--alternate-msvc' }
    & $projectPython $toolScript @buildArguments
    if ($LASTEXITCODE -ne 0) { throw "Build validation failed: $LASTEXITCODE" }
    if ($DesignAssets) {
        $previousTemp = $env:TEMP
        $previousTmp = $env:TMP
        try {
            $temporaryDirectory = Join-Path $repositoryRoot 'build/tmp'
            New-Item -ItemType Directory -Force -Path $temporaryDirectory | Out-Null
            $env:TEMP = $temporaryDirectory
            $env:TMP = $temporaryDirectory
            $cmakeProgram = Join-Path $repositoryRoot "$($manifest.cache.tools)/Scripts/cmake.exe"
            & $cmakeProgram --build "build/$Preset" --target xdstarter_render_svg --parallel 3
            if ($LASTEXITCODE -ne 0) { throw "Design renderer build failed: $LASTEXITCODE" }
            # Select the renderer from this preset, without writing a machine-local config.
            $previousRenderer = $env:XDSTARTER_SVG_RENDERER
            $previousQtRoot = $env:XDSTARTER_QT_ROOT
            try {
                $env:XDSTARTER_SVG_RENDERER = Join-Path $repositoryRoot "build/$Preset/bin/xdstarter_render_svg.exe"
                if ($QtPrefix) { $env:XDSTARTER_QT_ROOT = (Resolve-Path -LiteralPath $QtPrefix).Path }
                foreach ($source in @('build_desktop_grid.py', 'build_development_assets.py', 'verify_development_assets.py')) {
                    & $projectPython (Join-Path $repositoryRoot "docs/design/source/$source")
                    if ($LASTEXITCODE -ne 0) { throw "Design export/check failed: $source ($LASTEXITCODE)" }
                }
            } finally {
                $env:XDSTARTER_SVG_RENDERER = $previousRenderer
                $env:XDSTARTER_QT_ROOT = $previousQtRoot
            }
        } finally {
            $env:TEMP = $previousTemp
            $env:TMP = $previousTmp
        }
    }
} finally {
    Pop-Location
}
