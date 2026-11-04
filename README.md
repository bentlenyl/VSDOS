# VSDOS
A 32-Bit DOS-Like OS that's somewhat inspired by Linux. Pretty much, I already disliked Linux but still wanted the cool setup that Linux has (where you can make it into pretty much any OS). So, I made this. It's DOS-like because it's WAY easier to recreate than Windows NT plus the thing I disliked about Linux is how different it is from Windows. The main function, build, and behavior is similar to DOS and Windows but with new features such as FAT-32, multitasking, networking, etc planned in the future.

# BUILD TOOLS NEEDED
[NASM](https://www.nasm.us/pub/nasm/releasebuilds/3.02 "NASM"), [LLVM Clang](https://github.com/llvm/llvm-project/releases/tag/llvmorg-18.1.8 "LLVM Clang"), [QEMU](https://sourceforge.net/app/qemu/ "QEMU").

# HOW TO BUILD
1: Download the project and extract it into a folder.
2: Install the build tools needed.
3: Run build.bat.

# HOW TO RUN IN VIRTUALBOX
You need a VM with a floppy drive. Put the built .img file into the floppy slot, and reboot.

# COMMANDS
- Help: Shows all commands.
- Ver: Prints the OS version (Pretty inaccurate).
- Cls: Clears screen.
- Echo: Repeats whatever you type after (eg. echo hello prints hello).
- Dir: Prints all files and folders in directory.
- Type: Prints contents of whatever file you type after (eg. type hello.txt prints text inside hello.txt).
- Write: Makes a file (eg. write hello.txt makes hello.txt).
- Del: Deletes a file (eg. del hello.txt deletes hello.txt).
- Ren: Renames a file (eg. ren hello.txt hello2.txt).
- Md: Makes a directory (eg. md hello makes hello as a folder).
- Rd: Removes a directory (eg. rd hello deletes hello folder).
- Cd: Changes directory that you're in (eg. cd hello takes you into hello folder).
- Time: Shows uptime.
- Mem: Prints kernel memory info.
- Panic: Makes the kernel panic (used as a test of error handling).
- Run: Loads and runs a flat 32-bit .APP from FAT12.
- Call: Runs a batch file from the shell (eg. call setup.bat).

# APPLICATION API AND SDK
The SDK and application API let you make apps for the OS. The API folder works standalone from the OS, so you don't need the OS source code to develop an app. Apps call int 0x80 API through the SDK runtime. MAIN.APP is only a standalone SDK example and is not included in normal OS builds.