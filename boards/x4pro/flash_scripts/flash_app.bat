@echo off
rem MySafeFob x4pro — flash APP seule (firmware -> app0, otadata conserve)
rem Boucle dev rapide. Usage : flash_app.bat [COMx]   (defaut COM5)
set PORT=%1
if "%PORT%"=="" set PORT=COM5
"C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe" "%~dp0..\..\..\tools\flash_scripts\flash_mgr.py" --variant x4pro_app --app-only --port %PORT%
pause
