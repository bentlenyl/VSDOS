@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "ACTION=%~1"
if not defined ACTION set "ACTION=buildrun"

if /i "%ACTION%"=="help" goto :help
if /i "%ACTION%"=="clean" goto :clean
if /i "%ACTION%"=="build" goto :build
if /i "%ACTION%"=="run" goto :run
if /i "%ACTION%"=="hdd" goto :hdd
if /i "%ACTION%"=="rebuild" goto :rebuild
if /i "%ACTION%"=="buildrun" goto :buildrun
if /i "%ACTION%"=="check" goto :check
if /i "%ACTION%"=="test" goto :test
if /i "%ACTION%"=="verify" goto :verify

echo ERROR: Unknown command "%ACTION%".
call :show_usage
exit /b 2

:help
call :show_usage
exit /b 0

:show_usage
echo.
echo VeryStupidDOS - Windows build commands
echo   build.bat          Build the boot image and start QEMU
echo   build.bat build    Build the boot image only
echo   build.bat run      Boot the existing image as a floppy; use it directly
echo   build.bat hdd      Boot the existing image as an IDE hard disk
echo   build.bat rebuild  Rebuild the boot image without starting QEMU
echo   build.bat check    Build and validate the boot image
echo   build.bat test     Build, validate, and run the staged image in QEMU
echo   build.bat verify   Build and run automated QEMU checks
echo   build.bat clean    Remove generated build files
echo   build.bat help     Show this help
echo.
echo Requires NASM, LLVM Clang/LLD, and QEMU for run, test, and verify.
echo Build creates a fresh FAT12 image; back up user files before rebuilding.
echo WRITE creates or overwrites an 8.3 file with inline text; DEL, REN, DIR, TYPE, MD, RD, and CD support nested 8.3 paths.
echo The build builds the resident COMMAND.APP from src\shell and installs AUTOEXEC.BAT.
echo.
exit /b 0

:rebuild
call :build_image
if errorlevel 1 exit /b 1
exit /b 0

:buildrun
call :build_image
if errorlevel 1 exit /b 1
if /i "%PUBLISH_RESULT%"=="PUBLISHED" (
    call :run_qemu "build\VeryStupidDOS.img"
) else (
    call :run_qemu "build\stage\VeryStupidDOS.img"
)
exit /b %errorlevel%

:check
call :build_image stageonly
if errorlevel 1 exit /b 1
call :validate_staged_image
exit /b %errorlevel%

:test
call :build_image stageonly
if errorlevel 1 exit /b 1
call :validate_staged_image
if errorlevel 1 exit /b 1
call :run_qemu "build\stage\VeryStupidDOS.img"
exit /b %errorlevel%

:verify
call :build_image stageonly
if errorlevel 1 exit /b 1
call :validate_staged_image
if errorlevel 1 exit /b 1
set "QEMU_VERIFY="
where qemu-system-i386 >nul 2>nul
if not errorlevel 1 set "QEMU_VERIFY=qemu-system-i386"
if not defined QEMU_VERIFY (
    where qemu-system-x86_64 >nul 2>nul
    if not errorlevel 1 set "QEMU_VERIFY=qemu-system-x86_64"
)
if not defined QEMU_VERIFY (
    echo ERROR: Automated verification requires QEMU on PATH.
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\verify.ps1" -ImagePath "build\stage\VeryStupidDOS.img" -QemuPath "%QEMU_VERIFY%"
if errorlevel 1 (
    echo ERROR: Automated QEMU verification failed.
    exit /b 1
)
exit /b 0

:validate_staged_image
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\check_image.ps1" -StageDirectory "build\stage" -ImagePath "build\stage\VeryStupidDOS.img"
if errorlevel 1 (
    echo ERROR: Boot image validation failed.
    exit /b 1
)
exit /b 0

:build
call :build_image
if errorlevel 1 exit /b 1
exit /b 0

:run
if not exist "build\VeryStupidDOS.img" (
    call :build_image
    if errorlevel 1 exit /b 1
)
call :run_qemu "build\VeryStupidDOS.img"
exit /b %errorlevel%

:build_image
echo.
echo [1/11] Checking build tools...
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

if not exist "build" mkdir "build"
if errorlevel 1 (
    echo ERROR: Could not create the build directory.
    exit /b 1
)
if not exist "build\stage" mkdir "build\stage"
if errorlevel 1 (
    echo ERROR: Could not create the build staging directory.
    exit /b 1
)

echo [2/11] Compiling COMMAND.APP...
clang --target=i386-unknown-none-elf -m32 -march=i386 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -I "src\shell" -c "src\shell\shell.c" -o "build\stage\shell.o"
if errorlevel 1 exit /b 1
nasm -f elf32 "src\shell\syscall.asm" -o "build\stage\shell_syscall.o"
if errorlevel 1 exit /b 1
ld.lld -m elf_i386 -T "sdk\linker.ld" --oformat=binary "build\stage\shell_syscall.o" "build\stage\shell.o" -o "build\stage\COMMAND.APP"
if errorlevel 1 exit /b 1
echo [3/11] Compiling the freestanding C kernel...
clang --target=i386-unknown-none-elf -m32 -march=i386 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -c "src\kernel\kernel.c" -o "build\stage\kernel.o"
if errorlevel 1 (
    echo ERROR: Kernel compilation failed.
    exit /b 1
)

echo [4/11] Compiling interrupt and timer support...
clang --target=i386-unknown-none-elf -m32 -march=i386 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -c "src\kernel\interrupts.c" -o "build\stage\interrupts_c.o"
if errorlevel 1 (
    echo ERROR: Interrupt support compilation failed.
    exit /b 1
)

echo [5/11] Compiling disk, ATA, floppy, and FAT12 drivers...
clang --target=i386-unknown-none-elf -m32 -march=i386 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -c "src\drivers\ata.c" -o "build\stage\ata.o"
if errorlevel 1 (
    echo ERROR: ATA disk driver compilation failed.
    exit /b 1
)
clang --target=i386-unknown-none-elf -m32 -march=i386 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -c "src\drivers\disk.c" -o "build\stage\disk.o"
if errorlevel 1 (
    echo ERROR: Disk backend compilation failed.
    exit /b 1
)
clang --target=i386-unknown-none-elf -m32 -march=i386 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -c "src\drivers\floppy.c" -o "build\stage\floppy.o"
if errorlevel 1 (
    echo ERROR: Floppy-controller driver compilation failed.
    exit /b 1
)
clang --target=i386-unknown-none-elf -m32 -march=i386 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -I "src\drivers" -c "src\drivers\ata_entry.c" -o "build\stage\ata_entry.o"
if errorlevel 1 exit /b 1
clang --target=i386-unknown-none-elf -m32 -march=i386 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -I "src\drivers" -c "src\drivers\floppy_entry.c" -o "build\stage\floppy_entry.o"
if errorlevel 1 exit /b 1
ld.lld -m elf_i386 -Ttext=0x00030000 --entry=driver_entry --oformat=binary "build\stage\ata_entry.o" "build\stage\ata.o" -o "build\stage\ATA.DRV"
if errorlevel 1 (
    echo ERROR: ATA.DRV external driver linking failed.
    exit /b 1
)
ld.lld -m elf_i386 -Ttext=0x00031000 --entry=driver_entry --oformat=binary "build\stage\floppy_entry.o" "build\stage\floppy.o" -o "build\stage\FLOPPY.DRV"
if errorlevel 1 (
    echo ERROR: FLOPPY.DRV external driver linking failed.
    exit /b 1
)
clang --target=i386-unknown-none-elf -m32 -march=i386 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -c "src\kernel\filesystem.c" -o "build\stage\filesystem.o"
if errorlevel 1 (
    echo ERROR: FAT12 filesystem compilation failed.
    exit /b 1
)

echo [6/11] Assembling the kernel entry point...
nasm -f elf32 "src\kernel\entry.asm" -o "build\stage\entry.o"
if errorlevel 1 (
    echo ERROR: Kernel entry-point assembly failed.
    exit /b 1
)

echo [7/11] Assembling interrupt handlers...
nasm -f elf32 "src\kernel\interrupts.asm" -o "build\stage\interrupts.o"
if errorlevel 1 (
    echo ERROR: Interrupt-handler assembly failed.
    exit /b 1
)

echo [8/11] Assembling the FAT12-aware BIOS loader...
nasm -f bin "src\boot\stage2.asm" -o "build\stage\stage2.bin"
if errorlevel 1 (
    echo ERROR: Second-stage BIOS loader assembly failed.
    exit /b 1
)
for %%F in ("build\stage\stage2.bin") do if not "%%~zF"=="4096" (
    echo ERROR: Second-stage BIOS loader must be exactly 4096 bytes.
    exit /b 1
)

echo [9/11] Linking the flat kernel image...
ld.lld -m elf_i386 -T "src\kernel\linker.ld" --oformat=binary "build\stage\entry.o" "build\stage\interrupts.o" "build\stage\interrupts_c.o" "build\stage\disk.o" "build\stage\filesystem.o" "build\stage\kernel.o" -o "build\stage\kernel.bin"
if errorlevel 1 (
    echo ERROR: Kernel linking failed.
    exit /b 1
)

echo [10/11] Assembling the FAT12 BIOS boot sector...
nasm -f bin "src\boot\boot.asm" -o "build\stage\boot.bin"
if errorlevel 1 (
    echo ERROR: Bootloader assembly failed.
    exit /b 1
)
for %%F in ("build\stage\boot.bin") do if not "%%~zF"=="512" (
    echo ERROR: The boot sector must be exactly 512 bytes.
    exit /b 1
)

echo [11/11] Formatting FAT12 and installing kernel, COMMAND.APP, and AUTOEXEC.BAT...
copy /y "src\shell\AUTOEXEC.BAT" "build\AUTOEXEC.BAT" >nul
copy /y "src\shell\AUTOEXEC.BAT" "build\stage\AUTOEXEC.BAT" >nul
if errorlevel 1 (
    echo ERROR: Could not copy AUTOEXEC.BAT into the build folders.
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\make_image.ps1" -StageDirectory "build\stage" -OutputPath "build\stage\VeryStupidDOS.img" -ShellPath "build\stage\COMMAND.APP" -AutoexecPath "build\stage\AUTOEXEC.BAT" -AtaDriverPath "build\stage\ATA.DRV" -FloppyDriverPath "build\stage\FLOPPY.DRV"
if errorlevel 1 (
    echo ERROR: Disk image creation failed.
    exit /b 1
)
if /i "%~1"=="stageonly" goto :image_ready
set "PUBLISH_RESULT="
for /f "usebackq delims=" %%R in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $candidate='build\stage\VeryStupidDOS.publish.img'; $backup='build\stage\VeryStupidDOS.previous.img'; [IO.File]::Copy('build\stage\VeryStupidDOS.img',$candidate,$true); try { if ([IO.File]::Exists('build\VeryStupidDOS.img')) { if ([IO.File]::Exists($backup)) { [IO.File]::Delete($backup) }; [IO.File]::Replace($candidate,'build\VeryStupidDOS.img',$backup) } else { [IO.File]::Move($candidate,'build\VeryStupidDOS.img') } } catch [IO.IOException] { if ([IO.File]::Exists($candidate)) { [IO.File]::Delete($candidate) }; Write-Output 'STAGED_ONLY'; exit 0 }; if ([IO.File]::Exists($backup)) { [IO.File]::Delete($backup) }; Write-Output 'PUBLISHED'"`) do set "PUBLISH_RESULT=%%R"
if errorlevel 1 (
    echo ERROR: Could not publish the boot image.
    exit /b 1
)
if /i "%PUBLISH_RESULT%"=="STAGED_ONLY" (
    echo WARNING: The published image is in use and was left unchanged.
    echo The new image is ready at build\stage\VeryStupidDOS.img.
    echo Use build.bat test to run it, or close QEMU before publishing it.
    goto :image_ready
)
if /i not "%PUBLISH_RESULT%"=="PUBLISHED" (
    echo ERROR: The build did not report a successful image publish.
    exit /b 1
)

:image_ready
echo.
if /i "%~1"=="stageonly" (
    echo Staged build complete: build\stage\VeryStupidDOS.img
) else if /i "%PUBLISH_RESULT%"=="STAGED_ONLY" (
    echo Build complete: new image staged; published image unchanged.
) else (
    echo Build complete: build\VeryStupidDOS.img
)
exit /b 0

:run_qemu
set "SOURCE_IMAGE=%~1"
if not defined SOURCE_IMAGE set "SOURCE_IMAGE=build\VeryStupidDOS.img"
set "QEMU="
where qemu-system-i386 >nul 2>nul
if not errorlevel 1 set "QEMU=qemu-system-i386"
if not defined QEMU (
    where qemu-system-x86_64 >nul 2>nul
    if not errorlevel 1 set "QEMU=qemu-system-x86_64"
)
if not defined QEMU (
    echo ERROR: QEMU was not found on PATH.
    echo Add QEMU to PATH, then run build.bat run again.
    exit /b 1
)

echo Starting VeryStupidDOS from the persistent floppy image in QEMU.
echo Close QEMU to exit; the original image is attached directly.
"%QEMU%" -machine pc -m 32M -drive "file=%SOURCE_IMAGE%,format=raw,if=floppy,index=0" -boot a -serial stdio -no-reboot -no-shutdown
exit /b %errorlevel%

:hdd
if not exist "build\VeryStupidDOS.img" (
    call :build_image
    if errorlevel 1 exit /b 1
)
call :run_hdd_qemu "build\VeryStupidDOS.img"
exit /b %errorlevel%

:run_hdd_qemu
set "SOURCE_IMAGE=%~1"
set "QEMU="
where qemu-system-i386 >nul 2>nul
if not errorlevel 1 set "QEMU=qemu-system-i386"
if not defined QEMU (
    where qemu-system-x86_64 >nul 2>nul
    if not errorlevel 1 set "QEMU=qemu-system-x86_64"
)
if not defined QEMU (
    echo ERROR: QEMU was not found on PATH.
    exit /b 1
)
echo Starting VeryStupidDOS with the persistent IDE hard-disk image in QEMU.
"%QEMU%" -machine pc -m 32M -drive "file=%SOURCE_IMAGE%,format=raw,if=ide,index=0,media=disk" -boot c -serial stdio -no-reboot -no-shutdown
exit /b %errorlevel%

:clean
if exist "build" (
    rmdir /s /q "build"
    if errorlevel 1 (
        echo ERROR: Could not remove the build directory.
        exit /b 1
    )
)
echo Generated build files removed.
exit /b 0
