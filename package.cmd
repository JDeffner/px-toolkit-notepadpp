@echo off
setlocal
rem Builds the DLL and assembles the release zip: the plugin plus the px-lsp
rem server payload, so a user extracts one archive and needs no Node and no npm.
rem The payload is never vendored into git; it is downloaded here and cached in
rem build\, which is ignored.

rem The only version knobs. PLUGIN_VERSION names the zip; the other two pin the
rem px-lsp release the payload is taken from.
set "PLUGIN_VERSION=0.1.0"
set "SERVER_RELEASE_TAG=v0.3.5"
set "SERVER_VERSION=0.3.0"

set "SERVER_NAME=px-lsp-win-x64-%SERVER_VERSION%"
set "SERVER_URL=https://github.com/JDeffner/paradox-modding-toolkit/releases/download/%SERVER_RELEASE_TAG%/%SERVER_NAME%.zip"

rem curl.exe and tar.exe ship with Windows 10 and later. Call them by full path:
rem Git for Windows puts a GNU tar first on PATH, and GNU tar cannot write zips.
set "CURL=%SystemRoot%\System32\curl.exe"
set "TAR=%SystemRoot%\System32\tar.exe"
if not exist "%CURL%" (
  echo curl.exe not found in System32. Windows 10 or later is required.
  exit /b 1
)
if not exist "%TAR%" (
  echo tar.exe not found in System32. Windows 10 or later is required.
  exit /b 1
)

set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "PAYLOAD=%BUILD%\%SERVER_NAME%.zip"
set "STAGE=%BUILD%\package"
set "OUT=%BUILD%\PxToolkit-%PLUGIN_VERSION%-win-x64.zip"

call "%ROOT%build.cmd"
if errorlevel 1 exit /b 1

if exist "%PAYLOAD%" (
  echo Using cached %SERVER_NAME%.zip
) else (
  echo Downloading %SERVER_URL%
  rem Download aside and rename, so an interrupted transfer is not cached as done.
  "%CURL%" -fL --progress-bar -o "%PAYLOAD%.part" "%SERVER_URL%"
  if errorlevel 1 (
    del /q "%PAYLOAD%.part" 2>nul
    echo Download failed. Check the release tag %SERVER_RELEASE_TAG%.
    exit /b 1
  )
  move /y "%PAYLOAD%.part" "%PAYLOAD%" >nul
)

if exist "%BUILD%\%SERVER_NAME%" rmdir /s /q "%BUILD%\%SERVER_NAME%"
"%TAR%" -x -f "%PAYLOAD%" -C "%BUILD%"
if errorlevel 1 exit /b 1

rem Layout: the archive root is the folder that lands in Notepad++'s plugins\.
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\PxToolkit\px-lsp"
copy /y "%BUILD%\x64\Release\PxToolkit.dll" "%STAGE%\PxToolkit\" >nul
if errorlevel 1 exit /b 1
xcopy "%BUILD%\%SERVER_NAME%\*" "%STAGE%\PxToolkit\px-lsp\" /e /i /q /y >nul
if errorlevel 1 exit /b 1

if exist "%OUT%" del /q "%OUT%"
pushd "%STAGE%"
"%TAR%" -a -c -f "%OUT%" PxToolkit
set "ZIPERR=%errorlevel%"
popd
if not "%ZIPERR%"=="0" exit /b 1

echo.
echo Built %OUT%
echo   PxToolkit\PxToolkit.dll, PxToolkit\px-lsp\ (px-lsp %SERVER_VERSION%)
echo Extract it into Notepad++'s plugins folder.
