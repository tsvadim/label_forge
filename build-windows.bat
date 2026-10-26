@echo off
rem Local Windows build: run from "x64 Native Tools Command Prompt for VS 2022".
rem Set QTDIR to your Qt installation, e.g. C:\Qt\6.8.3\msvc2022_64
rem SPDX-License-Identifier: GPL-3.0-or-later

if "%QTDIR%"=="" set QTDIR=C:\Qt\6.8.3\msvc2022_64
set PATH=%QTDIR%\bin;%PATH%

cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QTDIR%" || exit /b 1
cmake --build build || exit /b 1

if exist dist\LabelForge rmdir /s /q dist\LabelForge
mkdir dist\LabelForge
copy build\LabelForge.exe dist\LabelForge\ >nul
windeployqt --release --compiler-runtime --no-opengl-sw dist\LabelForge\LabelForge.exe || exit /b 1
copy LICENSE dist\LabelForge\ >nul
copy README.md dist\LabelForge\ >nul
xcopy /e /i /q examples dist\LabelForge\examples >nul

echo.
echo Done: dist\LabelForge\LabelForge.exe
