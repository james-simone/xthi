# My custom xthi code

I did some ARCHER2 training recently, and they used some example code from Cray
(I think) called **xthi** to show the CPU affinity of each process/thread,
which is really handy for playing with running MPI & OpenMP codes using job
schedulers (e.g. Slurm) or low-level CPU affinity controls.

I ended up having a bit of a play with the original code to add in some
additional information and produce a slightly nicer output, and my version
included here is effectively a complete rewrite, though it uses the same
helper function from util-linux (see below).

This code has been packaged up for the SoPA Ubuntu Linux platform.
See: https://git.ecdf.ed.ac.uk/sopacst/ubuntu/xthi

## Compiling

I've provided a simple `Makefile` for building this on the SoPA Ubuntu Linux platform.
(This is also used to generate the corresponding Debian package.)

I've also provided a `CMakeLists.txt` for using this with CMake.

Some of the CPU affinity & NUMA functionality uses specific Linux & glibc functions.
I've tried to delimit these with C pre-processor conditionals, so the code should
build OK on macOS, albeit with reduced reporting capability.

## Original Sources

The original code was obtained from:
https://raw.githubusercontent.com/Wildish-LBL/SLURM-demo/master/xthi.c

It's mentioned in Cray's documentation at:
https://pubs.cray.com/bundle/CLE_User_Application_Placement_Guide_CLE52UP04_S-2496/page/Run_an_OpenMP_Application.html

The codes all incorporate a helper function to nicely format
CPU affinity, taken from:
https://github.com/karelzak/util-linux

## License

I'm going to go with GPL 2.0 for my version of the code.

None of the original xthi codes came with an obviously visible license
so I'm not sure of their original license.

The util-linux code is GPL 2.0.
