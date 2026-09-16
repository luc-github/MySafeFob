@echo off
rem MySafeFob x4pro — RESCUE : factory + bootloader hook + partitions +
rem otadata zero -> boot GARANTI sur le menu recovery
rem Usage : flash_factory.bat [COMx]   (defaut COM5)
set PORT=%1
if "%PORT%"=="" set PORT=COM5
"C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe" "%~dp0..\..\..\tools\flash_scripts\flash_mgr.py" --variant x4pro_factory --port %PORT%
pause
