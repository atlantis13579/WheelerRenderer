@echo off
setlocal
set "config=%~1"
if not defined config set "config=Release"
if /I not "%config%"=="Debug" if /I not "%config%"=="Release" (
    echo Usage: Build.bat [Debug^|Release]
    exit /b 1
)
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
    echo Visual Studio 2022 with Desktop development with C++ is required.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`call "%vswhere%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "vsdir=%%i"
if not defined vsdir (
    echo Visual Studio 2022 C++ tools not found.
    exit /b 1
)
call "%~dp0GenerateProjects.bat"
if errorlevel 1 exit /b %errorlevel%
"%vsdir%\MSBuild\Current\Bin\MSBuild.exe" "%~dp0Win64\Wheeler.sln" /m /p:Configuration=%config% /p:Platform=x64 /v:minimal /nologo
exit /b %errorlevel%
