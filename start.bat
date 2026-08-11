@echo off
rem eclipse-dmx: the mythos26 show, live on the rig.
rem Drives real DMX output - the widget must be attached and nothing else
rem (QLC+) may hold the port. Drop --live to watch it with no wire.

cd /d "%~dp0desktop"
set PYTHONPATH=python
python -m eclipse_dmx view config\mythos26.json --live

if errorlevel 1 (
  echo.
  echo exited with error %errorlevel%
  pause
)
