$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$version = (Get-Content "$repo/server-version.txt" -Raw).Trim()
$global:PxUpdaterTest_fixtureArchive = "$repo/build/px-lsp-win-x64-$version.zip"
$cache = "$repo/build/updater-test-$([guid]::NewGuid())"
$global:PxUpdaterTest_requests = 0
$global:PxUpdaterTest_offline = $false
$global:PxUpdaterTest_badHash = $false
$global:PxUpdaterTest_assetArchitecture = 'x64'
function Invoke-RestMethod {
    $global:PxUpdaterTest_requests++
    if ($global:PxUpdaterTest_offline) { throw 'Offline test' }
    $hash = (Get-FileHash $global:PxUpdaterTest_fixtureArchive -Algorithm SHA256).Hash
    if ($global:PxUpdaterTest_badHash) { $hash = '0' * 64 }
    return @{ draft = $false; prerelease = $false; assets = @(@{
        name = "px-lsp-win-$global:PxUpdaterTest_assetArchitecture-$version.zip"
        browser_download_url = "https://github.com/JDeffner/paradox-modding-toolkit/releases/download/vtest/px-lsp-win-$global:PxUpdaterTest_assetArchitecture-$version.zip"
        digest = "sha256:$hash"
    }) }
}
function Invoke-WebRequest { param($Uri, [switch]$UseBasicParsing, $OutFile, $TimeoutSec) Copy-Item -LiteralPath $global:PxUpdaterTest_fixtureArchive -Destination $OutFile }
function Assert($condition, $message) { if (!$condition) { throw $message } }
try {
    & "$repo/update-server.ps1" -CacheRoot $cache -BundledVersion $version -Force
    Assert (!(Test-Path "$cache/current.txt")) 'Same version must not download.'
    Assert ((Get-Content "$cache/status.txt" -Raw) -match 'up to date') 'Manual check needs a current-version status.'
    & "$repo/update-server.ps1" -CacheRoot $cache -BundledVersion '0.0.0' -Force
    if (!(Test-Path "$cache/current.txt")) { throw (Get-Content "$cache/update.log" -Raw) }
    Assert ((Get-Content "$cache/current.txt" -Raw).Trim() -eq $version) 'Update did not activate.'
    Assert (Test-Path "$cache/$version/dist/server.js") 'Payload missing.'
    Assert ((Get-Content "$cache/status.txt" -Raw) -match 'Restart the server') 'Manual update must explain activation.'
    $before = $global:PxUpdaterTest_requests
    & "$repo/update-server.ps1" -CacheRoot $cache -BundledVersion '0.0.0'
    Assert ($global:PxUpdaterTest_requests -eq $before) 'Daily throttle failed.'
    $global:PxUpdaterTest_offline = $true
    & "$repo/update-server.ps1" -CacheRoot $cache -BundledVersion '0.0.0' -Force
    Assert ((Get-Content "$cache/current.txt" -Raw).Trim() -eq $version) 'Offline check lost current server.'
    Assert ((Get-Content "$cache/status.txt" -Raw) -match 'Update failed') 'Manual check must report network failure.'
    $global:PxUpdaterTest_offline = $false
    $global:PxUpdaterTest_badHash = $true
    & "$repo/update-server.ps1" -CacheRoot "$cache/rejected" -BundledVersion '0.0.0' -Force
    Assert (!(Test-Path "$cache/rejected/current.txt")) 'Bad hash activated.'
    Assert ((Get-Content "$cache/rejected/update.log" -Raw) -match 'checksum mismatch') 'Checksum was not checked.'
    $global:PxUpdaterTest_badHash = $false
    foreach ($arch in @('x86', 'arm64')) {
        & "$repo/update-server.ps1" -Architecture $arch -CacheRoot "$cache/$arch" -BundledVersion '0.0.0' -Force
        Assert (!(Test-Path "$cache/$arch/current.txt")) 'Incompatible release must not activate.'
        Assert ((Get-Content "$cache/$arch/status.txt" -Raw) -match "No compatible $arch") 'Missing architecture must explain why the bundled server is kept.'
    }
    # A matching asset name must not hide an executable for another architecture.
    $global:PxUpdaterTest_assetArchitecture = 'x86'
    $payload = "$cache/fixture/px-lsp-win-x86-$version"
    New-Item -ItemType Directory "$payload/dist" -Force | Out-Null
    $pe = New-Object byte[] 512
    $pe[0] = 0x4d; $pe[1] = 0x5a; $pe[0x3c] = 0x80
    $pe[0x80] = 0x50; $pe[0x81] = 0x45; $pe[0x84] = 0x64; $pe[0x85] = 0x86
    [IO.File]::WriteAllBytes("$payload/node.exe", $pe)
    Set-Content "$payload/dist/server.js" '// fixture'
    Set-Content "$payload/px-lsp.cmd" '@exit /b 1'
    $global:PxUpdaterTest_fixtureArchive = "$cache/wrong-machine.zip"
    Compress-Archive -Path $payload -DestinationPath $global:PxUpdaterTest_fixtureArchive
    & "$repo/update-server.ps1" -Architecture x86 -CacheRoot "$cache/wrong-machine" -BundledVersion '0.0.0' -Force
    Assert (!(Test-Path "$cache/wrong-machine/current.txt")) 'Wrong executable architecture activated.'
    Assert ((Get-Content "$cache/wrong-machine/status.txt" -Raw) -match 'architecture does not match') 'Executable architecture was not checked.'
    Write-Host 'Updater tests passed: current version, verified update, daily throttle, offline fallback, checksum rejection, compatible asset selection, executable architecture rejection.'
} finally {
    $resolved = [IO.Path]::GetFullPath($cache)
    $root = [IO.Path]::GetFullPath("$repo/build").TrimEnd('\') + '\'
    if ($resolved.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path $resolved)) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}



