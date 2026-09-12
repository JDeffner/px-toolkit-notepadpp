@echo off
setlocal
rem Builds PxToolkit.dll and tests for x64 (default), x86, or ARM64.
rem CMake is not used: the
rem only toolchain this needs is MSBuild, which vswhere already knows how to find.

set "ARCH=%~1"
if not defined ARCH set "ARCH=x64"
set "PLATFORM="
if /i "%ARCH%"=="x64" set "PLATFORM=x64"
if /i "%ARCH%"=="x86" set "PLATFORM=Win32"
if /i "%ARCH%"=="arm64" set "PLATFORM=ARM64"
if not defined PLATFORM (
  echo Usage: build.cmd [x86^|x64^|arm64]
  exit /b 1
)

set "VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
if /i "%ARCH%"=="arm64" set "VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.ARM64"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere.exe not found. Install Visual Studio 2022 Build Tools with the
  echo "Desktop development with C++" workload.
  exit /b 1
)

for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -all -prerelease -products * -requires %VC_COMPONENT% -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
  echo MSBuild with the required C++ toolset not found: %VC_COMPONENT%.
  exit /b 1
)

"%MSBUILD%" "%~dp0PxToolkit.sln" /nologo /m /nr:false /v:minimal /p:Configuration=Release /p:Platform=%PLATFORM%
if errorlevel 1 exit /b 1

echo.
echo Built %~dp0build\%PLATFORM%\Release\PxToolkit.dll
echo Built %~dp0build\%PLATFORM%\Release\PxToolkitTests.exe
