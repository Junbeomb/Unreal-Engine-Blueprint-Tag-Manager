[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [string]$OutputDirectory,
    [string]$CompilerVersion,
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$pluginFile = Join-Path $taskRoot 'BlueprintTagManager\BlueprintTagManager.uplugin'
$taskEngineVersionFile = Join-Path $EngineRoot 'Engine\Build\Build.version'
if (-not (Test-Path -LiteralPath $taskEngineVersionFile)) { throw 'EngineRoot must point to an Unreal Engine installation.' }
$taskEngineVersion = Get-Content -LiteralPath $taskEngineVersionFile -Raw | ConvertFrom-Json
if ($taskEngineVersion.MajorVersion -ne 5 -or $taskEngineVersion.MinorVersion -lt 0 -or $taskEngineVersion.MinorVersion -gt 8) {
    throw 'This release targets Unreal Engine 5.0 through 5.8.'
}
$taskEngineLabel = 'UE5.' + $taskEngineVersion.MinorVersion
$taskEngineFullVersion = '5.' + $taskEngineVersion.MinorVersion + '.' + $taskEngineVersion.PatchVersion
$taskBuildTool = Join-Path $EngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll'
if (-not (Test-Path -LiteralPath $taskBuildTool)) { throw "UnrealBuildTool not found: $taskBuildTool" }
$taskDotnetRoot = Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\DotNet'
$taskDotnetFolders = @($taskDotnetRoot) + @(Get-ChildItem -LiteralPath $taskDotnetRoot -Directory | Sort-Object Name -Descending | ForEach-Object FullName)
$taskDotnet = @($taskDotnetFolders | ForEach-Object {
    foreach ($taskRuntime in @('win-x64\dotnet.exe', 'windows\dotnet.exe', 'dotnet.exe')) {
        $taskCandidate = Join-Path $_ $taskRuntime
        if (Test-Path -LiteralPath $taskCandidate) { $taskCandidate }
    }
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
# Use the engine's minor baseline: Launcher compatibility checks can report
# 5.x.0 even when Build.version records a later hotfix such as 5.x.4.
$taskBuiltDescriptor = Get-Content -LiteralPath $taskHostPluginFile -Raw | ConvertFrom-Json
$taskBuiltDescriptor | Add-Member -NotePropertyName EngineVersion -NotePropertyValue ('5.' + $taskEngineVersion.MinorVersion + '.0') -Force
[System.IO.File]::WriteAllText($taskHostPluginFile, ($taskBuiltDescriptor | ConvertTo-Json -Depth 12))
$taskBuildLog = Join-Path $OutputDirectory 'Build.log'

# UBT clears CL/_CL_ environment variables. Pass the privacy options through
# its explicit compiler argument property instead. Disable UBA path rewriting.
$taskHostPrefix = ($OutputDirectory.TrimEnd('\') + '\HostProject\').Replace('\', '/')
$taskEnginePrefix = ($EngineRoot.TrimEnd('\') + '\').Replace('\', '/')
$taskCompilerFlags = '"/d1trimfile:' + $taskHostPrefix + '" "/d1trimfile:' + $taskEnginePrefix + '"'
$taskArguments = @($taskBuildTool, 'UnrealEditor', 'Win64', 'Development', "-Project=$hostFile", "-plugin=$taskHostPluginFile",
    '-NoHotReload', '-OverrideBuildEnvironment',
    "-CompilerArguments=$taskCompilerFlags", "-log=$taskBuildLog")
if ($taskEngineVersion.MinorVersion -ge 4) { $taskArguments += '-NoUBA' }
if ($taskEngineVersion.MinorVersion -ge 4 -and $taskEngineVersion.MinorVersion -le 6) { $taskArguments += '-NoUBALocal' }
if ($CompilerVersion) {
    if ($CompilerVersion -notmatch '^\d+\.\d+(?:\.\d+)?$') { throw 'CompilerVersion must be an MSVC version, such as 14.38.33130.' }
    $taskArguments += "-CompilerVersion=$CompilerVersion"
}

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
Write-Output ('Building Blueprint Tag Manager for ' + $taskEngineFullVersion + '; PCH and Unity are disabled for the plugin module.')
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
$taskZip = Join-Path $OutputDirectory ('Releases\BlueprintTagManager-' + $taskVersion + '-' + $taskEngineLabel + '-Win64.zip')
& (Join-Path $PSScriptRoot 'New-PublicRelease.ps1') -BuiltPluginDirectory $taskHostPlugin -OutputZip $taskZip
Write-Output "Built plugin: $taskHostPlugin"
Write-Output 'Copy BlueprintTagManager.uplugin, Binaries, Source and Config into your project Plugins/BlueprintTagManager folder.'
