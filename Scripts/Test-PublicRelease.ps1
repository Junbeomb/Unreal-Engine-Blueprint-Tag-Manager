[CmdletBinding()]
param(
    [string]$RepositoryRoot,
    [string]$ArchivePath,
    [switch]$CheckGitIdentity
)

$ErrorActionPreference = 'Stop'
if (-not $RepositoryRoot) { $RepositoryRoot = Split-Path -Parent $PSScriptRoot }
$RepositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot).ProviderPath.TrimEnd('\')
$taskIssues = New-Object 'System.Collections.Generic.List[string]'
$taskChecked = 0
$taskGitArguments = @('-c', ('safe.directory=' + $RepositoryRoot.Replace('\', '/')), '-C', $RepositoryRoot)
$taskHasGit = $null -ne (Get-Command git -ErrorAction SilentlyContinue)
$taskIsRepo = $false
$taskPrivateEmail = ''
if ($taskHasGit -and (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git'))) {
    $taskInside = & git @taskGitArguments rev-parse --is-inside-work-tree 2>$null
    $taskIsRepo = $LASTEXITCODE -eq 0 -and $taskInside -eq 'true'
    if ($taskIsRepo) {
        $taskPrivateEmail = [string](& git @taskGitArguments config --get user.email 2>$null)
        if ($taskPrivateEmail -match '@users\.noreply\.github\.com$') { $taskPrivateEmail = '' }
    }
}

function Test-PublicFileBytes {
    param([string]$Label, [byte[]]$Bytes, [bool]$IsBinary = $false)
    $script:taskChecked++
    $taskText = [System.Text.Encoding]::UTF8.GetString($Bytes)
    $taskWide = [System.Text.Encoding]::Unicode.GetString($Bytes)
    $taskRules = [ordered]@{
        'private user-profile path' = '(?i)[A-Z]:[\\/]Users[\\/]|/(?:Users|home)/[^/\s]+/'
        'private key' = '-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'
        'GitHub token' = '(?:gh[pousr]_|github_pat_)[A-Za-z0-9_]{20,}'
        'AWS access key' = '(?:AKIA|ASIA)[A-Z0-9]{16}'
        'OpenAI token' = 'sk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{32,}'
        'Slack token' = 'xox[baprs]-[A-Za-z0-9-]{20,}'
        'credential in URL' = 'https?://[^\s/:@]+:[^\s/@]+@'
    }
    foreach ($taskRule in $taskRules.GetEnumerator()) {
        if ($taskText -match $taskRule.Value -or $taskWide -match $taskRule.Value) {
            $script:taskIssues.Add("${Label}: $($taskRule.Key)")
        }
    }
    if ($script:taskPrivateEmail -and ($taskText.Contains($script:taskPrivateEmail) -or $taskWide.Contains($script:taskPrivateEmail))) {
        $script:taskIssues.Add("${Label}: configured non-noreply Git email appears in file content")
    }
    if ($IsBinary -and ($taskText -match '(?i)(?<![A-Za-z0-9_])[A-Z]:[\\/]' -or $taskWide -match '(?i)(?<![A-Za-z0-9_])[A-Z]:[\\/]')) {
        $script:taskIssues.Add("${Label}: absolute Windows path embedded in binary")
    }
}

$taskBlockedPath = '(?i)(^|/)(Build|Dist|Binaries|Intermediate|Saved|DerivedDataCache|\.git|\.vs|\.vscode|\.idea)(/|$)|(^|/)\.env($|\.)|\.(pdb|obj|lib|exp|exe|dll|log|binlog|dmp|pem|key|pfx|p12|zip)$'
if ($taskIsRepo) {
    $taskRawPaths = @(& git @taskGitArguments ls-files --cached --others --exclude-standard -z)
    if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate Git-visible files.' }
    $taskPaths = @(($taskRawPaths -join "`n") -split "`0" | Where-Object { $_ })
}
else {
    # A source download has no .git directory. Only inspect the distributed source layout.
    $taskPaths = @(Get-ChildItem -LiteralPath $RepositoryRoot -File -Recurse -Force | ForEach-Object {
        $_.FullName.Substring($RepositoryRoot.Length + 1).Replace('\', '/')
    } | Where-Object {
        $_ -match '^(README\.md|LICENSE|\.gitignore|\.gitattributes|Scripts/[^/]+\.ps1|BlueprintTagManager/(BlueprintTagManager\.uplugin|README\.md|VALIDATION\.md|LICENSE|Source/.+|Config/.+)|Prebuilt/UE5\.[0-8]/BlueprintTagManager/(BlueprintTagManager\.uplugin|Binaries/Win64/(UnrealEditor-BlueprintTagManager\.dll|UnrealEditor\.modules)))$'
    })
}
foreach ($taskRelativePath in ($taskPaths | Sort-Object -Unique)) {
    $taskNormalized = $taskRelativePath.Replace('\', '/')
    $taskDownloadBinary = -not $taskIsRepo -and $taskNormalized -match '^Prebuilt/UE5\.[0-8]/BlueprintTagManager/Binaries/Win64/(UnrealEditor-BlueprintTagManager\.dll|UnrealEditor\.modules)$'
    if ($taskNormalized -match $taskBlockedPath -and -not $taskDownloadBinary) { $taskIssues.Add("${taskNormalized}: generated or private file is Git-visible") }
    $taskFullPath = Join-Path $RepositoryRoot $taskRelativePath
    if (-not (Test-Path -LiteralPath $taskFullPath -PathType Leaf)) { continue }
    Test-PublicFileBytes $taskNormalized ([System.IO.File]::ReadAllBytes($taskFullPath)) ($taskNormalized -match '\.(dll|exe|pdb|obj|lib)$')
}

if ($CheckGitIdentity) {
    if (-not $taskIsRepo) { $taskIssues.Add('Git identity check requires the actual Git repository.') }
    else {
        $taskName = [string](& git @taskGitArguments config --get user.name 2>$null)
        $taskEmail = [string](& git @taskGitArguments config --get user.email 2>$null)
        if (-not $taskName.Trim()) { $taskIssues.Add('Configure a public Git author name before committing.') }
        if ($taskEmail -notmatch '^[^\s@]+@users\.noreply\.github\.com$') {
            $taskIssues.Add('Git commit email is not a GitHub noreply address. Configure a repository-local public identity before committing.')
        }
        # Report only counts; do not print personal names or emails.
        $taskRefs = @(& git @taskGitArguments for-each-ref '--format=%(refname)')
        $taskHistory = @()
        if ($taskRefs.Count -gt 0) { $taskHistory = @(& git @taskGitArguments log --all '--format=%ae%n%ce') }
        $taskOldEmails = @($taskHistory | Where-Object { $_ -match '@' -and $_ -notmatch '@users\.noreply\.github\.com$' } | Sort-Object -Unique)
        if ($taskOldEmails.Count -gt 0) { $taskIssues.Add("Git history contains $($taskOldEmails.Count) non-noreply email identity/identities.") }
    }
}

if ($ArchivePath) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $taskArchive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ArchivePath).ProviderPath)
    try {
        foreach ($taskEntry in $taskArchive.Entries) {
            $taskEntryName = $taskEntry.FullName.Replace('\', '/')
            if ($taskEntryName.EndsWith('/')) { continue }
            if ($taskEntryName -match '(^/|^[A-Za-z]:|(^|/)\.\.(/|$))') { $taskIssues.Add('Archive contains an unsafe entry name.'); continue }
            $taskIsDll = $taskEntryName -match '^(BlueprintTagManager|Prebuilt/UE5\.[0-8]/BlueprintTagManager)/Binaries/Win64/UnrealEditor-BlueprintTagManager\.dll$'
            $taskIsModules = $taskEntryName -match '^(BlueprintTagManager|Prebuilt/UE5\.[0-8]/BlueprintTagManager)/Binaries/Win64/UnrealEditor\.modules$'
            if ($taskEntryName -match $taskBlockedPath -and -not $taskIsDll -and -not $taskIsModules) {
                $taskIssues.Add("Archive ${taskEntryName}: generated or private file must not be distributed")
            }
            $taskStream = $taskEntry.Open()
            $taskBuffer = New-Object System.IO.MemoryStream
            try {
                $taskStream.CopyTo($taskBuffer)
                Test-PublicFileBytes ("Archive " + $taskEntryName) $taskBuffer.ToArray() $taskIsDll
            }
            finally { $taskStream.Dispose(); $taskBuffer.Dispose() }
        }
    }
    finally { $taskArchive.Dispose() }
}

if ($taskIssues.Count -gt 0) {
    foreach ($taskIssue in $taskIssues) { Write-Output ('FAIL: ' + $taskIssue) }
    throw "Public release check failed ($($taskIssues.Count) finding(s); $taskChecked files scanned). Secret values are not printed."
}
Write-Output "Public release check passed: $taskChecked file(s) scanned. No matching private paths or known token patterns found."
