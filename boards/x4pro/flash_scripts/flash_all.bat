@echo off
rem MySafeFob x4pro — install COMPLET (firmware + factory + bootloader hook
rem + partitions + otadata -> boot direct sur l'app)
rem Usage : flash_all.bat [COMx]   (defaut COM5)
set PORT=%1
if "%PORT%"=="" set PORT=COM5
"C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe" "%~dp0..\..\..\tools\flash_scripts\flash_mgr.py" --variant x4pro_app --port %PORT%
pause
