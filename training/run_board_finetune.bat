@echo off
setlocal
set "PY=python"
set "ROOT=%~dp0"
set "BOARD=%ROOT%input_board"
set "BASE=%ROOT%output\dataset.npz"
set "OUT=%ROOT%output_board"
where %PY% >nul 2>nul || goto :nopython
if not exist "%BASE%" goto :missing
if not exist "%BOARD%\V" goto :missing
if not exist "%BOARD%\OPEN" goto :missing
if not exist "%BOARD%\FIST" goto :missing
"%PY%" "%ROOT%merge_board_masks.py" "%BASE%" "%BOARD%" "%OUT%\dataset_mixed.npz" || goto :fail
"%PY%" "%ROOT%train_tinycnn.py" "%OUT%\dataset_mixed.npz" "%OUT%\model" --epochs 20 --final-variant foreground || goto :fail
"%PY%" "%ROOT%export_tinycnn.py" "%OUT%\model\tiny_gesture_float.pt" "%OUT%\dataset_mixed.npz" "%OUT%\deploy" || goto :fail
"%PY%" "%ROOT%verify_export.py" "%OUT%\deploy" || goto :fail
echo DONE: %OUT%\deploy\tiny_gesture_weights.h
pause
exit /b 0
:nopython
echo Python was not found. Install Python and dependencies first.
pause
exit /b 3
:missing
echo Missing output\dataset.npz or input_board\V, OPEN, FIST.
pause
exit /b 2
:fail
echo FAILED. Read the last error above.
pause
exit /b 1
