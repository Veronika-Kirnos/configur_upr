@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
set "CXX=g++"
for /f "delims=" %%C in ('where g++') do if not defined CXX_DIR set "CXX_DIR=%%~dpC"
for /f %%V in ('g++ -dumpfullversion') do set "GCC_VERSION=%%V"
set "CPPFLAGS="
rem Use matching headers if old MinGW32 files share the compiler directory.
if exist "%CXX_DIR%..\x86_64-w64-mingw32\include" (
    set CPPFLAGS=-nostdinc ^
-isystem "%CXX_DIR%..\include\c++\%GCC_VERSION%" ^
-isystem "%CXX_DIR%..\include\c++\%GCC_VERSION%\x86_64-w64-mingw32" ^
-isystem "%CXX_DIR%..\include\c++\%GCC_VERSION%\backward" ^
-isystem "%CXX_DIR%..\lib\gcc\x86_64-w64-mingw32\%GCC_VERSION%\include" ^
-isystem "%CXX_DIR%..\x86_64-w64-mingw32\include"
)
if not defined D2_EXE if exist "%LOCALAPPDATA%\Programs\D2\d2.exe" (
    set "D2_EXE=%LOCALAPPDATA%\Programs\D2\d2.exe"
)
if /i "%~1"=="test" goto test
%CXX% %CPPFLAGS% -std=c++17 -Wall -Wextra -pedantic -O2 -static src\*.cpp -municode ^
    -o build\app.exe -lwinhttp -lxmllite -lshlwapi -lole32 -lshell32
if errorlevel 1 exit /b 1
if /i "%~1"=="build" exit /b 0
build\app.exe %*
exit /b %errorlevel%
:test
%CXX% %CPPFLAGS% -std=c++17 -Wall -Wextra -pedantic -O2 -static ^
    tests\tests.cpp src\json.cpp src\config.cpp src\http.cpp src\xml.cpp src\maven.cpp src\graph.cpp src\diagram.cpp ^
    -o build\tests.exe -lwinhttp -lxmllite -lshlwapi -lole32 -lshell32
if errorlevel 1 exit /b 1
build\tests.exe
exit /b %errorlevel%
