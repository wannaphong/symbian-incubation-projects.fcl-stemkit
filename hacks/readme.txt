Assorted Hacks

1. syborg_keyboard.cpp

Replace sf\adaptation\qemu\baseport\syborg\syborg_keymap.cpp with this file 
and rebuild the baseport, to turn F10 into the Key of Death.

Pressing F10 will cause Kern::Fault("KeyOfDeath", 0x0f100f10), which will drop
you into the kernel crash debugger if you are using a UDEB version of ekern.exe

