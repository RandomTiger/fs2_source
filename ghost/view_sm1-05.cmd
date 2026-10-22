@echo off
rem Formation ghost viewer (throwaway, issue #1): the real game as retail, green wireframes = wing leaders flying the fixed behaviour.
cd /d "%~dp0.."
set FS2_GHOST_IN=%~dp0sm1-05.fixed.txt
.\FS2_ghost_r.exe -window -autotest -replay "%~dp0sm1-05.fs2rec"

