# Concurrent Parallel and Distributed Programming Fall 2019 Final Project

Final project for CPD (formerly Advanced Unix Programming).  A partial re-implementation of GNU Parallel in (mostly) standards compliant Ansi C.

## About Project

We support a subset of GNU Parallel functionality.  That is this utility is used to run batch jobs or replace shell for-loops invoking the same command with differing arguments with a simple invocation.

As can be seen below, one or more data sets are taken as input and substituted into the given command.   Each combination of input values from the sets (element of cartesian product) is used to generate a job.

Parallel then runs these generated jobs as parallel processes up to --jobs at a time.  The GNU Parallel manual confusingly refers to these as "threads", as this is essentially a thread-pool model.

### Build
Option 1: make (original development tool)

Option 2: cmake (added for CLion compatibility, not tested)

### Supported Command Line Options
-j / --jobs #  Number of "threads" in pool.  Defaults to maximum (only intentionally non-compliant code in the project)

--dryrun  Output all generated job strings, don't actually run them.

### General Form
parallel [options] [command components] ::: data set 1 ::: data set 2

Options must come first.

All space separated text between the last recognized leading option and the first ::: are interpreted as the command.

Default command is "echo"

Substitution patterns supported: /., /, //, ., #, %

### Example Runs

* `parallel --jobs 2 echo ::: a b c d`
* `parallel -j2 echo "Hello {2} {1}" ::: a b c ::: 1 2 3`
* `parallel -j4 ./fib ::: 100 50 10`
* ``parallel  --dryrun echo '{#} {%} {} --> {.} && {/} && {/.} && {//}' ::: \`ls ./*\` ``

### Debugging Info
We found debugging this project to be tedious until we discovered .gdbinit scripting.  Examine the file and use it to automatically open parallel with desired command, set break points and select which process to follow on forking.

### Restrictions
See restrictions.txt for more information.

* No Perl hooks:
    * "{= <perl expression =}" syntax is not supported.
* Only general commands/scripts
    * no bsh/csh functions or other hacks
    * "sleep {} ; echo {}" not supported, ";" is part of bash
* On some shells (notably csh), special characters and variables need to be surrounded by {} to prevent the shell expanding them
* Command line only data sources, no input files.
    * ::: only, no :::: or explicit file selection
* Maximum of 8 data sources
    * Missing 9th is a small bug / oversight
* Size of each source must match, no "rollover" or default value when one is exhausted


We believe the expressive power of parallel is lessened by dropping bash and perl support, but we believe that many of the same set of commands can still be run.  They must be made explicit by coding them into script files that are executed, as apposed to ad-hoc one-liners.

## Class Requirements
Project was required to perform one of the following:
* Implement a Unix utility
* Multi-process or parallel programming
* Implement a network utility, or use sockets (or other networking mechanism) to implement some other program

This program, had it been completed, would have satisfied all three requirements.  With ssh based distributed jobs satisfying the third requirement.

### Results / Incomplete Work
The end goal of this project was to re-implement part of GNU Parallel, including distributing jobs to local machines via ssh, using nothing but Ansi C.

Partly this was to prove the thesis that GNU Parallel could have been developed in the 1980's instead of 2007.  That is as soon as multi-processor systems were available (and ssh/rsh developed for the distributed component)

We succeeded in implementing a subset of GNU Parallel command line parsing, and local execution.  A few more modifications and setting up local distributed programming would have been relatively easy.

We only needed to modify command parsing to produce a single concrete_command string when running sh/ssh with execvp() instead of the vector of strings currently used.  That create a new vector with the shell and "-c" as the initial arguments, and the old vector condensed to a single string.

This would also require properly handling quotes and setting up ssh for passwordless login (generating security keys and installing them properly)

Before attempting this the code must be thoroughly examined with valgrind to find memory leaks.  All attempts to perform the above were stymied by odd memory corruption.  Current memory access appears to work fine on x86_64 Linux.

See the numerous TODO items in parallel.c for other work that could be easily rewritten and expanded to create an experience more like real GNU Parallel.