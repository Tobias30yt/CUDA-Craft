@echo off
setlocal
cd /d "%~dp0"
"C:\Program Files\WindowsApps\PythonSoftwareFoundation.Python.3.10_3.10.3056.0_x64__qbz5n2kfra8p0\python3.10.exe" tools\launch-fabric-client.py %*
if errorlevel 1 pause
