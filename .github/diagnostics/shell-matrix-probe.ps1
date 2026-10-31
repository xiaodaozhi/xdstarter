$ErrorActionPreference = 'Stop'
if (!$IsWindows -or $env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'This probe runs only on a GitHub-hosted Windows runner.'
}
$workspace = (Resolve-Path -LiteralPath $env:GITHUB_WORKSPACE).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (!$visualStudio) { throw 'MSVC is unavailable on this runner.' }
Import-Module (Join-Path $visualStudio 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $visualStudio -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'

$fixtureDir = Join-Path $workspace 'build/app-release'
$history = Join-Path $fixtureDir 'shell-localization-history'
New-Item -ItemType Directory -Path $history -Force | Out-Null
$helper = Join-Path $fixtureDir 'xdstarter_launch_fixture.exe'
$main = Join-Path $fixtureDir 'minimal-main.c'
$resource = Join-Path $fixtureDir 'fixture.res'
[IO.File]::WriteAllText($main, 'int main(void) { return 0; }')
& rc.exe /nologo "/fo$resource" (Join-Path $workspace 'tests/platform/application_catalog_fixture.rc')
if ($LASTEXITCODE -ne 0) { throw 'Cannot compile fixture resource.' }
Push-Location $fixtureDir
try {
    & cl.exe /nologo "/Fe$helper" $main $resource /link /INCREMENTAL:NO
    if ($LASTEXITCODE -ne 0) { throw 'Cannot link minimal fixture module.' }
} finally { Pop-Location }

$output = Join-Path $history 'cross-matrix.json'
try {
    & (Join-Path $workspace 'scripts/diagnose_shell_matrix.ps1') -Helper $helper -Output $output
    & python (Join-Path $workspace 'scripts/verify_shell_matrix.py') $output
    if ($LASTEXITCODE -ne 0) { throw 'Shell matrix validation failed.' }
} finally {
    if (Test-Path -LiteralPath $output) {
        $report = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
        @{ status = $report.status; cleanupComplete = $report.cleanupComplete; errors = $report.errors;
           cases = @($report.aclCases | ForEach-Object {
               @{ target = $_.targetLocation; source = $_.templateLocation; name = $_.shellDisplayName;
                  folderMatches = ($_.folderAccessSha256 -eq $_.folderTemplateSha256);
                  iniMatches = ($_.iniAccessSha256 -eq $_.iniTemplateSha256);
                  folderTemplate = $_.folderTemplateSha256; iniTemplate = $_.iniTemplateSha256 }
           }) } | ConvertTo-Json -Depth 8 -Compress | Write-Output
    }
}
