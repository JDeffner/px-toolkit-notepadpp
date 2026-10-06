param([ValidateSet('x86', 'x64', 'arm64')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$pluginVersion = '0.2.2'
if ($env:GITHUB_REF_TYPE -eq 'tag' -and $env:GITHUB_REF_NAME -ne "v$pluginVersion") {
    throw "Release tag must match Notepad++ plugin version v$pluginVersion."
}
$platform = @{ x86 = 'Win32'; x64 = 'x64'; arm64 = 'ARM64' }[$Architecture]
$serverVersion = (Get-Content "$PSScriptRoot/server-version.txt" -Raw).Trim()
$serverTag = 'v0.5.5'
$serverHash = '8f01ffbee94c43bbf0b2590d5c99e318abf3600c44c988d73c7b76394c6faa15'
$buildRoot = Join-Path $PSScriptRoot 'build'
& "$PSScriptRoot/build.cmd" $Architecture
if ($LASTEXITCODE) { throw 'Build failed.' }
$dllVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo("$buildRoot/$platform/Release/PxToolkit.dll")
if ($dllVersion.ProductVersion -ne $pluginVersion -or $dllVersion.FileVersion -ne "$pluginVersion.0") {
    throw 'Notepad++ plugin DLL version does not match the package version.'
}
$hostArchitecture = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
if ($Architecture -ne 'arm64' -or $hostArchitecture -eq 'ARM64') {
    & "$buildRoot/$platform/Release/PxToolkitTests.exe"
    if ($LASTEXITCODE) { throw 'Tests failed.' }
} else { Write-Host 'ARM64 tests built, but execution requires an ARM64 Windows host.' }
$name = "px-lsp-win-x64-$serverVersion"
$archive = "$buildRoot/$name.zip"
if (!(Test-Path $archive)) {
    & "$env:SystemRoot/System32/curl.exe" -fL --retry 3 -o "$archive.part" "https://github.com/JDeffner/paradox-modding-toolkit/releases/download/$serverTag/$name.zip"
    if ($LASTEXITCODE) { throw 'Server download failed.' }
    Move-Item -LiteralPath "$archive.part" -Destination $archive -Force
}
if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $serverHash) { throw 'Server checksum mismatch.' }
$stage = if ($Architecture -eq 'x64') { "$buildRoot/package" } else { "$buildRoot/package-$Architecture" }
foreach ($target in @($stage, "$buildRoot/$name")) {
    $resolved = [IO.Path]::GetFullPath($target)
    $allowedRoot = [IO.Path]::GetFullPath($buildRoot).TrimEnd('\') + '\'
    if (!$resolved.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid build cleanup path.' }
    if (Test-Path $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
Expand-Archive -LiteralPath $archive -DestinationPath $buildRoot -Force
New-Item -ItemType Directory -Path "$stage/PxToolkit" -Force | Out-Null
Copy-Item -LiteralPath "$buildRoot/$platform/Release/PxToolkit.dll" -Destination "$stage/PxToolkit"
Copy-Item -LiteralPath "$buildRoot/$name" -Destination "$stage/PxToolkit/px-lsp" -Recurse
# The upstream server bundle is JavaScript/data. Only its Node executable is architecture-specific.
if ($Architecture -ne 'x64') {
    $nodeVersion = '22.23.2'
    $nodeHashes = @{
        x86 = '725c9e2bdd1c2016b41c995a81f4fa36ce4e2ee565b7455d8f889182727df647'
        arm64 = 'fec025a6da31757e3b6af84c5a1628e9d38442ca99a2161091d78f2fcfa35ef3'
    }
    $nodeName = "node-v$nodeVersion-win-$Architecture"
    $nodeArchive = "$buildRoot/$nodeName.zip"
    if (!(Test-Path $nodeArchive)) {
        & "$env:SystemRoot/System32/curl.exe" -fL --retry 3 -o "$nodeArchive.part" "https://nodejs.org/dist/v$nodeVersion/$nodeName.zip"
        if ($LASTEXITCODE) { throw 'Node download failed.' }
        Move-Item -LiteralPath "$nodeArchive.part" -Destination $nodeArchive -Force
    }
    if ((Get-FileHash $nodeArchive -Algorithm SHA256).Hash -ne $nodeHashes[$Architecture]) { throw 'Node checksum mismatch.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($nodeArchive)
    try {
        foreach ($item in @(@('node.exe', 'node.exe'), @('LICENSE', 'NODE-LICENSE'))) {
            $entry = $zip.GetEntry("$nodeName/$($item[0])")
            if (!$entry) { throw "Node archive is missing $($item[0])." }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, "$stage/PxToolkit/px-lsp/$($item[1])", $true)
        }
    } finally { $zip.Dispose() }
}
# Cross-builds still verify both PE headers when this host cannot execute ARM64.
foreach ($executable in @("$stage/PxToolkit/PxToolkit.dll", "$stage/PxToolkit/px-lsp/node.exe")) {
    $binary = [IO.File]::OpenRead($executable)
    $reader = [IO.BinaryReader]::new($binary)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid executable: $executable" }
        $binary.Position = 0x3c
        $header = $reader.ReadUInt32()
        if ($header -gt $binary.Length - 6) { throw "Invalid PE header: $executable" }
        $binary.Position = $header
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE signature: $executable" }
        if ($reader.ReadUInt16() -ne @{ x86 = 0x14c; x64 = 0x8664; arm64 = 0xaa64 }[$Architecture]) { throw "Wrong architecture: $executable" }
    } finally { $reader.Dispose() }
}
if ($Architecture -ne 'arm64' -or $hostArchitecture -eq 'ARM64') {
    $reported = & "$stage/PxToolkit/px-lsp/node.exe" "$stage/PxToolkit/px-lsp/dist/server.js" --version
    if ($LASTEXITCODE -ne 0 -or ($reported -join "`n").Trim() -notmatch "(^|\s)$([regex]::Escape($serverVersion))$") { throw 'Packaged server version check failed.' }
}
Set-Content "$stage/PxToolkit/architecture.txt" $Architecture -Encoding ASCII
foreach ($file in @('update-server.ps1', 'server-version.txt', 'README.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'CONTRIBUTING.md', 'SUPPORT.md', 'SECURITY.md', 'CODE_OF_CONDUCT.md', 'CHANGELOG.md')) {
    Copy-Item -LiteralPath "$PSScriptRoot/$file" -Destination "$stage/PxToolkit"
}
Copy-Item -LiteralPath "$PSScriptRoot/docs" -Destination "$stage/PxToolkit/docs" -Recurse
New-Item -ItemType Directory -Path "$stage/PxToolkit/assets/branding" -Force | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/assets/branding/icon-256.png" -Destination "$stage/PxToolkit/assets/branding"
$out = "$buildRoot/PxToolkit-NotepadPlusPlus-$pluginVersion-win-$Architecture.zip"
Compress-Archive -Path "$stage/PxToolkit" -DestinationPath $out -Force
Write-Host "Built $out (Notepad++ plugin $pluginVersion; bundled px-lsp $serverVersion)"
