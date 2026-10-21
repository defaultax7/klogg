@echo off
rem ============================================================================
rem  Klogg Windows packaging helper
rem
rem  Builds klogg.exe and klogg_portable.exe, deploys the Qt runtime and
rem  Visual C++ runtime, and creates a ZIP that can be shared with others.
rem
rem  Usage: pack.bat [options]
rem
rem  Options:
rem    --release            Package a Release build (default: RelWithDebInfo)
rem    --relwithdebinfo     Package a RelWithDebInfo build (default)
rem    --qt <path>          Path to the Qt installation
rem    --build-dir <path>   Existing CMake build directory
rem    --no-zip             Only create the package folder, no ZIP
rem    --help               Show this help
rem
rem  Environment variables:
rem    QT_DIR, BUILD_DIR, BUILD_CONFIG
rem ============================================================================

setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

if not defined QT_DIR set "QT_DIR=C:\Qt\5.15.2\msvc2019_64"
if not defined BUILD_DIR set "BUILD_DIR=%SCRIPT_DIR%\build_root"
if not defined BUILD_CONFIG set "BUILD_CONFIG=RelWithDebInfo"

set "CREATE_ZIP=1"

:parse_args
if "%~1"=="" goto args_parsed
if /I "%~1"=="--release"        ( set "BUILD_CONFIG=Release" & shift & goto parse_args )
if /I "%~1"=="--relwithdebinfo" ( set "BUILD_CONFIG=RelWithDebInfo" & shift & goto parse_args )
if /I "%~1"=="--no-zip"         ( set "CREATE_ZIP=0" & shift & goto parse_args )
if /I "%~1"=="--help"           goto help
if /I "%~1"=="--qt" (
    if "%~2"=="" ( echo ERROR: --qt requires a path & exit /b 1 )
    set "QT_DIR=%~2" & shift & shift & goto parse_args
)
if /I "%~1"=="--build-dir" (
    if "%~2"=="" ( echo ERROR: --build-dir requires a path & exit /b 1 )
    set "BUILD_DIR=%~2" & shift & shift & goto parse_args
)
echo ERROR: unknown option "%~1"
goto usage

:usage
call :print_usage
exit /b 1

:help
call :print_usage
exit /b 0

:print_usage
echo Usage: pack.bat [--release^|--relwithdebinfo] [--qt ^<path^>]
echo                 [--build-dir ^<path^>] [--no-zip] [--help]
exit /b 0

:args_parsed

if /I "%BUILD_CONFIG%"=="Debug" (
    echo ERROR: Debug builds are not suitable for distribution.
    echo        Use --release or --relwithdebinfo.
    exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake was not found in PATH.
    exit /b 1
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo ERROR: "%BUILD_DIR%" is not a configured CMake build directory.
    echo        Run build.bat first.
    exit /b 1
)

set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
if not exist "%WINDEPLOYQT%" (
    echo ERROR: windeployqt.exe not found at "%WINDEPLOYQT%".
    echo        Pass --qt ^<path^> or set QT_DIR.
    exit /b 1
)

set "OUTPUT_DIR=%BUILD_DIR%\output\%BUILD_CONFIG%"
if not exist "%OUTPUT_DIR%\klogg.exe" (
    echo ERROR: "%OUTPUT_DIR%\klogg.exe" was not found.
    echo        Build the project first with build.bat.
    exit /b 1
)

rem --------------------------------------------------------------------------
rem Locate Visual Studio so windeployqt can copy the C++ runtime.
rem --------------------------------------------------------------------------
set "VSINSTALL="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
)
if defined VSINSTALL (
    if exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" (
        call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)

rem --------------------------------------------------------------------------
rem Determine package version and paths.
rem --------------------------------------------------------------------------
set "KLOGG_VERSION="
for /f "usebackq delims=" %%v in (`powershell -NoProfile -Command "$m=(Select-String -Path '%SCRIPT_DIR%\CMakeLists.txt' -Pattern 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)').Matches; if($m){$m[0].Groups[1].Value}"`) do set "KLOGG_VERSION=%%v"
if not defined KLOGG_VERSION set "KLOGG_VERSION=dev"

set "PACKAGE_ROOT=%SCRIPT_DIR%\packages"
set "PACKAGE_NAME=klogg-%KLOGG_VERSION%-win64"
set "PACKAGE_DIR=%PACKAGE_ROOT%\%PACKAGE_NAME%"
set "ZIP_FILE=%PACKAGE_ROOT%\%PACKAGE_NAME%.zip"

rem --------------------------------------------------------------------------
rem Build the app and the portable variant.
rem --------------------------------------------------------------------------
echo Building klogg (%BUILD_CONFIG%)...
cmake --build "%BUILD_DIR%" --config %BUILD_CONFIG% --target klogg --parallel
if errorlevel 1 (
    echo ERROR: build failed.
    exit /b 1
)

echo Building klogg_portable (%BUILD_CONFIG%)...
cmake --build "%BUILD_DIR%" --config %BUILD_CONFIG% --target klogg_portable --parallel
if errorlevel 1 (
    echo ERROR: portable build failed.
    exit /b 1
)

rem --------------------------------------------------------------------------
rem Prepare the package folder.
rem --------------------------------------------------------------------------
if not exist "%PACKAGE_ROOT%" mkdir "%PACKAGE_ROOT%"
if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"
mkdir "%PACKAGE_DIR%"
if errorlevel 1 (
    echo ERROR: failed to create "%PACKAGE_DIR%".
    exit /b 1
)

echo Copying executables...
copy /y "%OUTPUT_DIR%\klogg.exe" "%PACKAGE_DIR%\" >nul
if errorlevel 1 (
    echo ERROR: failed to copy klogg.exe.
    exit /b 1
)
copy /y "%OUTPUT_DIR%\klogg_portable.exe" "%PACKAGE_DIR%\" >nul
if errorlevel 1 (
    echo ERROR: failed to copy klogg_portable.exe.
    exit /b 1
)

echo Copying TBB runtime...
for %%f in ("%OUTPUT_DIR%\tbb*.dll") do (
    if exist "%%~ff" copy /y "%%~ff" "%PACKAGE_DIR%\" >nul
)

rem Some builds may provide OpenSSL next to the Qt installation.
for %%f in ("%QT_DIR%\bin\libssl*.dll") do (
    if exist "%%~ff" copy /y "%%~ff" "%PACKAGE_DIR%\" >nul
)
for %%f in ("%QT_DIR%\bin\libcrypto*.dll") do (
    if exist "%%~ff" copy /y "%%~ff" "%PACKAGE_DIR%\" >nul
)

echo Deploying Qt runtime...
pushd "%PACKAGE_DIR%"
"%WINDEPLOYQT%" --release --no-translations --no-compiler-runtime "klogg.exe"
set "WINDEPLOYQT_RESULT=%ERRORLEVEL%"
popd
if not "%WINDEPLOYQT_RESULT%"=="0" (
    echo WARNING: windeployqt reported an error. The package may be incomplete.
)

rem Include the offscreen plugin so the package can be smoke-tested headlessly.
if exist "%QT_DIR%\plugins\platforms\qoffscreen.dll" (
    if not exist "%PACKAGE_DIR%\platforms" mkdir "%PACKAGE_DIR%\platforms"
    copy /y "%QT_DIR%\plugins\platforms\qoffscreen.dll" "%PACKAGE_DIR%\platforms\" >nul
)

rem --------------------------------------------------------------------------
rem Copy the Visual C++ runtime when available.
rem --------------------------------------------------------------------------
set "VC_RUNTIME_COPIED=0"
if defined VCToolsRedistDir (
    set "REDIST_DIR=%VCToolsRedistDir%"
    if not "!REDIST_DIR:~-1!"=="\" set "REDIST_DIR=!REDIST_DIR!\"
    for /d %%d in ("!REDIST_DIR!x64\Microsoft.VC*.CRT") do (
        if exist "%%~d\*.dll" (
            copy /y "%%~d\*.dll" "%PACKAGE_DIR%\" >nul
            if not errorlevel 1 set "VC_RUNTIME_COPIED=1"
        )
    )
)
if "%VC_RUNTIME_COPIED%"=="0" (
    for %%f in (msvcp140.dll vcruntime140.dll vcruntime140_1.dll concrt140.dll) do (
        if exist "%SystemRoot%\System32\%%f" (
            copy /y "%SystemRoot%\System32\%%f" "%PACKAGE_DIR%\" >nul
        )
    )
)

rem The copied runtime DLLs are sufficient; do not ship the large redistributable.
if exist "%PACKAGE_DIR%\vc_redist.x64.exe" del /q "%PACKAGE_DIR%\vc_redist.x64.exe"


rem --------------------------------------------------------------------------
rem Documentation and package README.
rem --------------------------------------------------------------------------
for %%f in (COPYING NOTICE DOCUMENTATION.md) do (
    if exist "%SCRIPT_DIR%\%%f" copy /y "%SCRIPT_DIR%\%%f" "%PACKAGE_DIR%\" >nul
)
call :write_readme

rem --------------------------------------------------------------------------
rem Create the ZIP.
rem --------------------------------------------------------------------------
if "%CREATE_ZIP%"=="1" (
    if exist "%ZIP_FILE%" del /q "%ZIP_FILE%"

    echo Creating ZIP "%ZIP_FILE%"...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%PACKAGE_DIR%' -DestinationPath '%ZIP_FILE%' -Force" >nul
    if errorlevel 1 (
        echo WARNING: Compress-Archive failed, trying tar...
        tar -a -c -f "%ZIP_FILE%" -C "%PACKAGE_ROOT%" "%PACKAGE_NAME%"
        if errorlevel 1 (
            echo ERROR: failed to create ZIP archive.
            exit /b 1
        )
    )
)

echo.
echo Package created:
echo   Folder: "%PACKAGE_DIR%"
if "%CREATE_ZIP%"=="1" echo   ZIP:    "%ZIP_FILE%"
echo.
echo The ZIP contains klogg.exe, klogg_portable.exe, Qt DLLs, plugins and the
echo Visual C++ runtime. Recipients can extract it and run klogg.exe.
exit /b 0

:write_readme
> "%PACKAGE_DIR%\README.txt" echo Klogg %KLOGG_VERSION% - Windows 64-bit package
>>"%PACKAGE_DIR%\README.txt" echo Requires 64-bit Windows 10 or later.
>>"%PACKAGE_DIR%\README.txt" echo ==========================================
>>"%PACKAGE_DIR%\README.txt" echo.
>>"%PACKAGE_DIR%\README.txt" echo Run klogg.exe to use your normal per-user klogg settings.
>>"%PACKAGE_DIR%\README.txt" echo Run klogg_portable.exe to keep klogg.conf and the session
>>"%PACKAGE_DIR%\README.txt" echo file in this folder instead of your user profile.
>>"%PACKAGE_DIR%\README.txt" echo.
>>"%PACKAGE_DIR%\README.txt" echo SSH remote tail:
>>"%PACKAGE_DIR%\README.txt" echo   File -^> Open URL, then enter:
>>"%PACKAGE_DIR%\README.txt" echo   ssh://user@host/var/log/application.log
>>"%PACKAGE_DIR%\README.txt" echo.
>>"%PACKAGE_DIR%\README.txt" echo Windows Credential Manager target:
>>"%PACKAGE_DIR%\README.txt" echo   klogg-ssh:user@host
>>"%PACKAGE_DIR%\README.txt" echo Create it with:
>>"%PACKAGE_DIR%\README.txt" echo   cmdkey /generic:"klogg-ssh:user@host" /user:user /pass:^<password^>
>>"%PACKAGE_DIR%\README.txt" echo.
>>"%PACKAGE_DIR%\README.txt" echo For disposable local test servers only:
>>"%PACKAGE_DIR%\README.txt" echo   ssh://user@127.0.0.1:50023/var/log/application.log?hostKeyChecking=no
>>"%PACKAGE_DIR%\README.txt" echo This disables host key verification for that URL.
>>"%PACKAGE_DIR%\README.txt" echo.
>>"%PACKAGE_DIR%\README.txt" echo See DOCUMENTATION.md for the full manual.
>>"%PACKAGE_DIR%\README.txt" echo.
>>"%PACKAGE_DIR%\README.txt" echo This package uses the Qt runtime and Visual C++ runtime
>>"%PACKAGE_DIR%\README.txt" echo deployed next to the executables. No installation is required.
exit /b 0