[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.7',
    [string]$OutputDirectory,
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$pluginFile = Join-Path $taskRoot 'BlueprintTagManager\BlueprintTagManager.uplugin'
$taskBuildTool = Join-Path $EngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll'
if (-not (Test-Path -LiteralPath $taskBuildTool)) { throw "UnrealBuildTool not found: $taskBuildTool" }
$taskDotnet = @(Get-ChildItem -LiteralPath (Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\DotNet') -Directory | Sort-Object Name -Descending | ForEach-Object {
    $taskCandidate = Join-Path $_.FullName 'win-x64\dotnet.exe'
    if (Test-Path -LiteralPath $taskCandidate) { $taskCandidate }
}) | Select-Object -First 1
if (-not $taskDotnet) { throw 'The engine bundled Windows x64 .NET runtime was not found.' }
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('BTM-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
if ($OutputDirectory -match '[^\x00-\x7F]' -or $OutputDirectory.Length -gt 100) {
    throw 'Use a short ASCII output path (100 characters or fewer), such as C:\UEBuild\BTM. UE / MSVC can fail with long or non-ASCII build paths.'
}
# Require a fresh destination; never clear an existing user's directory.
if (Test-Path -LiteralPath $OutputDirectory) { throw "Choose a new output directory: $OutputDirectory already exists." }

$taskHost = Join-Path $OutputDirectory 'HostProject'
$taskHostPlugin = Join-Path $taskHost 'Plugins\BlueprintTagManager'
New-Item -ItemType Directory -Path $taskHostPlugin | Out-Null
Copy-Item -LiteralPath $pluginFile -Destination $taskHostPlugin
foreach ($taskSourceDirectory in @('Source', 'Config')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot ('BlueprintTagManager\' + $taskSourceDirectory)) -Destination $taskHostPlugin -Recurse
}
foreach ($taskDoc in @('README.md', 'VALIDATION.md', 'LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot ('BlueprintTagManager\' + $taskDoc)) -Destination $taskHostPlugin
}
$hostFile = Join-Path $taskHost 'HostProject.uproject'
Set-Content -LiteralPath $hostFile -Encoding Ascii -Value '{ "FileVersion": 3, "Plugins": [ { "Name": "BlueprintTagManager", "Enabled": true } ] }'
$taskHostPluginFile = Join-Path $taskHostPlugin 'BlueprintTagManager.uplugin'
$taskBuildLog = Join-Path $OutputDirectory 'Build.log'

# UBT clears CL/_CL_ environment variables. Pass the privacy options through
# its explicit compiler argument property instead. Disable UBA path rewriting.
$taskHostPrefix = ($OutputDirectory.TrimEnd('\') + '\HostProject\').Replace('\', '/')
$taskEnginePrefix = ($EngineRoot.TrimEnd('\') + '\').Replace('\', '/')
$taskCompilerFlags = '"/d1trimfile:' + $taskHostPrefix + '" "/d1trimfile:' + $taskEnginePrefix + '"'
$taskArguments = @($taskBuildTool, 'UnrealEditor', 'Win64', 'Development', "-Project=$hostFile", "-plugin=$taskHostPluginFile",
    '-NoHotReload', '-NoPCH', '-NoSharedPCH', '-DisableUnity', '-NoUBA', '-OverrideBuildEnvironment',
    "-CompilerArguments=$taskCompilerFlags", "-log=$taskBuildLog")

# Preserve nested quotes and paths with spaces under both Windows PowerShell
# 5.1 and PowerShell 7. No command shell or command-string evaluation is used.
function ConvertTo-WindowsArgument([string]$Value) {
    $taskEscaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $taskEscaped = [regex]::Replace($taskEscaped, '(\\+)$', '$1$1')
    return '"' + $taskEscaped + '"'
}
$taskStartInfo = New-Object System.Diagnostics.ProcessStartInfo
$taskStartInfo.FileName = $taskDotnet
$taskStartInfo.Arguments = ($taskArguments | ForEach-Object { ConvertTo-WindowsArgument $_ }) -join ' '
$taskStartInfo.WorkingDirectory = Join-Path $EngineRoot 'Engine\Source'
$taskStartInfo.UseShellExecute = $false
$taskStartInfo.CreateNoWindow = $true
$taskStartInfo.RedirectStandardOutput = $true
$taskStartInfo.RedirectStandardError = $true
$taskProcess = [System.Diagnostics.Process]::Start($taskStartInfo)
$taskStdout = $taskProcess.StandardOutput.ReadToEndAsync()
$taskStderr = $taskProcess.StandardError.ReadToEndAsync()
$taskProcess.WaitForExit()
Write-Output $taskStdout.Result
if ($taskStderr.Result) { Write-Output $taskStderr.Result }
if ($taskProcess.ExitCode -ne 0) { throw "Plugin build failed with exit code $($taskProcess.ExitCode)" }
$taskProcess.Dispose()

if ($RunTests) {
    $editorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $reportDir = Join-Path $OutputDirectory 'TestResults'
    $logFile = Join-Path $OutputDirectory 'AutomationTests.log'
    & $editorCmd $hostFile -unattended -nop4 -nosplash -NullRHI -nosound -RenderOffscreen '-ExecCmds=Automation RunTests BlueprintTagManager' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$reportDir" "-abslog=$logFile"
    if ($LASTEXITCODE -ne 0) { throw "Automation tests failed with exit code $LASTEXITCODE. See $logFile" }
    $reportFile = Join-Path $reportDir 'index.json'
    if (-not (Test-Path -LiteralPath $reportFile)) { throw 'Automation did not export a test report.' }
    $report = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
    if ($report.failed -gt 0 -or $report.succeededWithWarnings -gt 0 -or $report.notRun -gt 0 -or $report.succeeded -ne 8) { throw "Tests incomplete or failed. See $reportFile" }
}

$taskVersion = (Get-Content -LiteralPath $pluginFile -Raw | ConvertFrom-Json).VersionName
$taskZip = Join-Path $OutputDirectory ('Releases\BlueprintTagManager-' + $taskVersion + '-UE5.7-Win64.zip')
& (Join-Path $PSScriptRoot 'New-PublicRelease.ps1') -BuiltPluginDirectory $taskHostPlugin -OutputZip $taskZip
Write-Output "Built plugin: $taskHostPlugin"
Write-Output 'Copy BlueprintTagManager.uplugin, Binaries, Source and Config into your project Plugins/BlueprintTagManager folder.'
