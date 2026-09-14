@echo off
REM Build the Auto Deposit plugin with MSVC x64 + Ninja.
REM Usage:  build.bat [Debug|Release]
REM
REM Visual Studio 2022 is found where it is installed, whichever edition: any of
REM Community, Professional, Enterprise or Build Tools, under either Program
REM Files root. Nothing here is tied to one machine.

setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022"
if not exist "%VSROOT%" set "VSROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2022"

set "VCVARS="
for %%E in (Community Professional Enterprise BuildTools) do if not defined VCVARS if exist "%VSROOT%\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSROOT%\%%E\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS (
	echo ERROR: no Visual Studio 2022 x64 toolchain found under "%VSROOT%".
	echo        Install the C++ workload, or call vcvars64.bat yourself and run:
	echo          cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG%
	echo          cmake --build build
	exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 (
	echo ERROR: failed to initialize the MSVC environment.
	exit /b 1
)

REM Prefer the CMake that ships with Visual Studio, fall back to whatever is on
REM PATH.
set "CMAKE="
for %%E in (Community Professional Enterprise BuildTools) do if not defined CMAKE if exist "%VSROOT%\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%VSROOT%\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE set "CMAKE=cmake"

pushd "%~dp0"
set "BUILD=build"

"%CMAKE%" -S . -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 (
	popd
	exit /b 1
)

"%CMAKE%" --build "%BUILD%"
if errorlevel 1 (
	popd
	exit /b 1
)

echo.
echo Build finished: %CD%\%BUILD%\d2rl-auto-deposit.dll
popd
endlocal
