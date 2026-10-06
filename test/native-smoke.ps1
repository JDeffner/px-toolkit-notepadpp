param([switch]$Dark, [switch]$KeepOpen, [ValidateSet('x86', 'x64', 'arm64')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$platform = @{ x86 = 'Win32'; x64 = 'x64'; arm64 = 'ARM64' }[$Architecture]
$suffix = if ($Architecture -eq 'x64') { '' } else { "-$Architecture" }
$smokeOutput = "$repo/build/smoke-dll$suffix"
$package = "$repo/build/package$suffix/PxToolkit"
$portable = @{ x86 = 'npp.8.9.8.portable.zip'; x64 = 'npp.8.9.8.portable.x64.zip'; arm64 = 'npp.8.9.8.portable.arm64.zip' }[$Architecture]
$component = if ($Architecture -eq 'arm64') { 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' } else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
$msbuild = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -all -prerelease -products * -requires $component -find 'MSBuild\**\Bin\MSBuild.exe'
if (!$msbuild) { throw "Visual Studio C++ build tools for $Architecture were not found." }
& $msbuild "$repo/src/PxToolkit.vcxproj" /nologo /m /nr:false /v:minimal /p:Configuration=Release "/p:Platform=$platform" /p:SmokeTest=true "/p:OutDir=$smokeOutput\" "/p:IntDir=$repo\build\obj\smoke$suffix\"
if ($LASTEXITCODE) { throw 'Smoke DLL build failed.' }
$run = "$repo\build\native-smoke-$([guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Path $run | Out-Null
Expand-Archive -LiteralPath "$repo/build/$portable" -DestinationPath "$run/npp"
New-Item -ItemType Directory -Force -Path "$run/npp/plugins/PxToolkit", "$run/npp/plugins/Config", "$run/mod/common/scripted_effects", "$run/mod/common/game_concepts", "$run/mod/events" | Out-Null
Copy-Item -Path "$package/*" -Destination "$run/npp/plugins/PxToolkit" -Recurse
Copy-Item -LiteralPath "$smokeOutput/PxToolkit.dll" -Destination "$run/npp/plugins/PxToolkit/PxToolkit.dll" -Force
Copy-Item -LiteralPath "$repo/update-server.ps1" -Destination "$run/npp/plugins/PxToolkit/update-server.ps1" -Force
Set-Content "$run/npp/plugins/Config/px-toolkit.ini" "[px-toolkit]`r`ngameId=ck3`r`nautoUpdateServer=0" -Encoding ASCII
Set-Content "$run/mod/descriptor.mod" 'name="PX native smoke fixture"' -Encoding ASCII
Set-Content "$run/mod/common/game_concepts/px_smoke_concepts.txt" 'px_smoke_concept = { }' -Encoding ASCII
@'
px_smoke_effect = {
    add_gold = $AMOUNT$
    if = {
        limit = { is_ai = no }
        add_prestige = 5
    }
}
'@ | Set-Content "$run/mod/common/scripted_effects/px_smoke_effects.txt" -Encoding ASCII
@'
namespace = px_smoke
px_smoke.1 = {
    type = character_event
    title = px_smoke_missing_title
    desc = px_smoke_missing_desc
    immediate = {
        px_smoke_effect = { AMOUNT = 10 }
    }
    option = { name = px_smoke_missing_option }
}
'@ | Set-Content "$run/mod/events/px_smoke_events.txt" -Encoding ASCII
$darkMode = if ($Dark) { 'yes' } else { 'no' }
@"
<NotepadPlus><GUIConfigs>
  <GUIConfig name="DarkMode" enable="$darkMode" colorTone="0" />
  <GUIConfig name="ScintillaPrimaryView" isChangeHistoryEnabled="1" />
  <GUIConfig name="AppPosition" x="30" y="30" width="1500" height="1000" isMaximized="no" />
  <GUIConfig name="DockingManager" leftWidth="200" rightWidth="200" topHeight="200" bottomHeight="360" />
</GUIConfigs></NotepadPlus>
"@ | Set-Content "$run/npp/config.xml" -Encoding UTF8
$env:PX_SMOKE_ROOT = $run
# Exercise the packaged launcher without reading or changing the user's server cache.
$previousLocalAppData = $env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA = "$run/local-app-data"
    New-Item -ItemType Directory -Path $env:LOCALAPPDATA -Force | Out-Null
    $process = Start-Process -FilePath "$run/npp/notepad++.exe" -WorkingDirectory "$run/npp" -ArgumentList "-multiInst -nosession $run\mod\common\scripted_effects\px_smoke_effects.txt" -WindowStyle Hidden -PassThru
} finally { $env:LOCALAPPDATA = $previousLocalAppData }
Write-Output "Smoke process: $($process.Id). Results: $run"
Set-Content "$repo/build/last-native-smoke.txt" $run -Encoding ASCII
$deadline = (Get-Date).AddSeconds(90)
while (!(Test-Path "$run/done.txt") -and (Get-Date) -lt $deadline -and !$process.HasExited) { Start-Sleep -Milliseconds 500 }
$passed = (Test-Path "$run/done.txt") -and (Get-Content "$run/done.txt" -Raw).Trim() -eq '0 failures'
if (Test-Path "$run/results.txt") { Get-Content "$run/results.txt" }
if (!$KeepOpen -and !$process.HasExited) {
    if ([IO.Path]::GetFullPath($process.Path) -ne [IO.Path]::GetFullPath("$run/npp/notepad++.exe")) { throw 'Unexpected test process path.' }
    Stop-Process -Id $process.Id
}
if (!$passed) { throw "Native smoke failed. See $run" }
