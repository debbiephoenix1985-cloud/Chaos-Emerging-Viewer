@echo off

python develop.py -G vs%* -t Release configure -DUSE_MIRROR=1

echo Configuring done. Press a key to exit.
pause
