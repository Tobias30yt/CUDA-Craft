@echo off
setlocal
pushd "%~dp0"
"%~dp0build\cuda\cuda-craft.exe" --generator climate --world "%~dp0world-climate" %*
set "CUDA_CRAFT_EXIT=%errorlevel%"
popd
if not "%CUDA_CRAFT_EXIT%"=="0" (
  echo CUDA Craft stopped with an error.
  pause
  exit /b 1
)
