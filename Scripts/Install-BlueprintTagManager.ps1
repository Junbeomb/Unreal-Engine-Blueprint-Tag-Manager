[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectFile,
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [string]$CompilerVersion,
    [string]$BuildOutputDirectory,
    [switch]$BuildFromSource,
    [switch]$Replace
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskSource = Join-Path $taskRoot 'BlueprintTagManager'
$ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).ProviderPath
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).ProviderPath
if ([System.IO.Path]::GetExtension($ProjectFile) -ne '.uproject' -or -not (Test-Path -LiteralPath $ProjectFile -PathType Leaf)) {
    throw 'ProjectFile must be an existing .uproject file. Close its Unreal Editor before installing.'
}
$taskVersionFile = Join-Path $EngineRoot 'Engine\Build\Build.version'
if (-not (Test-Path -LiteralPath $taskVersionFile -PathType Leaf)) { throw 'EngineRoot must point to an Unreal Engine installation.' }
$taskEngine = Get-Content -LiteralPath $taskVersionFile -Raw | ConvertFrom-Json
if ($taskEngine.MajorVersion -ne 5 -or $taskEngine.MinorVersion -lt 0 -or $taskEngine.MinorVersion -gt 8) {
    throw 'This installer targets Unreal Engine 5.0 through 5.8 on Windows x64.'
}
$taskEngineLabel = 'UE5.' + $taskEngine.MinorVersion
$taskSourceDescriptor = Get-Content -LiteralPath (Join-Path $taskSource 'BlueprintTagManager.uplugin') -Raw | ConvertFrom-Json
$taskProjectRoot = Split-Path -Parent $ProjectFile
$taskPluginsRoot = Join-Path $taskProjectRoot 'Plugins'
$taskDestination = Join-Path $taskPluginsRoot 'BlueprintTagManager'
if ((Test-Path -LiteralPath $taskDestination) -and -not $Replace) {
    throw 'BlueprintTagManager already exists in this project. Close the editor and use -Replace to back it up before upgrading.'
}

function Assert-ProjectPath([string]$Path) {
    $taskFullPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $taskFullPath.StartsWith($taskProjectRoot.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Installation path escaped the selected project.'
    }
    $taskAncestor = $taskFullPath
    while ($taskAncestor -and $taskAncestor -ne $taskProjectRoot) {
        if (Test-Path -LiteralPath $taskAncestor) {
            if ((Get-Item -LiteralPath $taskAncestor -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                throw 'Installation paths must not contain junctions or symbolic links.'
            }
        }
        $taskAncestor = Split-Path -Parent $taskAncestor
    }
}
Assert-ProjectPath $taskDestination
$taskProfile = Join-Path $taskRoot ('Prebuilt\' + $taskEngineLabel + '\BlueprintTagManager')
$taskUsePrebuilt = $false
if (-not $BuildFromSource -and (Test-Path -LiteralPath $taskProfile -PathType Container)) {
    $taskProfileDescriptor = Get-Content -LiteralPath (Join-Path $taskProfile 'BlueprintTagManager.uplugin') -Raw | ConvertFrom-Json
    $taskProfileModules = Get-Content -LiteralPath (Join-Path $taskProfile 'Binaries\Win64\UnrealEditor.modules') -Raw | ConvertFrom-Json
    $taskEngineModulesFile = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.modules'
    if ($taskProfileDescriptor.VersionName -ne $taskSourceDescriptor.VersionName -or
        $taskProfileDescriptor.EngineVersion -ne ('5.' + $taskEngine.MinorVersion + '.0') -or
        $taskProfileModules.Modules.BlueprintTagManager -ne 'UnrealEditor-BlueprintTagManager.dll' -or
        -not (Test-Path -LiteralPath (Join-Path $taskProfile 'Binaries\Win64\UnrealEditor-BlueprintTagManager.dll') -PathType Leaf)) {
        throw 'The prebuilt profile is incomplete or does not match this release.'
    }
    if (Test-Path -LiteralPath $taskEngineModulesFile -PathType Leaf) {
        $taskEngineModules = Get-Content -LiteralPath $taskEngineModulesFile -Raw | ConvertFrom-Json
        $taskUsePrebuilt = $taskProfileModules.BuildId -and $taskProfileModules.BuildId -eq $taskEngineModules.BuildId
    }
    if (-not $taskUsePrebuilt) { Write-Output 'The prebuilt Build ID differs from this engine; building source instead.' }
}

$taskInstallSource = $taskSource
if ($taskUsePrebuilt) {
    Write-Output ("Selected prebuilt profile: $taskEngineLabel (matching engine Build ID).")
}
else {
    Write-Output ("Building source for $taskEngineLabel. A compatible MSVC toolset and Windows SDK are required.")
    if (-not $BuildOutputDirectory) {
        $BuildOutputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('BTM-install-' + [Guid]::NewGuid().ToString('N').Substring(0, 10))
    }
    $taskBuildParameters = @{ EngineRoot = $EngineRoot; OutputDirectory = $BuildOutputDirectory }
    if ($CompilerVersion) { $taskBuildParameters.CompilerVersion = $CompilerVersion }
    & (Join-Path $PSScriptRoot 'Build-BlueprintTagManager.ps1') @taskBuildParameters
    $taskInstallSource = Join-Path $BuildOutputDirectory 'HostProject\Plugins\BlueprintTagManager'
}

# Assemble a complete plugin outside Plugins before touching an existing install.
$taskInstallId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$taskWorkRoot = Join-Path $taskProjectRoot ('Saved\BlueprintTagManager\Install-' + $taskInstallId)
$taskStagedPlugin = Join-Path $taskWorkRoot 'BlueprintTagManager'
Assert-ProjectPath $taskStagedPlugin
$taskLinks = @(Get-ChildItem -LiteralPath $taskInstallSource -Recurse -Force | Where-Object { $_.Attributes -band [System.IO.FileAttributes]::ReparsePoint })
if ($taskLinks.Count -gt 0) { throw 'Plugin source contains a junction or symbolic link.' }
New-Item -ItemType Directory -Path $taskStagedPlugin | Out-Null
foreach ($taskItem in @('Source', 'Config', 'BlueprintTagManager.uplugin', 'README.md', 'VALIDATION.md', 'LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $taskInstallSource $taskItem) -Destination $taskStagedPlugin -Recurse
}
$taskBinarySource = $taskInstallSource
if ($taskUsePrebuilt) {
    $taskBinarySource = $taskProfile
    Copy-Item -LiteralPath (Join-Path $taskProfile 'BlueprintTagManager.uplugin') -Destination $taskStagedPlugin -Force
}
$taskStagedBinaries = Join-Path $taskStagedPlugin 'Binaries\Win64'
New-Item -ItemType Directory -Path $taskStagedBinaries | Out-Null
foreach ($taskBinary in @('UnrealEditor-BlueprintTagManager.dll', 'UnrealEditor.modules')) {
    Copy-Item -LiteralPath (Join-Path $taskBinarySource ('Binaries\Win64\' + $taskBinary)) -Destination $taskStagedBinaries
}

$taskBackup = $null
Assert-ProjectPath $taskDestination
Assert-ProjectPath $taskStagedPlugin
New-Item -ItemType Directory -Path $taskPluginsRoot -Force | Out-Null
if (Test-Path -LiteralPath $taskDestination) {
    if (-not $Replace) { throw 'The destination appeared during installation; no existing files were changed.' }
    $taskBackup = Join-Path $taskProjectRoot ('Saved\BlueprintTagManager\Backups\' + $taskInstallId + '\BlueprintTagManager')
    Assert-ProjectPath $taskBackup
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskBackup) | Out-Null
    # Both resolved targets were checked to remain inside the selected project.
    Move-Item -LiteralPath $taskDestination -Destination $taskBackup
    Write-Output ("Previous installation backed up to: $taskBackup")
}
try { Move-Item -LiteralPath $taskStagedPlugin -Destination $taskDestination }
catch {
    if ($taskBackup -and -not (Test-Path -LiteralPath $taskDestination)) {
        Assert-ProjectPath $taskBackup
        Assert-ProjectPath $taskDestination
        Move-Item -LiteralPath $taskBackup -Destination $taskDestination
    }
    throw
}
Write-Output ("Installed Blueprint Tag Manager $($taskSourceDescriptor.VersionName) for $taskEngineLabel into: $taskDestination")
Write-Output 'Open the project with the selected engine, enable Blueprint Tag Manager in Edit > Plugins, and restart if requested.'
