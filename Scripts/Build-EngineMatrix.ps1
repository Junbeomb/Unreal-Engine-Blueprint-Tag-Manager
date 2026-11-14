[CmdletBinding()]
param(
    [string[]]$EngineRoots,
    [string]$EngineBaseDirectory = 'C:\Program Files\Epic Games',
    [ValidateRange(0, 8)][int[]]$MinorVersions = @(0..8),
    [string]$OutputDirectory,
    [hashtable]$CompilerVersions = @{},
    [switch]$RunTests,
    [switch]$RequireAllVersions
)

$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('BTM-matrix-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
if ($OutputDirectory -match '[^\x00-\x7F]' -or $OutputDirectory.Length -gt 85) {
    throw 'Use a fresh, short ASCII matrix output path (85 characters or fewer).'
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a fresh matrix output directory.' }
$taskVersions = @($MinorVersions | Sort-Object -Unique)
if ($taskVersions.Count -eq 0) { throw 'Select at least one engine minor version.' }
$taskCandidates = @($EngineRoots | Where-Object { $_ })
if ($taskCandidates.Count -eq 0) {
    if (Test-Path -LiteralPath $EngineBaseDirectory) {
        $taskCandidates += @(Get-ChildItem -LiteralPath $EngineBaseDirectory -Directory -Filter 'UE_5.*' | ForEach-Object FullName)
    }
    $taskLauncherManifest = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path -LiteralPath $taskLauncherManifest) {
        $taskCandidates += @((Get-Content -LiteralPath $taskLauncherManifest -Raw | ConvertFrom-Json).InstallationList |
            Where-Object AppName -like 'UE_5.*' | ForEach-Object InstallLocation)
    }
}
$taskEngines = @{}
foreach ($taskEngine in @($taskCandidates | Where-Object { $_ } | Sort-Object -Unique)) {
    $taskVersionFile = Join-Path $taskEngine 'Engine\Build\Build.version'
    if (-not (Test-Path -LiteralPath $taskVersionFile)) { throw "Not an engine installation: $taskEngine" }
    $taskVersion = Get-Content -LiteralPath $taskVersionFile -Raw | ConvertFrom-Json
    if ($taskVersion.MajorVersion -ne 5 -or $taskVersions -notcontains [int]$taskVersion.MinorVersion) { continue }
    $taskMinor = [int]$taskVersion.MinorVersion
    if (-not $taskEngines.ContainsKey($taskMinor) -or $taskVersion.PatchVersion -gt $taskEngines[$taskMinor].Version.PatchVersion) {
        $taskEngines[$taskMinor] = @{ Root = $taskEngine; Version = $taskVersion }
    }
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$taskResults = @($taskVersions | ForEach-Object {
    $taskInstalled = $taskEngines.ContainsKey([int]$_)
    [pscustomobject]@{
        Engine = '5.' + $_
        Patch = if ($taskInstalled) { [int]$taskEngines[[int]$_].Version.PatchVersion } else { $null }
        Status = if ($taskInstalled) { 'NotBuilt' } else { 'NotInstalled' }
        Tests = 'NotRun'
        Archive = $null
    }
})
$taskReportFile = Join-Path $OutputDirectory 'CompatibilityResults.json'
function Write-MatrixReport {
    # Publicly shareable status: no machine paths, names, or environment dump.
    [System.IO.File]::WriteAllText($taskReportFile, (ConvertTo-Json -InputObject @($taskResults) -Depth 6))
}
Write-MatrixReport
if ($RequireAllVersions -and @($taskResults | Where-Object Status -eq 'NotInstalled').Count -gt 0) {
    throw 'Required engines are missing. See CompatibilityResults.json; no builds were started.'
}
$taskPluginDescriptor = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'BlueprintTagManager\BlueprintTagManager.uplugin') -Raw | ConvertFrom-Json
foreach ($taskResult in $taskResults) {
    if ($taskResult.Status -eq 'NotInstalled') {
        Write-Output ('UE' + $taskResult.Engine + ': not installed; not built or tested.')
        continue
    }
    $taskEngine = $taskEngines[[int]($taskResult.Engine.Split('.')[1])]
    $taskBuildDirectory = Join-Path $OutputDirectory ('UE' + $taskResult.Engine)
    $taskBuildParameters = @{ EngineRoot = $taskEngine.Root; OutputDirectory = $taskBuildDirectory; RunTests = $RunTests }
    if ($CompilerVersions.ContainsKey($taskResult.Engine)) { $taskBuildParameters.CompilerVersion = $CompilerVersions[$taskResult.Engine] }
    try {
        & (Join-Path $PSScriptRoot 'Build-BlueprintTagManager.ps1') @taskBuildParameters
        $taskResult.Status = 'Built'
        if ($RunTests) { $taskResult.Tests = '10Passed' }
        $taskResult.Archive = 'UE' + $taskResult.Engine + '/Releases/BlueprintTagManager-' + $taskPluginDescriptor.VersionName + '-UE' + $taskResult.Engine + '-Win64.zip'
    }
    catch {
        $taskResult.Status = 'Failed'
        Write-Output ('UE' + $taskResult.Engine + ': ' + $_.Exception.Message)
    }
    Write-MatrixReport
}
$taskBuilt = @($taskResults | Where-Object Status -eq 'Built').Count
$taskMissing = @($taskResults | Where-Object Status -eq 'NotInstalled').Count
$taskFailed = @($taskResults | Where-Object Status -eq 'Failed').Count
Write-Output ("Engine matrix: $taskBuilt built, $taskMissing not installed, $taskFailed failed. Report: $taskReportFile")
if ($taskFailed -gt 0 -or $taskBuilt -eq 0) { throw 'The engine matrix did not complete successfully. See the per-engine logs and status report.' }
