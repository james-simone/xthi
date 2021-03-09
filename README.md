# My custom xthi code

I did some ARCHER2 training recently, and they used some example code from Cray
(I think) called **xthi** to show the CPU affinity of each process/thread,
which is really handy for playing with running MPI & OpenMP codes using job
schedulers (e.g. Slurm) or low-level CPU affinity controls.

I ended up having a bit of a play with the original code to add in some
additional information and produce a slightly nicer output, and my version
included here is effectively a complete rewrite, though it uses the same
helper function from util-linux (see below).

## Sources

The original code was downloaded from:
https://raw.githubusercontent.com/Wildish-LBL/SLURM-demo/master/xthi.c

It's mentioned in Cray's documentation at:
https://pubs.cray.com/bundle/CLE_User_Application_Placement_Guide_CLE52UP04_S-2496/page/Run_an_OpenMP_Application.html

The codes all incorporate a helper function to nicely format
CPU affinity, taken from:
https://github.com/karelzak/util-linux

## License

None of the original xthi codes came with a license so I'm not sure
of its license.

The util-linux code is GPL 2.0.

