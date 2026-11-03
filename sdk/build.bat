@echo off
setlocal EnableExtensions
cd /d "%~dp0"

where nasm >nul 2>nul
if errorlevel 1 (
    echo ERROR: NASM was not found on PATH.
    exit /b 1
)
where clang >nul 2>nul
if errorlevel 1 (
    echo ERROR: LLVM Clang was not found on PATH.
    exit /b 1
)
where ld.lld >nul 2>nul
if errorlevel 1 (
    echo ERROR: LLVM LLD was not found on PATH.
    exit /b 1
)

if not exist build mkdir build
clang --target=i386-unknown-none-elf -m32 -march=i386 -std=c11 -O2 ^
    -Wall -Wextra -Werror -ffreestanding -fno-builtin ^
    -fno-stack-protector -fno-pic -fno-pie ^
    -fno-asynchronous-unwind-tables -fno-unwind-tables ^
    -I include -c main.c -o build\main.o
if errorlevel 1 exit /b 1
nasm -f elf32 runtime\syscall.asm -o build\syscall.o
if errorlevel 1 exit /b 1
ld.lld -m elf_i386 -T linker-main.ld --oformat=binary ^
    build\syscall.o build\main.o -o build\MAIN.APP
if errorlevel 1 exit /b 1
echo SDK build complete: sdk\build\MAIN.APP
exit /b 0
