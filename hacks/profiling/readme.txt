Profiling:

This is a simple tool that will start the sampling profiler, wait a given period (default = 5mins) and then stop, and unload the profiler.
Add it to your rom, and to somewhere (early!) in your startup sequence to find out what's happening during a boot sequence.

Argument: -time=<n>
<n> is the number of milliseconds to wait before stopping the profiler.


You can put something like this in a startup command list:

// ---------------------------------------------------------------------------

// r_cmd_profiling

// ---------------------------------------------------------------------------

//

RESOURCE SSM_START_PROCESS_INFO r_cmd_profiling

    {

    priority = 0xFE01;

    execution_behaviour = ESsmFireAndForget;

    name = "Z:\\sys\\bin\\startupprofiling.exe";

    args = "-time=600000000";

    severity = ECmdCriticalSeverity;

    }
