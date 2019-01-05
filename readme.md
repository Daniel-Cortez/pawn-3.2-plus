# Pawn 3.2(+)

This is a modified version of the Pawn 3.2 toolkit by ITB CompuPhase.

The main goal of this project is to provide an improved version of Pawn 3.2 with changes made to both the compiler and runtime components.

The codebase is based off [Pawn 3.10](https://github.com/pawn-lang/compiler) (also referred to as "The Community Compiler"; a fork of Pawn 3.2, which is aimed at fixing bugs and adding new features to the compiler for [SA-MP](http://sa-mp.com/)) and adds various improvements to the runtime components - some of the changes are backported from Pawn 3.3 (like the addition of <code>frename()</code>, <code>fstat()</code>, <code>strcopy()</code> etc.) and the others are completely new (such as bytecode verification and a new interpreter core).


## Changes:
<ul>
<li>Backported non-breaking changes from Pawn 3.3 and reimplemented native functions from Pawn 4.
<br/>Notable changes include the addition of new functions in the following modules:
<ul>
<li>amxFile

```Pawn
native bool: frename(const oldname[], const newname[]);
native bool: fcopy(const source[], const target[]);
native bool: fcreatedir(const name[]);

native bool: fstat(name[], &size = 0, &timestamp = 0, &mode = 0, &inode = 0);
native bool: fattrib(const name[], timestamp=0, attrib=0x0f);
native       filecrc(const name[]);

native       readcfg(const filename[]="", const section[]="", const key[], value[], size=sizeof value, const defvalue[]="", bool: pack=false);
native       readcfgvalue(const filename[]="", const section[]="", const key[], defvalue=0);
native bool: writecfg(const filename[]="", const section[]="", const key[], const value[]);
native bool: writecfgvalue(const filename[]="", const section[]="", const key[], value);
native bool: deletecfg(const filename[]="", const section[]="", const key[]="");
```
</li>

<li>amxFloat

```Pawn
native Float:floatint(Float:value);
native operator=(Float:oper) = floatint;
```
</li>

<li>amxString

```Pawn
native strcopy(dest[], const source[], maxlength=sizeof dest);

native urlencode(dest[], const source[], maxlength=sizeof dest, bool:pack=false);
native urldecode(dest[], const source[], maxlength=sizeof dest, bool:pack=false);
```
</li>

<li>amxTime

```Pawn
native cvttimestamp(seconds1970, &year=0, &month=0, &day=0, &hour=0, &minute=0, &second=0);
```
</li>
</ul>
There are also fixes for already existing native functions:
<ul>
<li>valstr
<br/>Fixed hangup on large numbers.
<br/>Fixed the function being prone to a buffer overrun (added the <code>maxlength</code> argument).
</li>

<li>strcmp
<br/>Fixed incorrect return values when either one of the strings is empty or if one string partially matches the other one.
</li>

<li>strins
<br/>Fixed the function being prone to a buffer overrun (it didn't take the <code>maxlength</code> argument into account).
<br/>Fixed the resulting string not being terminated by a <code>'\0'</code> symbol in certain situations.
</li>

<li>fputchar
<br/>Fixed garbage return value when the <code>utf8</code> argument is <code>false</code>.
</li>

<li>fwrite
<br/>Fixed an issue when the last character in the string (before <code>'\0'</code>) didn't get written if the string was packed.
</li>

<li>printf
<br/>Fixed handling of "%%".
<br/>Fixed the function printing <code>cellmin</code> incorrectly.
</li>

<li>ispacked
<br/>Fixed invalid return value (0) when a packed string starts with a cell with the most significant bit set (e.g. <code>!"\128abc"</code>).
</li>

<li>strfind
<br/>Fixed out of bounds access when the search start position (the <code>index</code> argument) is negative.
</li>

<li>strdel
<br/>Fixed out of bounds access when the position of the first character to remove (the <code>start</code> argument) is negative.
</li>

<li>uuencode
<br/>Fixed invalid data encoding (the function was always returning an empty string instead of encoded data).
</li>

<li>getarg
<br/>Fixed the function allowing to read data outside of the script memory.
</li>

<li>getproperty
<br/>Fixed the function being prone to a buffer overrun (added the <code>maxlength</code> argument).
</li>

<li>strformat
<br/>Fixed the function printing <code>cellmin</code> incorrectly.
</li>
</ul>
</li>

<li>Added custom CMake project file to the repository root directory for building both the compiler and the runtime.
<br/>Also changed the project file in the "source/amx" directory, added options for compiling with 32/64-bit cell size, building the extension modules as static libraries (might be useful to link them into the host app), and for inclusion/exclusion of particular modules from the build.
</li>

<li>Added custom interpreter core (<code>amx_Exec</code>) which merges two standard cores (the GCC-specific one and the ANSI C one) into one codebase.
<br/>The new core has additional runtime checks that fix various bugs and vulnerabilities in the interpreter.
<br/>For example, in the original Pawn 3.2 most of the memory (stack/heap and data sections) access instructions don't have any address checks, which allows to read/write data outside of the script memory.
<br/>There are also no checks for stack overflow/underflow in any of the stack operations (other than the <code>STACK</code> opcode), so it's possible to use <code>SCTRL 4</code> to make <code>CIP</code> point outside of the script memory and then read/write data from there with <code>PUSH*</code>/<code>POP*</code> instructions.
<br/>These two and a few other kinds of vulnerabilities are fixed in the new core.
The old cores still can be used by disabling the <code>PAWN_USE_NEW_AMXEXEC</code> option in the CMake project.
</li>

<li>Added bytecode verification at script loading (in <code>amx_Init</code>).
<br/>This includes address checks for branch/memory access instructions and opcode-specific checks (e.g. function ID check for the 1'st argument of SYSREQ.c and SYSREQ.n).
</li>

<li>Removed the dependency of a cell size from a pointer size.
<br/>Now it's possible to use the runtime with 32-bit cell size on 64-bit processors.
In the original version of Pawn 3.2 doing so wasn't a good idea since the physical addresses of native functions and AMX extension modules were stored in Pawn cells and a cell size could not be enough to fit a pointer.
<br/>Also, as part of this change, file functions in the amxFile module now operate on file IDs (1, 2, ...) instead of casting (FILE *) pointers into cells and vice versa.
</li>

<li>Implemented relocation of data addresses in data access instructions.
<br/>In the original Pawn 3.2 code the jump addresses in branch instructions (<code>CALL</code>, <code>JUMP</code>, <code>JZER</code>, <code>JNZ</code>, <code>JEQ</code> etc.) are replaced by the corresponding physical addresses on script load, so why not do the same thing to speed up access to the data section?
<br/>This feature is only supported for the new interpreter core (see above).
</li>
</ul>
