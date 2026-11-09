[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuiltPluginDirectory,
    [Parameter(Mandatory = $true)][string]$OutputZip,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$BuiltPluginDirectory = (Resolve-Path -LiteralPath $BuiltPluginDirectory).ProviderPath
$OutputZip = [System.IO.Path]::GetFullPath($OutputZip)
if ((Test-Path -LiteralPath $OutputZip) -and -not $Force) { throw "Output already exists: $OutputZip. Use a new path or -Force." }
$taskStage = Join-Path ([System.IO.Path]::GetTempPath()) ('BTM-release-' + [Guid]::NewGuid().ToString('N'))
$taskPluginStage = Join-Path $taskStage 'BlueprintTagManager'
New-Item -ItemType Directory -Path $taskPluginStage | Out-Null

# Explicit allowlist: do not package the HostProject, PDBs, logs, or build intermediates.
Copy-Item -LiteralPath (Join-Path $BuiltPluginDirectory 'BlueprintTagManager.uplugin') -Destination $taskPluginStage
foreach ($taskFolder in @('Source', 'Config')) {
    $taskSourceFolder = Join-Path $BuiltPluginDirectory $taskFolder
    $taskLinks = @(Get-ChildItem -LiteralPath $taskSourceFolder -Recurse -Force | Where-Object { $_.Attributes -band [System.IO.FileAttributes]::ReparsePoint })
    if ($taskLinks.Count -gt 0) { throw 'Release source contains a reparse point.' }
    Copy-Item -LiteralPath $taskSourceFolder -Destination $taskPluginStage -Recurse
}
$taskBinaryStage = Join-Path $taskPluginStage 'Binaries\Win64'
New-Item -ItemType Directory -Path $taskBinaryStage | Out-Null
foreach ($taskBinary in @('UnrealEditor-BlueprintTagManager.dll', 'UnrealEditor.modules')) {
    Copy-Item -LiteralPath (Join-Path $BuiltPluginDirectory ('Binaries\Win64\' + $taskBinary)) -Destination $taskBinaryStage
}
foreach ($taskDoc in @('README.ko.md', 'VALIDATION.ko.md', 'LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot ('BlueprintTagManager\' + $taskDoc)) -Destination $taskPluginStage
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'README.md'), (Join-Path $taskRoot 'LICENSE') -Destination $taskStage
$taskScriptStage = Join-Path $taskStage 'Scripts'
New-Item -ItemType Directory -Path $taskScriptStage | Out-Null
Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' -File | Copy-Item -Destination $taskScriptStage

$taskOutputParent = Split-Path -Parent $OutputZip
New-Item -ItemType Directory -Path $taskOutputParent -Force | Out-Null
$taskTemporaryZip = Join-Path $taskOutputParent ([Guid]::NewGuid().ToString('N') + '.zip')
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
# Legacy .NET on Windows can put backslashes into ZIP entry names. Write
# canonical forward-slash names explicitly for standard ZIP tools.
$taskArchive = [System.IO.Compression.ZipFile]::Open($taskTemporaryZip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($taskFile in (Get-ChildItem -LiteralPath $taskStage -Recurse -File)) {
        $taskEntryName = $taskFile.FullName.Substring($taskStage.Length + 1).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($taskArchive, $taskFile.FullName, $taskEntryName, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
}
finally { $taskArchive.Dispose() }
try {
    & (Join-Path $PSScriptRoot 'Test-PublicRelease.ps1') -RepositoryRoot $taskRoot -ArchivePath $taskTemporaryZip
    Move-Item -LiteralPath $taskTemporaryZip -Destination $OutputZip -Force:$Force
}
catch {
    # This path is the newly generated GUID ZIP, never the requested output.
    if (Test-Path -LiteralPath $taskTemporaryZip) { Remove-Item -LiteralPath $taskTemporaryZip -Force }
    throw
}
Write-Output "Public release ZIP: $OutputZip"
Write-Output ('SHA256: ' + (Get-FileHash -LiteralPath $OutputZip -Algorithm SHA256).Hash)
