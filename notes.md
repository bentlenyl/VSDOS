# The Idea
The goal of my OS is to have the same "use on anything" setup that Linux uses, but be built more like Windows and MS-DOS for the people that are used to Windows instead of Linux (see README.md for more stuff about this).

# The Things That I'm Going To Hopefully Do To Make This Usable Today (not in order)
1: Add FAT-32 instead of the FAT-12 it uses right now.
2: Add drivers, kernel calls, and an app API and SDK (SDK will be Windows made, sorry not sorry.) for developers.
3: Networking? IDK. Just an idea for later.
4: Move shell to an app rather than being hard-coded into the kernel (and set up application function for this).

# Moving Shell From Kernel To An App
1: Make a basic executable format and application loading.
2: Add kernel system calls for applications such as input/output, etc (hard-coded).
3: Move shell from kernel into it's own app and make the kernel load it on boot.
4: Let the shell launch other apps whilst being an app itself.
5: Let the shell have FAT-12 control.

# Finishing FAT-12 And Adding FAT-32
1: Before step 5 on kernel shell to shell app, add write support to the FAT-12 system.
More stuff coming soon, I don't wanna get ahead of myself.