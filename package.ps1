$ErrorActionPreference = 'Stop'
$version = '0.2.0'
$serverVersion = (Get-Content "$PSScriptRoot/server-version.txt" -Raw).Trim()
$serverTag = 'v0.4.3'
$serverHash = '2f308b7de406df02aa3ed75112ce0e6ed09d616157f1c1f30e8b6443c5af422b'
$buildRoot = Join-Path $PSScriptRoot 'build'
& "$PSScriptRoot/build.cmd"
if ($LASTEXITCODE) { throw 'Build failed.' }
& "$buildRoot/x64/Release/PxToolkitTests.exe"
if ($LASTEXITCODE) { throw 'Tests failed.' }
$name = "px-lsp-win-x64-$serverVersion"
$archive = "$buildRoot/$name.zip"
if (!(Test-Path $archive)) {
    & "$env:SystemRoot/System32/curl.exe" -fL --retry 3 -o "$archive.part" "https://github.com/JDeffner/paradox-modding-toolkit/releases/download/$serverTag/$name.zip"
    if ($LASTEXITCODE) { throw 'Server download failed.' }
    Move-Item -LiteralPath "$archive.part" -Destination $archive -Force
}
if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $serverHash) { throw 'Server checksum mismatch.' }
$stage = "$buildRoot/package"
foreach ($target in @($stage, "$buildRoot/$name")) {
    $resolved = [IO.Path]::GetFullPath($target)
    $allowedRoot = [IO.Path]::GetFullPath($buildRoot).TrimEnd('\') + '\'
    if (!$resolved.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid build cleanup path.' }
    if (Test-Path $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
Expand-Archive -LiteralPath $archive -DestinationPath $buildRoot -Force
New-Item -ItemType Directory -Path "$stage/PxToolkit" -Force | Out-Null
Copy-Item -LiteralPath "$buildRoot/x64/Release/PxToolkit.dll" -Destination "$stage/PxToolkit"
Copy-Item -LiteralPath "$buildRoot/$name" -Destination "$stage/PxToolkit/px-lsp" -Recurse
foreach ($file in @('update-server.ps1', 'server-version.txt', 'LICENSE', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -LiteralPath "$PSScriptRoot/$file" -Destination "$stage/PxToolkit"
}
$out = "$buildRoot/PxToolkit-$version-win-x64.zip"
Compress-Archive -Path "$stage/PxToolkit" -DestinationPath $out -Force
Write-Host "Built $out (px-lsp $serverVersion)"
