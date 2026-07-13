@echo off
setlocal

set "ROOT=%~dp0.."
set "EXE=%ROOT%\build\Standalone\ODFF.exe"

if not exist "%EXE%" (
    echo ODFF.exe was not found:
    echo %EXE%
    echo.
    echo Build Release first:
    echo cmake --build build --config Release
    exit /b 1
)

where dumpbin >nul 2>nul
if errorlevel 1 (
    echo dumpbin.exe is not available in this terminal.
    echo Run this script from a Visual Studio Developer Command Prompt.
    exit /b 1
)

echo Checking imported DLLs for:
echo %EXE%
echo.
dumpbin /dependents "%EXE%"

echo.
echo A standalone ODFF build must not list:
echo   glfw3.dll
echo   imgui.dll
echo   VCRUNTIME140.dll
echo   VCRUNTIME140_1.dll
echo   MSVCP140.dll
echo.
echo Normal Windows system DLLs are expected.
endlocal
