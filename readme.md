# Pawn 3.2(+)

This is a modified version of the Pawn 3.2 toolkit by ITB CompuPhase.

The main goal of this project is to provide an improved version of Pawn 3.2 with changes made to both the compiler and the runtime.

The codebase is based off of the [Community fork of Pawn 3.2](https://github.com/pawn-lang/compiler) (also known as "Pawn 3.10") which is aimed at fixing and adding new features to the compiler for [SA-MP](http://sa-mp.com/).
This project, in turn, is focused on improvement of the runtime components - this includes bugfixes and backporting functionality from Pawn 3.3, as well as adding completely new features (such as much more thorough bytecode verification and a new unified interpreter core).


## Changes

See [changes.md](changes.md) for the list of notable changes.


## Compiling

Building is done using CMake. The `CMakeLists.txt` file is located in the root directory of this repository and covers both the interpreter and (optionally) the compiler.  
Options:
| Option name                | Description                                                   | Default value |
|----------------------------|---------------------------------------------------------------|---------------|
|`PAWN_BUILD_PAWNCC`         | Build Pawn compiler                                           | `ON`          |
|`PAWN_BUILD_PAWNRUN`        | Build the example run-time program and the debugger           | `ON`          |
|`PAWN_USE_64BIT_CELLS`      | Use 64-bit cells (uses 32-bit cells otherwise)                | `OFF`         |
|`PAWN_USE_NEW_AMXEXEC`      | Use the new interpreter core                                  | `ON`          |
|`PAWN_AMX_EXTS_STATIC`      | Build AMX extension modules as static libraries               | `ON`          |
|`PAWN_AMX_EXT_<name>`       | Allows to enable/disable specific extension modules           | `ON`          |
|`PAWN_AMX_EXT_CONSOLE_IDLE` | Enable `@keypressed()` callback in extension module `Console` | `ON`          |
|`PAWN_AMX_EXT_TIME_IDLE`    | Enable `@timer()` callback in extension module `Time`         | `ON`          |

If you want to embed Pawn into your project, just include the root directory of this repository as a subdirectory in your project's CMake script.


## License

The project is licensed under the zlib license, with small portions of code released under other licenses that don't require binary attribution.
See file [license.txt](license.txt) for details.
