@echo off
setlocal
set "JAVA_HOME=C:\Program Files\Java\jdk-23"
if not exist "%JAVA_HOME%\bin\javac.exe" (
  echo JDK 23 not found. Set JAVA_HOME in tools\build-fabric-client.cmd to a compatible JDK.
  exit /b 1
)
set "GRADLE_USER_HOME=%~dp0..\.tools\gradle-home"
pushd "%~dp0..\fabric-client"
call "%~dp0..\.tools\gradle\gradle-9.2.1\bin\gradle.bat" --no-daemon build %*
set "CUDA_FABRIC_RESULT=%errorlevel%"
popd
exit /b %CUDA_FABRIC_RESULT%
