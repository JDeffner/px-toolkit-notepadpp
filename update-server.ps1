param(
    [string]$CacheRoot = "$env:LOCALAPPDATA\PxToolkit\servers",
    [string]$BundledVersion = (Get-Content "$PSScriptRoot\server-version.txt" -Raw).Trim(),
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$lock = $null
$stage = $null
try {
    New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
    # One writer across multiple Notepad++ processes. Closing the handle releases it.
    $lock = [IO.File]::Open("$CacheRoot\update.lock", 'OpenOrCreate', 'ReadWrite', 'None')
    $stamp = "$CacheRoot\last-check.txt"
    if (!$Force -and (Test-Path $stamp) -and ((Get-Date).ToUniversalTime() - (Get-Item $stamp).LastWriteTimeUtc).TotalHours -lt 24) { return }
    Set-Content $stamp ([DateTime]::UtcNow.ToString('o'))
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $release = Invoke-RestMethod 'https://api.github.com/repos/JDeffner/paradox-modding-toolkit/releases/latest' -Headers @{ 'User-Agent' = 'PxToolkit-Notepadpp' } -TimeoutSec 30
    $asset = @($release.assets | Where-Object { $_.name -match '^px-lsp-win-x64-\d+\.\d+\.\d+\.zip$' })
    if ($release.draft -or $release.prerelease -or $asset.Count -ne 1) { throw 'No stable Windows server asset found.' }
    $asset = $asset[0]
    $version = $asset.name -replace '^px-lsp-win-x64-|\.zip$', ''
    $current = [version]$BundledVersion
    $pointer = "$CacheRoot\current.txt"
    if (Test-Path $pointer) {
        $cached = (Get-Content $pointer -Raw).Trim()
        if ($cached -match '^\d+\.\d+\.\d+$' -and (Test-Path "$CacheRoot\$cached\px-lsp.cmd") -and [version]$cached -gt $current) { $current = [version]$cached }
    }
    if ([version]$version -le $current) { Set-Content "$CacheRoot\status.txt" "LSP $current is up to date." -Encoding UTF8; return }
    if ($asset.browser_download_url -notmatch '^https://github\.com/JDeffner/paradox-modding-toolkit/releases/download/[^/]+/px-lsp-win-x64-\d+\.\d+\.\d+\.zip$') { throw 'Unexpected asset URL.' }
    if ($asset.digest -notmatch '^sha256:[a-fA-F0-9]{64}$') { throw 'Missing SHA-256 digest.' }
    $stage = Join-Path $CacheRoot ([guid]::NewGuid().ToString())
    New-Item -ItemType Directory -Path $stage | Out-Null
    $archive = "$stage\server.zip"
    Invoke-WebRequest $asset.browser_download_url -UseBasicParsing -OutFile $archive -TimeoutSec 180
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $asset.digest.Substring(7)) { throw 'Server download checksum mismatch.' }
    Expand-Archive -LiteralPath $archive -DestinationPath "$stage\unpacked"
    $payload = "$stage\unpacked\px-lsp-win-x64-$version"
    foreach ($file in @('node.exe', 'dist\server.js', 'px-lsp.cmd')) {
        if (!(Test-Path "$payload\$file" -PathType Leaf)) { throw "Missing server file: $file" }
    }
    $reported = & "$payload\node.exe" "$payload\dist\server.js" --version 2>&1
    if ($LASTEXITCODE -ne 0 -or ($reported -join "`n").Trim() -notmatch "(^|\s)$([regex]::Escape($version))$") { throw 'Downloaded server failed its version check.' }
    $destination = "$CacheRoot\$version"
    if (!(Test-Path $destination)) { Move-Item -LiteralPath $payload -Destination $destination }
    # Publish only after validation. Existing servers stay in place while in use.
    $pending = "$CacheRoot\current.pending"
    Set-Content -Path $pending -Value $version -Encoding ASCII
    if (Test-Path $pointer) { [IO.File]::Replace($pending, $pointer, $null) }
    else { [IO.File]::Move($pending, $pointer) }
    Add-Content "$CacheRoot\update.log" "$([DateTime]::UtcNow.ToString('o')) Ready: px-lsp $version. Applies on next server start."
    Set-Content "$CacheRoot\status.txt" "LSP $version is ready. Restart the server to use it." -Encoding UTF8
} catch {
    if ($lock) { Add-Content "$CacheRoot\update.log" "$([DateTime]::UtcNow.ToString('o')) $($_.Exception.Message)"; Set-Content "$CacheRoot\status.txt" "Update failed. The existing LSP is unchanged. $($_.Exception.Message)" -Encoding UTF8 }
} finally {
    if ($stage -and (Test-Path $stage)) {
        $root = [IO.Path]::GetFullPath($CacheRoot).TrimEnd('\') + '\'
        if ([IO.Path]::GetFullPath($stage).StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $stage -Recurse -Force }
    }
    if ($lock) { $lock.Dispose() }
}
