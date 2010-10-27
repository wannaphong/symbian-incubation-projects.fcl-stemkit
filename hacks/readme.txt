Assorted Hacks

1. syborg_keyboard.cpp

Replace sf\adaptation\qemu\baseport\syborg\keyboard\syborg_keymap.cpp with this file 
and rebuild the baseport, to turn F10 into the Key of Death.

Pressing F10 will cause Kern::Fault("KeyOfDeath", 0x0f100f10), which will drop
you into the kernel crash debugger if you are using a UDEB version of ekern.exe



2. cl_file.cpp

Replace K:\sf\os\kernelhwsrv\userlibandfileserver\fileserver\sfsrv\cl_file.cpp with this file
And rebuild efsrv.mmp from \sf\os\kernelhwsrv\userlibandfileserver\fileserver\group

This will give you debug for every RFile::Open command called.


3. EComServer.mmp

In order to get logging out of the EComServer.mmp it must be rebuilt with the logging macro turned on.
Replace \sf\os\ossrv\lowlevellibsandfws\pluginfw\Framework\MMPFiles\EComServer.mmp
And rebuild EComServer.mmp from K:\sf\os\ossrv\lowlevellibsandfws\pluginfw\Group