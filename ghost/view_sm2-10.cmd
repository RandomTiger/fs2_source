@echo off
rem Formation ghost viewer (throwaway, issue #1): the real game as retail, green wireframes = wing leaders flying the fixed behaviour.
cd /d "%~dp0.."
set FS2_GHOST_IN=%~dp0sm2-10.fixed.txt
.\FS2_ghost_r.exe -window -autotest -replay "%~dp0sm2-10.fs2rec"

