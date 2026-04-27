@echo off
setlocal

:: SimulationCraft Build Script
:: Usage:  build.bat [debug|release] [configure|rebuild|clean]
::   build.bat              - incremental debug build (default)
::   build.bat release      - incremental release build
::   build.bat configure    - reconfigure cmake (needed after adding new files)
::   build.bat rebuild      - clean + full rebuild
::   build.bat clean        - delete build artifacts

set BUILD_TYPE=Debug
set ACTION=build

:: Parse arguments (order-independent)
:parse_args
if "%~1"=="" goto done_args
if /i "%~1"=="release"   (set BUILD_TYPE=Release& shift & goto parse_args)
if /i "%~1"=="debug"     (set BUILD_TYPE=Debug& shift & goto parse_args)
if /i "%~1"=="configure" (set ACTION=configure& shift & goto parse_args)
if /i "%~1"=="rebuild"   (set ACTION=rebuild& shift & goto parse_args)
if /i "%~1"=="clean"     (set ACTION=clean& shift & goto parse_args)
echo Unknown argument: %~1
exit /b 1
:done_args

set BUILD_DIR=%~dp0build

:: Load MSVC environment (only if cl.exe not already available)
where cl >nul 2>&1
if errorlevel 1 (
    echo Loading MSVC toolchain...
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
    if errorlevel 1 (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
    )
    where cl >nul 2>&1
    if errorlevel 1 (
        echo ERROR: Could not find MSVC compiler. Install VS2022 BuildTools or Community.
        exit /b 1
    )
)

:: Handle actions
if "%ACTION%"=="clean" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo Done.
    exit /b 0
)

if "%ACTION%"=="rebuild" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

:: Configure if needed
if not exist "%BUILD_DIR%\build.ninja" (
    echo Configuring CMake with Ninja [%BUILD_TYPE%]...
    cmake -S "%~dp0." -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
        -DBUILD_GUI=OFF ^
        -DCMAKE_CXX_STANDARD=17
    if errorlevel 1 (
        echo ERROR: CMake configure failed.
        exit /b 1
    )
)

if "%ACTION%"=="configure" (
    echo Configure complete.
    exit /b 0
)

:: Build
echo Building simc [%BUILD_TYPE%]...
cmake --build "%BUILD_DIR%" -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo Build complete: %BUILD_DIR%\simc.exe
