@echo off
if not exist "build\VeryStupidDOS.img" call build.bat build
if errorlevel 1 exit /b 1
qemu-system-i386 -machine pc -m 32M -drive "file=build\VeryStupidDOS.img,format=raw,if=ide,index=0,media=disk" -boot c -serial stdio -no-reboot -no-shutdown
