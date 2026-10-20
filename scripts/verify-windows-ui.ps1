param(
    [Parameter(Mandatory = $true)][string]$QtPrefix,
    [ValidateSet('app-debug', 'app-release')][string]$Preset = 'app-release',
    [switch]$NativeScales
)

$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$qtRoot = (Resolve-Path -LiteralPath $QtPrefix).Path
$testProgram = Join-Path $workspaceRoot "build/$Preset/bin/xdstarter_ui_tests.exe"
if (!(Test-Path -LiteralPath $testProgram)) {
    throw "Build the $Preset application and tests first."
}
if (!(Test-Path -LiteralPath (Join-Path $qtRoot 'bin/Qt6Core.dll'))) {
    throw 'QtPrefix must point to the compatible Windows Qt Kit root.'
}

$outputRoot = Join-Path $workspaceRoot 'build/validation/ui'
$environmentNames = @(
    'PATH', 'TEMP', 'TMP', 'QT_QPA_PLATFORM', 'QT_QUICK_BACKEND', 'QT_FORCE_STDERR_LOGGING',
    'QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS', 'QT_SCALE_FACTOR_ROUNDING_POLICY',
    'QT_ENABLE_HIGHDPI_SCALING', 'QT_QPA_FONTDIR', 'XDSTARTER_VALIDATION_DIR'
)
$originalEnvironment = @{}
foreach ($name in $environmentNames) {
    $originalEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

function Invoke-UiCheck([string]$Directory, [double]$Factor, [switch]$RenderOnly) {
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    $env:XDSTARTER_VALIDATION_DIR = $Directory
    $env:QT_SCALE_FACTOR = $Factor.ToString('0.########', [Globalization.CultureInfo]::InvariantCulture)
    $testArguments = @('-o', ((Join-Path $Directory 'tests.log') + ',txt'))
    if ($RenderOnly) { $testArguments = @('renderedGroups') + $testArguments }
    & $testProgram @testArguments | Out-Host
    $testExitCode = $LASTEXITCODE
    Get-Content -LiteralPath (Join-Path $Directory 'tests.log') -Tail 6 | Out-Host
    if ($testExitCode -ne 0) { throw "UI checks failed: $Directory" }
    $evidencePaths = @(Get-ChildItem -LiteralPath $Directory -Filter '*.json' -File)
    if ($evidencePaths.Count -ne 6) { throw 'Expected dark/light captures for all three groups.' }
    $evidence = @($evidencePaths | ForEach-Object { Get-Content -Raw -LiteralPath $_.FullName | ConvertFrom-Json })
    foreach ($entry in $evidence) {
        if (!$entry.pixelScanPassed) { throw 'Rendered pixel geometry failed.' }
        if ($NativeScales -and $entry.platformName -ne 'windows') { throw 'Expected native Windows QPA.' }
    }
    return ,$evidence
}

try {
    # Keep temporary layout fixtures in this writable workspace, including managed desktops.
    $temporaryRoot = Join-Path $outputRoot 'temp'
    New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
    $env:TEMP = $temporaryRoot
    $env:TMP = $temporaryRoot
    $env:PATH = (Join-Path $qtRoot 'bin') + [IO.Path]::PathSeparator + $env:PATH
    $env:QT_FORCE_STDERR_LOGGING = '1'
    if ($env:WINDIR) { $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts' }
    Remove-Item Env:QT_SCALE_FACTOR_ROUNDING_POLICY -ErrorAction SilentlyContinue
    Remove-Item Env:QT_SCREEN_SCALE_FACTORS -ErrorAction SilentlyContinue
    Remove-Item Env:QT_ENABLE_HIGHDPI_SCALING -ErrorAction SilentlyContinue
    if ($NativeScales) {
        $env:QT_QPA_PLATFORM = 'windows'
        Remove-Item Env:QT_QUICK_BACKEND -ErrorAction SilentlyContinue
        # Calibrate against the monitor's effective DPR; QT_SCALE_FACTOR multiplies it.
        $probe = Invoke-UiCheck (Join-Path $outputRoot 'native-probe') 1 -RenderOnly
        $baseDpr = [double]$probe[0].windowDevicePixelRatio
        if ($baseDpr -le 0) { throw 'Invalid native device pixel ratio.' }
        $records = @()
        foreach ($scale in @(1.0, 1.25, 1.5, 1.75, 2.0)) {
            $label = [int][Math]::Round(100 * $scale)
            $directory = Join-Path $outputRoot "native-$label"
            $evidence = Invoke-UiCheck $directory ($scale / $baseDpr)
            foreach ($entry in $evidence) {
                if ([Math]::Abs([double]$entry.windowDevicePixelRatio - $scale) -gt 0.01) {
                    throw "Requested $scale DPR but obtained $($entry.windowDevicePixelRatio)."
                }
            }
            $records += [ordered]@{
                percent = $label
                effectiveDpr = $evidence[0].windowDevicePixelRatio
                graphicsApi = $evidence[0].graphicsApi
                directory = "native-$label"
                captures = $evidence.Count
            }
        }
        [ordered]@{
            schemaVersion = 1
            mode = 'Windows QPA with Qt scaling; not Windows display-setting or mixed-monitor validation'
            nativeBaseDpr = $baseDpr
            executableSha256 = (Get-FileHash -LiteralPath $testProgram -Algorithm SHA256).Hash.ToLower()
            scales = $records
        } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'native-matrix.json') -Encoding utf8
    } else {
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QT_QUICK_BACKEND = 'software'
        $null = Invoke-UiCheck (Join-Path $outputRoot 'offscreen') 1
    }
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $originalEnvironment[$name], 'Process')
    }
}
