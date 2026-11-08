@echo off
setlocal EnableExtensions
rem Runs the unit tests in test/ and waits for a key press before closing.
rem Uses PlatformIO when GCC is on PATH, otherwise Visual Studio's MSVC.

cd /d "%~dp0"
set "RESULT=1"

set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
if not exist "%PIO%" set "PIO=pio"

where g++ >nul 2>&1
if errorlevel 1 goto use_msvc

"%PIO%" test -e native
set "RESULT=%errorlevel%"
goto finish

:use_msvc
set "UNITY=.pio\libdeps\native\Unity\src"
if exist "%UNITY%\unity.c" goto find_msvc

rem PlatformIO downloads Unity before it needs the missing GCC.
echo Downloading the Unity test framework...
"%PIO%" test -e native >nul 2>&1
if exist "%UNITY%\unity.c" goto find_msvc
echo ERROR: could not download Unity through PlatformIO.
goto finish

:find_msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_compiler

set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto no_compiler
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto no_compiler

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
rem vcvars64.bat may change the current directory.
cd /d "%~dp0"

set "OUT=.pio\build\msvc_tests"
if not exist "%OUT%" mkdir "%OUT%"

set "FAILED=0"
for /d %%t in (test\test_*) do call :run_suite "%%~nxt"
set "RESULT=%FAILED%"
goto finish

:run_suite
echo.
echo === %~1 ===
cl /nologo /EHsc /std:c++17 /Isrc "/I%UNITY%" "/Fo%OUT%\\" "/Fe%OUT%\%~1.exe" "%UNITY%\unity.c" "test\%~1\test_main.cpp" > "%OUT%\%~1.log" 2>&1
if not errorlevel 1 goto suite_built
type "%OUT%\%~1.log"
echo COMPILE ERROR in %~1
set /a FAILED+=1
exit /b

:suite_built
"%OUT%\%~1.exe"
if errorlevel 1 set /a FAILED+=1
exit /b

:no_compiler
echo ERROR: no C++ compiler found. Install Visual Studio with the C++ workload,
echo or MSYS2/MinGW with g++ on PATH.
goto finish

:finish
echo.
if "%RESULT%"=="0" goto passed
echo ========== TESTS FAILED ==========
goto wait

:passed
echo ========== ALL TESTS PASSED ==========

:wait
echo.
pause
exit /b %RESULT%
