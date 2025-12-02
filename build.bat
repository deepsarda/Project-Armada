@echo off
REM =============================================================================
REM Armada Cross-Platform Build Script for Windows
REM =============================================================================
REM This script builds the project with all dependencies bundled.
REM No external library installation is required.
REM
REM Usage:
REM   build.bat [options]
REM
REM Options:
REM   --release       Build in release mode (default)
REM   --debug         Build in debug mode
REM   --clean         Clean build directory before building
REM   --package       Create distributable package after building
REM   --help          Show this help message
REM =============================================================================

setlocal enabledelayedexpansion

REM Default options
set BUILD_TYPE=Release
set CLEAN_BUILD=0
set CREATE_PACKAGE=0

REM Parse arguments
:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="--release" (
    set BUILD_TYPE=Release
    shift
    goto :parse_args
)
if /i "%~1"=="--debug" (
    set BUILD_TYPE=Debug
    shift
    goto :parse_args
)
if /i "%~1"=="--clean" (
    set CLEAN_BUILD=1
    shift
    goto :parse_args
)
if /i "%~1"=="--package" (
    set CREATE_PACKAGE=1
    shift
    goto :parse_args
)
if /i "%~1"=="--help" (
    echo Usage: build.bat [options]
    echo.
    echo Options:
    echo   --release       Build in release mode (default)
    echo   --debug         Build in debug mode
    echo   --clean         Clean build directory before building
    echo   --package       Create distributable package after building
    echo   --help          Show this help message
    exit /b 0
)
echo Unknown option: %~1
exit /b 1

:done_parsing

REM Get script directory
set SCRIPT_DIR=%~dp0
set BUILD_DIR=%SCRIPT_DIR%build
set DIST_DIR=%SCRIPT_DIR%dist

echo.
echo ========================================
echo   Armada Build Script for Windows
echo   Build Type: %BUILD_TYPE%
echo ========================================
echo.

REM Check for CMake
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: CMake is not installed or not in PATH.
    echo Please install CMake from https://cmake.org/download/
    exit /b 1
)

REM Check for Git
where git >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: Git is not installed or not in PATH.
    echo Please install Git from https://git-scm.com/download/win
    exit /b 1
)

REM Check for Visual Studio / MSVC
where cl >nul 2>nul
if %errorlevel% neq 0 (
    echo WARNING: MSVC compiler not found in PATH.
    echo Trying to detect Visual Studio installation...
    
    REM Try to find and run vcvars64.bat
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
    ) else (
        echo ERROR: Could not find Visual Studio installation.
        echo Please install Visual Studio with C++ development tools.
        exit /b 1
    )
    
    where cl >nul 2>nul
    if %errorlevel% neq 0 (
        echo ERROR: Failed to set up MSVC environment.
        exit /b 1
    )
)

echo Found CMake: 
cmake --version | findstr /n "^" | findstr "^1:"
echo.

REM Clean if requested
if %CLEAN_BUILD%==1 (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

REM Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM Configure with CMake
echo Configuring with CMake...
cmake .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DSTATIC_BUILD=ON
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

REM Build
echo.
echo Building...
cmake --build . --config %BUILD_TYPE% --parallel
if %errorlevel% neq 0 (
    echo ERROR: Build failed.
    exit /b 1
)

echo.
echo ========================================
echo   Build completed successfully!
echo ========================================
echo Executable: %BUILD_DIR%\%BUILD_TYPE%\armada.exe
echo.

REM Create package if requested
if %CREATE_PACKAGE%==1 (
    echo Creating distribution package...
    if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
    cpack -C %BUILD_TYPE%
    if exist *.zip move /y *.zip "%DIST_DIR%\" >nul 2>nul
    echo Package created in: %DIST_DIR%
    dir "%DIST_DIR%"
)

echo Done!
exit /b 0
