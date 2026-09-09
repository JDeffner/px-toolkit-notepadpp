$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$version = (Get-Content "$repo/server-version.txt" -Raw).Trim()
$global:PxUpdaterTest_fixtureArchive = "$repo/build/px-lsp-win-x64-$version.zip"
$cache = "$repo/build/updater-test-$([guid]::NewGuid())"
$global:PxUpdaterTest_requests = 0
$global:PxUpdaterTest_offline = $false
$global:PxUpdaterTest_badHash = $false
function Invoke-RestMethod {
    $global:PxUpdaterTest_requests++
    if ($global:PxUpdaterTest_offline) { throw 'Offline test' }
    $hash = (Get-FileHash $global:PxUpdaterTest_fixtureArchive -Algorithm SHA256).Hash
    if ($global:PxUpdaterTest_badHash) { $hash = '0' * 64 }
    return @{ draft = $false; prerelease = $false; assets = @(@{
        name = "px-lsp-win-x64-$version.zip"
        browser_download_url = "https://github.com/JDeffner/paradox-modding-toolkit/releases/download/vtest/px-lsp-win-x64-$version.zip"
        digest = "sha256:$hash"
    }) }
}
function Invoke-WebRequest { param($Uri, [switch]$UseBasicParsing, $OutFile, $TimeoutSec) Copy-Item -LiteralPath $global:PxUpdaterTest_fixtureArchive -Destination $OutFile }
function Assert($condition, $message) { if (!$condition) { throw $message } }
try {
    & "$repo/update-server.ps1" -CacheRoot $cache -BundledVersion $version -Force
    Assert (!(Test-Path "$cache/current.txt")) 'Same version must not download.'
    & "$repo/update-server.ps1" -CacheRoot $cache -BundledVersion '0.0.0' -Force
    if (!(Test-Path "$cache/current.txt")) { throw (Get-Content "$cache/update.log" -Raw) }
    Assert ((Get-Content "$cache/current.txt" -Raw).Trim() -eq $version) 'Update did not activate.'
    Assert (Test-Path "$cache/$version/dist/server.js") 'Payload missing.'
    $before = $global:PxUpdaterTest_requests
    & "$repo/update-server.ps1" -CacheRoot $cache -BundledVersion '0.0.0'
    Assert ($global:PxUpdaterTest_requests -eq $before) 'Daily throttle failed.'
    $global:PxUpdaterTest_offline = $true
    & "$repo/update-server.ps1" -CacheRoot $cache -BundledVersion '0.0.0' -Force
    Assert ((Get-Content "$cache/current.txt" -Raw).Trim() -eq $version) 'Offline check lost current server.'
    $global:PxUpdaterTest_offline = $false
    $global:PxUpdaterTest_badHash = $true
    & "$repo/update-server.ps1" -CacheRoot "$cache/rejected" -BundledVersion '0.0.0' -Force
    Assert (!(Test-Path "$cache/rejected/current.txt")) 'Bad hash activated.'
    Assert ((Get-Content "$cache/rejected/update.log" -Raw) -match 'checksum mismatch') 'Checksum was not checked.'
    Write-Host 'Updater tests passed: current version, verified update, daily throttle, offline fallback, checksum rejection.'
} finally {
    $resolved = [IO.Path]::GetFullPath($cache)
    $root = [IO.Path]::GetFullPath("$repo/build").TrimEnd('\') + '\'
    if ($resolved.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path $resolved)) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}



