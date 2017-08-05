# Pawn 3.2(+)

This is a modified version of the Pawn 3.2 toolkit by ITB CompuPhase.

The main goal of this project is to provide an improved version of Pawn 3.2 with changes made to both the compiler and runtime components.

The codebase is based off of the [Zeex's fork of Pawn 3.2](https://github.com/Zeex/pawn) (also known as Pawn 3.10) which is aimed at fixing and adding new features to the compiler for [SA-MP](http://sa-mp.com/).
This project, on the other hand, is focused on improvement of the runtime - some of the changes are backported from Pawn 3.3 (like the addition of `frename()`, `fstat()`, `strcopy()` etc.) and the others are completely new (such as bytecode verification and a new interpreter core).


## Changes:
  * Backported non-breaking changes from Pawn 3.3 and pre-release versions up to 4.0.
    <br/>Notable changes include the addition of new native functions in modules amxFile (`frename`, `fstat`, `fattrib` and `filecrc`), amxTime (`settimestamp` and `cvttimestamp`) and amxString (`strcopy`).
    There are also fixes for already existing functions such as `valstr` (fixed hangup on large numbers) and `printf` (fixed handling of "%%").
    Backporting from 4.0 doesn't seem to be possible due to a different license (Apache 2.0).

  * Added custom CMake project file to the repository root directory for building both the compiler and the runtime.
    <br/>Also changed the project file in the "source/amx" directory, added options for compiling with 32/64-bit cell size, building the extension modules as static libraries (might be useful to link them into the host app), and for inclusion/exclusion of particular modules from the build.

  * Added custom interpreter core (`amx_Exec`) which merges two standard cores (the GCC-specific one and the ANSI C one) into one codebase.
    <br/>The new core has additional runtime checks that fix various bugs and vulnerabilities in the interpreter's code.
    <br/>For example, in the original Pawn 3.2 most of the memory (stack/heap and data sections) access instructions don't have any address checks, which allows to read/write data outside of the script memory.
    <br/>There are also no checks for stack overflow/underflow in any of the stack operations (other than the `STACK` opcode), so it's possible to use `SCTRL 4` to make `CIP` point outside of the script memory and then read/write data from there with `PUSH*`/`POP*` instructions.
    <br/>These two and a few other kinds of vulnerabilities are fixed in the new core.
    The old cores can still be used by disabling the PAWN_USE_NEW_AMXEXEC option in the CMake project.

    Despite the extra runtime checks, the new core doesn't have any problems with performance. Moreover, it has even better performance than in the old cores (tested on machines with the x86 family processors; on ARMv8 the difference is not really noticeable).
    The performance increase became possible by the use of static branch prediction (see macros `AMX_LIKELY` and `AMX_UNLIKELY` in amx_internal.h) and various other code optimization techniques.
    You can test it yourself by building two different versions of the VM, with PAWN_USE_NEW_AMXEXEC enabled/disabled, and then compiling script "fib_bench.p" (located in the "examples" directory) and running it on each of them.

  * Added bytecode verification at script loading (in amx_Init).
    <br/>This includes address checks for branch/memory access instructions and opcode-specific checks (e.g. function ID check for the 1'st argument of SYSREQ.c and SYSREQ.n).

  * Removed the dependency of the cell size from a pointer size.
    <br/>Now it's possible to use the runtime with 32-bit cell size on 64-bit processors.
    In the original version of Pawn 3.2 doing so wasn't a good idea since the physical addresses of native functions and AMX extension modules were stored in Pawn cells and the cell size could not be enough to fit a pointer.

  * Implemented relocation of data addresses in data access instructions.
    <br/>In the original Pawn 3.2 code the jump addresses in branch instructions (`CALL`, `JUMP`, `JZER`, `JNZ`, `JEQ` etc.) are replaced by the corresponding physical addresses on script load, so why not do the same thing to speed up access to the data section?
    <br/>This feature is only supported for the new interpreter core (see above).
