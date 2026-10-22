@echo off
rem Formation ghost viewer (throwaway, issue #1): the real game as retail, red wireframes = wing leaders flying the fixed behaviour, blue = the same ship as retail has it.
cd /d "%~dp0.."
set FS2_GHOST_IN=%~dp0loop2-2.fixed.txt
rem Camera follows this ship (pass another name as the first argument to change it)
if "%~1"=="" (set FS2_GHOST_CAM=Alpha 1) else (set FS2_GHOST_CAM=%~1)
.\FS2_ghost_r.exe -window -autotest -replay "%~dp0loop2-2.fs2rec"

