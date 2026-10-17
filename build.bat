@echo off
setlocal
rem Builds bin\GrannyLegacyMenu.dll and bin\injector.exe with MSVC (x64).
cd /d "%~dp0"

if defined VSCMD_VER goto :haveenv
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\18\Community"
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" echo vcvars64.bat not found in "%VSDIR%" & exit /b 1
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
:haveenv

if not exist bin mkdir bin
if not exist obj mkdir obj

if not defined OUTNAME set OUTNAME=GrannyLegacyMenu
set IMGUI=third_party\imgui
set MH=third_party\minhook
set CFLAGS=/nologo /O2 /MT /EHsc /std:c++20 /utf-8 /Zc:char8_t- /W3 /DNDEBUG /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DIMGUI_DEFINE_MATH_OPERATORS /D_CRT_SECURE_NO_WARNINGS ^
 /I src /I %IMGUI% /I %MH%\include /Fo:obj\ /Fd:obj\

set SOURCES=src\dllmain.cpp src\il2cpp.cpp src\unity.cpp src\game.cpp src\state.cpp src\config.cpp src\log.cpp ^
 src\render.cpp src\theme.cpp src\widgets.cpp src\menu.cpp src\esp.cpp src\motion.cpp src\toggles.cpp src\blur.cpp src\i18n.cpp ^
 %IMGUI%\imgui.cpp %IMGUI%\imgui_draw.cpp %IMGUI%\imgui_tables.cpp %IMGUI%\imgui_widgets.cpp ^
 %IMGUI%\backends\imgui_impl_dx11.cpp %IMGUI%\backends\imgui_impl_win32.cpp ^
 %MH%\src\buffer.c %MH%\src\hook.c %MH%\src\trampoline.c %MH%\src\hde\hde64.c

cl %CFLAGS% /LD %SOURCES% /link /OUT:bin\%OUTNAME%.dll /PDB:obj\%OUTNAME%.pdb d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /EHsc /std:c++20 /DUNICODE /D_UNICODE injector\injector.cpp /Fo:obj\ /link /OUT:bin\injector.exe
if errorlevel 1 exit /b 1

echo.
echo Build OK: bin\%OUTNAME%.dll, bin\injector.exe
