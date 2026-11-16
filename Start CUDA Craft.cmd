@echo off
setlocal
pushd "%~dp0"
if not exist "%~dp0build\cuda\cuda-craft.exe" (
  echo CUDA Craft has not been built. Run tools\build-cuda.cmd first.
  pause
  exit /b 1
)
"%~dp0build\cuda\cuda-craft.exe" %*
set "CUDA_CRAFT_EXIT=%errorlevel%"
popd
if not "%CUDA_CRAFT_EXIT%"=="0" (
  echo CUDA Craft stopped with an error.
  pause
  exit /b 1
)
exit /b 0
