@echo off
setlocal EnableExtensions
title ODFF 0.2.0 Release Build

cd /d "%~dp0.."
if errorlevel 1 (
    echo ERROR: Could not enter the ODFF project directory.
    goto :failed
)

if not exist "CMakeLists.txt" (
    echo ERROR: CMakeLists.txt was not found in:
    echo %CD%
    echo.
    echo Keep this batch file inside the ODFF\tools folder.
    goto :failed
)

set "CMAKE_EXE="
where cmake.exe >nul 2>nul
if not errorlevel 1 set "CMAKE_EXE=cmake.exe"

if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not defined CMAKE_EXE (
    echo ERROR: CMake was not found.
    echo.
    echo Install CMake, or add cmake.exe to PATH, then run this file again.
    goto :failed
)

echo ODFF source: %CD%
echo CMake: "%CMAKE_EXE%"
echo.

"%CMAKE_EXE%" -S "%CD%" -B "%CD%\build-0.2.0" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo.
    echo ERROR: CMake configuration failed.
    goto :failed
)

"%CMAKE_EXE%" --build "%CD%\build-0.2.0" --config Release --parallel
if errorlevel 1 (
    echo.
    echo ERROR: Compilation failed.
    goto :failed
)

if not exist "%CD%\build-0.2.0\Standalone\ODFF.exe" (
    echo.
    echo ERROR: The build completed but ODFF.exe was not found.
    goto :failed
)

echo.
echo ODFF 0.2.0 was built successfully.
echo.
echo Executable:
echo %CD%\build-0.2.0\Standalone\ODFF.exe
echo.
pause
exit /b 0

:failed
echo.
echo The build did not complete. The error is shown above.
echo Copy the complete output if you need help with the remaining failure.
echo.
pause
exit /b 1
