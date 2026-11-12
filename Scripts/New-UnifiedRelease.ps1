[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string[]]$BuiltPluginDirectories,
    [Parameter(Mandatory = $true)][string]$OutputZip,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskSource = Join-Path $taskRoot 'BlueprintTagManager'
$taskSourceDescriptor = Get-Content -LiteralPath (Join-Path $taskSource 'BlueprintTagManager.uplugin') -Raw | ConvertFrom-Json
if ($taskSourceDescriptor.PSObject.Properties['EngineVersion']) { throw 'The common source descriptor must not pin EngineVersion.' }
$OutputZip = [System.IO.Path]::GetFullPath($OutputZip)
if ((Test-Path -LiteralPath $OutputZip) -and -not $Force) { throw 'Output exists. Choose a new path or use -Force.' }
$taskStage = Join-Path ([System.IO.Path]::GetTempPath()) ('BTM-unified-' + [Guid]::NewGuid().ToString('N'))
$taskPluginStage = Join-Path $taskStage 'BlueprintTagManager'
New-Item -ItemType Directory -Path $taskPluginStage | Out-Null
foreach ($taskItem in @('Source', 'Config', 'BlueprintTagManager.uplugin', 'README.md', 'VALIDATION.md', 'LICENSE')) {
    $taskItemPath = Join-Path $taskSource $taskItem
    $taskLinks = @(Get-Item -LiteralPath $taskItemPath -Force) + @(Get-ChildItem -LiteralPath $taskItemPath -Recurse -Force)
    if (@($taskLinks | Where-Object { $_.Attributes -band [System.IO.FileAttributes]::ReparsePoint }).Count -gt 0) { throw 'Release source contains a reparse point.' }
    Copy-Item -LiteralPath $taskItemPath -Destination $taskPluginStage -Recurse
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'README.md'), (Join-Path $taskRoot 'LICENSE') -Destination $taskStage
$taskScriptStage = Join-Path $taskStage 'Scripts'
New-Item -ItemType Directory -Path $taskScriptStage | Out-Null
Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' -File | Copy-Item -Destination $taskScriptStage

$taskProfiles = @{}
foreach ($taskBuiltPath in $BuiltPluginDirectories) {
    $taskBuilt = (Resolve-Path -LiteralPath $taskBuiltPath).ProviderPath
    $taskDescriptorFile = Join-Path $taskBuilt 'BlueprintTagManager.uplugin'
    $taskDescriptor = Get-Content -LiteralPath $taskDescriptorFile -Raw | ConvertFrom-Json
    if ($taskDescriptor.Version -ne $taskSourceDescriptor.Version -or $taskDescriptor.VersionName -ne $taskSourceDescriptor.VersionName -or $taskDescriptor.EngineVersion -notmatch '^5\.([0-8])\.0$') {
        throw 'A prebuilt descriptor does not match this release or engine range.'
    }
    $taskLabel = 'UE5.' + $Matches[1]
    if ($taskProfiles.ContainsKey($taskLabel)) { throw "Duplicate profile: $taskLabel" }
    # Do not combine binaries with source different from the code they were built from.
    foreach ($taskFolder in @('Source', 'Config')) {
        $taskCommonFolder = Join-Path $taskSource $taskFolder
        $taskBuiltFolder = Join-Path $taskBuilt $taskFolder
        $taskCommonFiles = @(Get-ChildItem -LiteralPath $taskCommonFolder -File -Recurse)
        if (@(Get-ChildItem -LiteralPath $taskBuiltFolder -File -Recurse).Count -ne $taskCommonFiles.Count) { throw 'Prebuilt source file count differs from common source.' }
        foreach ($taskFile in $taskCommonFiles) {
            $taskRelative = $taskFile.FullName.Substring($taskCommonFolder.Length + 1)
            $taskBuiltFile = Join-Path $taskBuiltFolder $taskRelative
            if (-not (Test-Path -LiteralPath $taskBuiltFile -PathType Leaf) -or
                (Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $taskBuiltFile -Algorithm SHA256).Hash) {
                throw "Prebuilt source differs: $taskLabel/$taskFolder/$taskRelative"
            }
        }
    }
    $taskModulesFile = Join-Path $taskBuilt 'Binaries\Win64\UnrealEditor.modules'
    $taskModules = Get-Content -LiteralPath $taskModulesFile -Raw | ConvertFrom-Json
    if (-not $taskModules.BuildId -or $taskModules.Modules.BlueprintTagManager -ne 'UnrealEditor-BlueprintTagManager.dll') { throw 'Invalid prebuilt module manifest.' }
    $taskProfileStage = Join-Path $taskStage ('Prebuilt\' + $taskLabel + '\BlueprintTagManager')
    $taskBinaryStage = Join-Path $taskProfileStage 'Binaries\Win64'
    New-Item -ItemType Directory -Path $taskBinaryStage | Out-Null
    Copy-Item -LiteralPath $taskDescriptorFile -Destination $taskProfileStage
    foreach ($taskBinary in @('UnrealEditor-BlueprintTagManager.dll', 'UnrealEditor.modules')) {
        Copy-Item -LiteralPath (Join-Path $taskBuilt ('Binaries\Win64\' + $taskBinary)) -Destination $taskBinaryStage
    }
    $taskProfiles[$taskLabel] = $true
}

New-Item -ItemType Directory -Path (Split-Path -Parent $OutputZip) -Force | Out-Null
$taskTemporaryZip = Join-Path (Split-Path -Parent $OutputZip) ([Guid]::NewGuid().ToString('N') + '.zip')
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskArchive = [System.IO.Compression.ZipFile]::Open($taskTemporaryZip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($taskFile in (Get-ChildItem -LiteralPath $taskStage -File -Recurse)) {
        $taskEntry = $taskFile.FullName.Substring($taskStage.Length + 1).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($taskArchive, $taskFile.FullName, $taskEntry, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
}
finally { $taskArchive.Dispose() }
try {
    & (Join-Path $PSScriptRoot 'Test-PublicRelease.ps1') -RepositoryRoot $taskRoot -ArchivePath $taskTemporaryZip
    Move-Item -LiteralPath $taskTemporaryZip -Destination $OutputZip -Force:$Force
}
catch {
    if (Test-Path -LiteralPath $taskTemporaryZip) { Remove-Item -LiteralPath $taskTemporaryZip -Force }
    throw
}
$taskHash = (Get-FileHash -LiteralPath $OutputZip -Algorithm SHA256).Hash
[System.IO.File]::WriteAllText($OutputZip + '.sha256', ($taskHash + '  ' + [System.IO.Path]::GetFileName($OutputZip) + "`n"))
Write-Output ('Prebuilt profiles: ' + (($taskProfiles.Keys | Sort-Object) -join ', '))
Write-Output ("Unified release ZIP: $OutputZip")
Write-Output ("SHA256: $taskHash")
