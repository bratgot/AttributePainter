@echo off
setlocal EnableDelayedExpansion
set "NUKE_ROOT=C:\Program Files\Nuke17.0"
set "USD_ROOT="
set "BUILD_TYPE=Release"
set "JOBS=%NUMBER_OF_PROCESSORS%"
set "SCRIPT_DIR=%~dp0"
for %%i in ("%SCRIPT_DIR%..") do set "ROOT_DIR=%%~fi"
set "BUILD_DIR=%ROOT_DIR%\build"
:parse_args
if "%~1"=="" goto :after_args
if /i "%~1"=="/nuke" ( set "NUKE_ROOT=%~2" & shift & shift & goto :parse_args )
if /i "%~1"=="/usd"  ( set "USD_ROOT=%~2"  & shift & shift & goto :parse_args )
if /i "%~1"=="/type" ( set "BUILD_TYPE=%~2" & shift & shift & goto :parse_args )
if /i "%~1"=="/install" ( set "DO_INSTALL=1" & shift & goto :parse_args )
if /i "%~1"=="/clean" ( set "DO_CLEAN=1" & shift & goto :parse_args )
:after_args
if "!USD_ROOT!"=="" set "USD_ROOT=!NUKE_ROOT!\usd"
where cmake >nul 2>&1
if errorlevel 1 ( echo [ERROR] cmake not found. Get it from cmake.org & exit /b 1 )
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" ( echo [ERROR] vswhere not found. Install Visual Studio 2022. & exit /b 1 )
for /f "usebackq delims=" %%p in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath 2^>nul`) do set "VS_INSTALL=%%p"
if "!VS_INSTALL!"=="" ( echo [ERROR] No VS with C++ workload found. & exit /b 1 )
echo [info] VS: !VS_INSTALL!
call "!VS_INSTALL!\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 ( echo [ERROR] vcvarsall failed. & exit /b 1 )
echo [info] Nuke: !NUKE_ROOT!
echo [info] USD:  !USD_ROOT!
if !DO_CLEAN!==1 if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"
echo [info] Configuring...
cmake -B "!BUILD_DIR!" -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=!BUILD_TYPE! "-DNUKE_ROOT=!NUKE_ROOT!" "-DUSD_ROOT=!USD_ROOT!" "!ROOT_DIR!"
if errorlevel 1 ( echo [ERROR] Configure failed. & exit /b 1 )
echo [info] Building...
cmake --build "!BUILD_DIR!" --config !BUILD_TYPE! -- /maxcpucount:!JOBS!
if errorlevel 1 ( echo [ERROR] Build failed. & exit /b 1 )
echo [OK] Done.
for /r "!BUILD_DIR!" %%f in (AttributePainter.dll) do echo [OK] Plugin: %%f
if !DO_INSTALL!==1 cmake --install "!BUILD_DIR!" --config !BUILD_TYPE!
endlocal
exit /b 0