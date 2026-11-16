@echo off
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CUDA_CRAFT_VS=%%i"
if not defined CUDA_CRAFT_VS (
  echo Visual Studio C++ Build Tools were not found.
  exit /b 1
)
call "%CUDA_CRAFT_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b 1
pushd "%~dp0.."
cmake --preset cuda-local
if not "%errorlevel%"=="0" goto failed
cmake --build --preset cuda-local
if not "%errorlevel%"=="0" goto failed
ctest --preset cuda-local
if not "%errorlevel%"=="0" goto failed
popd
exit /b 0
:failed
popd
exit /b 1
