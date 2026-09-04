@echo off
setlocal
rem Builds PxToolkit.dll and the test exe, Release x64. CMake is not used: the
rem only toolchain this needs is MSBuild, which vswhere already knows how to find.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere.exe not found. Install Visual Studio 2022 Build Tools with the
  echo "Desktop development with C++" workload.
  exit /b 1
)

for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -all -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
  echo MSBuild with the C++ toolset not found.
  exit /b 1
)

"%MSBUILD%" "%~dp0PxToolkit.sln" /nologo /m /v:minimal /p:Configuration=Release /p:Platform=x64
if errorlevel 1 exit /b 1

echo.
echo Built %~dp0build\x64\Release\PxToolkit.dll
echo Built %~dp0build\x64\Release\PxToolkitTests.exe
