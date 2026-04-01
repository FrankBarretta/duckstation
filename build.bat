@echo off
setlocal EnableExtensions

set "ROOT_DIR=%~dp0"
cd /d "%ROOT_DIR%"

set "GENERATOR=Visual Studio 18 2026"
set "ARCH=x64"
set "CONFIG=Release"
set "BUILD_DIR=build-vs2026"
set "DEPS_DIR=dep\prebuilt\windows-x64"
set "POWERSHELL_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"

if not "%~1"=="" set "CONFIG=%~1"
if not "%~2"=="" set "BUILD_DIR=%~2"

if not exist "%POWERSHELL_EXE%" (
  echo PowerShell non trovato: "%POWERSHELL_EXE%"
  exit /b 1
)

if not exist "%DEPS_DIR%" (
  echo Dipendenze prebuild mancanti in "%DEPS_DIR%".
  echo Scarica deps-windows-x64.7z da https://github.com/duckstation/dependencies/releases/tag/release-20260328
  echo ed estrailo in dep\prebuilt prima di lanciare questo script.
  exit /b 1
)

call :patch_prebuilt_cmake || exit /b 1

echo Configuring with %GENERATOR% ^(%ARCH%^), config %CONFIG%...
cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A %ARCH% -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 exit /b %errorlevel%

echo Building %CONFIG%...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 exit /b %errorlevel%

echo.
echo Build completata: "%CD%\%BUILD_DIR%\bin\%CONFIG%\duckstation-qt.exe"
exit /b 0

:patch_prebuilt_cmake
echo Patching extracted prebuilt CMake metadata...
"%POWERSHELL_EXE%" -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; function Patch-File([string]$Path, [string]$Old, [string]$New) { if (-not (Test-Path -LiteralPath $Path)) { throw ('File not found: ' + $Path) }; $Content = Get-Content -Raw -LiteralPath $Path; if ($Content.Contains($New)) { return }; if (-not $Content.Contains($Old)) { throw ('Unexpected content in ' + $Path) }; Set-Content -LiteralPath $Path -Value $Content.Replace($Old, $New) -Encoding Ascii }; Patch-File 'dep/prebuilt/windows-x64/lib/cmake/SDL3/SDL3sharedTargets.cmake' 'set(_IMPORT_PREFIX \"D:/a/dependencies/dependencies/windows-x64\")' 'get_filename_component(_IMPORT_PREFIX \"${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)'; Patch-File 'dep/prebuilt/windows-x64/lib/cmake/SDL3/SDL3headersTargets.cmake' 'set(_IMPORT_PREFIX \"D:/a/dependencies/dependencies/windows-x64\")' 'get_filename_component(_IMPORT_PREFIX \"${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)'; Patch-File 'dep/prebuilt/windows-x64/lib/cmake/SDL3/SDL3testTargets.cmake' 'set(_IMPORT_PREFIX \"D:/a/dependencies/dependencies/windows-x64\")' 'get_filename_component(_IMPORT_PREFIX \"${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)'; Patch-File 'dep/prebuilt/windows-x64/lib/cmake/Qt6BuildInternals/QtBuildInternalsExtra.cmake' 'set(qtbi_orig_prefix \"D:/a/dependencies/dependencies/windows-x64\")' 'get_filename_component(qtbi_orig_prefix \"${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)'"
if errorlevel 1 exit /b %errorlevel%
exit /b 0