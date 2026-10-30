# VSDOS
A 32-Bit DOS-Like OS that's somewhat inspired by Linux. Pretty much, I already disliked Linux but still wanted the cool setup that Linux has (where you can make it into pretty much any OS). So, I made this. It's DOS-like because it's WAY easier to recreate than Windows NT plus the thing i disliked about Linux is how different it is from Windows. The main function, build, and behavior is similar to DOS and Windows but with new features such as FAT-32, multitasking, networking, etc...

# BUILD TOOLS NEEDED
[NASM](https://www.nasm.us/pub/nasm/releasebuilds/3.02 "NASM"), [LLVM Clang](https://github.com/llvm/llvm-project/releases/tag/llvmorg-18.1.8 "LLVM Clang"), [QEMU](https://sourceforge.net/app/qemu/ "QEMU").

# HOW TO BUILD
1: Download the project and extract it into a folder.
2: Install the build tools needed.
3: Run build.bat.

# HOW TO RUN IN VIRTUALBOX
You need a VM with a floppy drive. Put the built .img file into the floppy slot, and reboot.
