@echo off
setlocal
set "PY=python"
set "ROOT=%~dp0"
set "INPUT=%ROOT%input"
set "OUT=%ROOT%output"
where %PY% >nul 2>nul || goto :nopython
if not exist "%INPUT%\fist" goto :missing
if not exist "%INPUT%\palm" goto :missing
if not exist "%INPUT%\peace" goto :missing
if not exist "%INPUT%\minimal-annotations.json" goto :missing
"%PY%" "%ROOT%prepare_public_masks.py" "%INPUT%" "%OUT%\dataset.npz" --review "%OUT%\mask_review.png" || goto :fail
echo Open output\mask_review.png. Stop now if the masks are wrong.
pause
"%PY%" "%ROOT%train_tinycnn.py" "%OUT%\dataset.npz" "%OUT%\model" --epochs 18 --final-variant foreground || goto :fail
"%PY%" "%ROOT%export_tinycnn.py" "%OUT%\model\tiny_gesture_float.pt" "%OUT%\dataset.npz" "%OUT%\deploy" || goto :fail
"%PY%" "%ROOT%verify_export.py" "%OUT%\deploy" || goto :fail
echo DONE: %OUT%\deploy\tiny_gesture_weights.h
pause
exit /b 0
:nopython
echo Python was not found. Install Python and dependencies first.
pause
exit /b 3
:missing
echo Missing input folders or minimal-annotations.json.
pause
exit /b 2
:fail
echo FAILED. Read the last error above.
pause
exit /b 1
