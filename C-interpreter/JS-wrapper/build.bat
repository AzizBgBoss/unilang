@echo off
cd /d "%~dp0"
where emcc >nul 2>nul
if errorlevel 1 (
  if defined EMSDK (
    call "%EMSDK%\emsdk_env.bat" >nul
  )
)
python build.py || py build.py
pause
