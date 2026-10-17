@echo off
setlocal
rem Injects the latest bin\GrannyLegacyMenu.dll into Granny Legacy.
rem A uniquely named copy is injected (bin\loaded\), so you can rebuild while the game keeps the old one loaded.
cd /d "%~dp0"

if not exist "bin\GrannyLegacyMenu.dll" (
    echo bin\GrannyLegacyMenu.dll not found - run build.bat first.
    pause
    exit /b 1
)
if not exist "bin\loaded" mkdir "bin\loaded"

rem Remove copies from earlier sessions (the one still loaded in the game is locked and stays)
del /q "bin\loaded\*.dll" >nul 2>&1

for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set STAMP=%%t
set "COPY=bin\loaded\GrannyLegacyMenu_%STAMP%.dll"
copy /y "bin\GrannyLegacyMenu.dll" "%COPY%" >nul || (echo Copy failed & pause & exit /b 1)

rem An already injected older copy unloads itself automatically (hot reload)
"bin\injector.exe" "%COPY%"
if errorlevel 1 pause
