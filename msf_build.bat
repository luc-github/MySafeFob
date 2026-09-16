@echo off
rem Wrapper de build MySafeFob — contourne le blocage MSYS de idf.py 5.5
set IDF_TOOLS_PATH=C:\Espressif
set IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v5.5.5\venv
set IDF_PATH=C:\esp\v5.5.5\esp-idf
set "PATH=C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\idf-exe\1.0.3;%PATH%"
set MSYSTEM=
cd /d "%~dp0"
"C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe" "C:\esp\v5.5.5\esp-idf\tools\idf.py" %*
exit /b %errorlevel%
