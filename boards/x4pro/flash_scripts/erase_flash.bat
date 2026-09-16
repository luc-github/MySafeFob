@echo off
rem MySafeFob x4pro — erase flash complet (16 Mo)
rem Usage : erase_flash.bat [COMx]   (defaut COM5)
set PORT=%1
if "%PORT%"=="" set PORT=COM5
"C:\Espressif\tools\python\v5.5.5\venv\Scripts\esptool.exe" --chip esp32s3 --port %PORT% erase_flash
pause
