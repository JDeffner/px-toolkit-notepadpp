param([switch]$Dark, [switch]$KeepOpen)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$msbuild = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -all -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe'
& $msbuild "$repo/src/PxToolkit.vcxproj" /nologo /m /v:minimal /p:Configuration=Release /p:Platform=x64 /p:SmokeTest=true "/p:OutDir=$repo\build\smoke-dll\" "/p:IntDir=$repo\build\obj\smoke\"
if ($LASTEXITCODE) { throw 'Smoke DLL build failed.' }
$run = "$repo\build\native-smoke-$([guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Path $run | Out-Null
Expand-Archive -LiteralPath "$repo/build/npp.8.9.8.portable.x64.zip" -DestinationPath "$run/npp"
New-Item -ItemType Directory -Force -Path "$run/npp/plugins/PxToolkit", "$run/npp/plugins/Config", "$run/mod/common/scripted_effects", "$run/mod/common/game_concepts", "$run/mod/events" | Out-Null
Copy-Item -Path "$repo/build/package/PxToolkit/*" -Destination "$run/npp/plugins/PxToolkit" -Recurse
Copy-Item -LiteralPath "$repo/build/smoke-dll/PxToolkit.dll" -Destination "$run/npp/plugins/PxToolkit/PxToolkit.dll" -Force
Copy-Item -LiteralPath "$repo/update-server.ps1" -Destination "$run/npp/plugins/PxToolkit/update-server.ps1" -Force
Set-Content "$run/npp/plugins/Config/px-toolkit.ini" "[px-toolkit]`r`ngameId=ck3`r`nautoUpdateServer=0`r`nserverCommand=$run\npp\plugins\PxToolkit\px-lsp\px-lsp.cmd" -Encoding ASCII
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
if ($Dark) {
    Set-Content "$run/npp/config.xml" '<NotepadPlus><GUIConfigs><GUIConfig name="DarkMode" enable="yes" colorTone="0" /></GUIConfigs></NotepadPlus>' -Encoding UTF8
}
$env:PX_SMOKE_ROOT = $run
$process = Start-Process -FilePath "$run/npp/notepad++.exe" -WorkingDirectory "$run/npp" -ArgumentList "-multiInst -nosession $run\mod\common\scripted_effects\px_smoke_effects.txt" -PassThru
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
