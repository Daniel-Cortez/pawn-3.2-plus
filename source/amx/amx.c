/*  Pawn Abstract Machine (for the Pawn language)
 *
 *  Copyright (c) ITB CompuPhase, 1997-2009
 *
 *  This software is provided "as-is", without any express or implied warranty.
 *  In no event will the authors be held liable for any damages arising from
 *  the use of this software.
 *
 *  Permission is granted to anyone to use this software for any purpose,
 *  including commercial applications, and to alter it and redistribute it
 *  freely, subject to the following restrictions:
 *
 *  1.  The origin of this software must not be misrepresented; you must not
 *      claim that you wrote the original software. If you use this software in
 *      a product, an acknowledgment in the product documentation would be
 *      appreciated but is not required.
 *  2.  Altered source versions must be plainly marked as such, and must not be
 *      misrepresented as being the original software.
 *  3.  This notice may not be removed or altered from any source distribution.
 *
 *  Version: $Id: amx.c 3699 2007-01-17 11:15:40Z thiadmer $
 */

#if BUILD_PLATFORM == WINDOWS && BUILD_TYPE == RELEASE && BUILD_COMPILER == MSVC && PAWN_CELL_SIZE == 64
  /* bad bad workaround but we have to prevent a compiler crash :/ */
  #pragma optimize("g",off)
#endif

#define WIN32_LEAN_AND_MEAN
#if defined _UNICODE || defined __UNICODE__ || defined UNICODE
# if !defined UNICODE   /* for Windows API */
#   define UNICODE
# endif
# if !defined _UNICODE  /* for C library */
#   define _UNICODE
# endif
#endif

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>     /* for wchar_t */
#include <stdlib.h>     /* for getenv() */
#include <string.h>
#include "amx.h"
#if defined LINUX || defined __FreeBSD__ || defined __OpenBSD__
  #include <sclinux.h>
  #if !defined AMX_NODYNALOAD
    #include <dlfcn.h>
  #endif
  #if defined JIT
    #include <sys/types.h>
    #include <sys/mman.h>
  #endif
#endif
#if defined __LCC__ || defined LINUX
  #include <wchar.h>    /* for wcslen() */
#endif
#if (defined _Windows && !defined AMX_NODYNALOAD) || (defined JIT && __WIN32__)
  #include <windows.h>
#endif

#include "amx_internal.h"

#ifdef AMX_INIT
 #ifdef AMX_PTR_SIZE
    #ifdef __cplusplus
    extern "C" {
    #endif /* __cplusplus */
    int VerifyRelocateBytecode(AMX *amx);
    #ifdef __cplusplus
    }
    #endif /* __cplusplus */
  #else
    /* pawndisasm is compiled with amx.c, but without amx_verifier.c,
       so we'll use a dummy function here instead.
    */
    static int VerifyRelocateBytecode(AMX *amx)
    {
      (void)amx;
      return AMX_ERR_NONE;
    }
  #endif
#endif


#if !defined NDEBUG
  static int check_endian(void)
  {
    uint16_t val=0x00ff;
    unsigned char *ptr=(unsigned char *)&val;
    /* "ptr" points to the starting address of "val". If that address
     * holds the byte "0xff", the computer stored the low byte of "val"
     * at the lower address, and so the memory lay out is Little Endian.
     */
    assert(*ptr==0xff || *ptr==0x00);
    #if BYTE_ORDER==BIG_ENDIAN
      return *ptr==0x00;  /* return "true" if big endian */
    #else
      return *ptr==0xff;  /* return "true" if little endian */
    #endif
  }
#endif

#if BYTE_ORDER==BIG_ENDIAN || PAWN_CELL_SIZE==16
  static void swap16(uint16_t *v)
  {
    unsigned char *s = (unsigned char *)v;
    unsigned char t;

    assert_static(sizeof(*v)==2);
    /* swap two bytes */
    t=s[0];
    s[0]=s[1];
    s[1]=t;
  }
#endif

#if BYTE_ORDER==BIG_ENDIAN || PAWN_CELL_SIZE==32
  static void swap32(uint32_t *v)
  {
    unsigned char *s = (unsigned char *)v;
    unsigned char t;

    assert_static(sizeof(*v)==4);
    /* swap outer two bytes */
    t=s[0];
    s[0]=s[3];
    s[3]=t;
    /* swap inner two bytes */
    t=s[1];
    s[1]=s[2];
    s[2]=t;
  }
#endif

#if (BYTE_ORDER==BIG_ENDIAN || PAWN_CELL_SIZE==64) && (defined _I64_MAX || defined HAVE_I64)
  static void swap64(uint64_t *v)
  {
    unsigned char *s = (unsigned char *)v;
    unsigned char t;

    assert_static(sizeof(*v)==8);

    t=s[0];
    s[0]=s[7];
    s[7]=t;

    t=s[1];
    s[1]=s[6];
    s[6]=t;

    t=s[2];
    s[2]=s[5];
    s[5]=t;

    t=s[3];
    s[3]=s[4];
    s[4]=t;
  }
#endif

#if defined AMX_ALIGN || defined AMX_INIT
uint16_t * AMXAPI amx_Align16(uint16_t *v)
{
  assert_static(sizeof(*v)==2);
  assert(check_endian());
  #if BYTE_ORDER==BIG_ENDIAN
    swap16(v);
  #endif
  return v;
}

uint32_t * AMXAPI amx_Align32(uint32_t *v)
{
  assert_static(sizeof(*v)==4);
  assert(check_endian());
  #if BYTE_ORDER==BIG_ENDIAN
    swap32(v);
  #endif
  return v;
}

#if defined _I64_MAX || defined HAVE_I64
uint64_t * AMXAPI amx_Align64(uint64_t *v)
{
  assert_static(sizeof(*v)==8);
  assert(check_endian());
  #if BYTE_ORDER==BIG_ENDIAN
    swap64(v);
  #endif
  return v;
}
#endif  /* _I64_MAX || HAVE_I64 */
#endif  /* AMX_ALIGN || AMX_INIT */

#if PAWN_CELL_SIZE==16
  #define swapcell  swap16
#elif PAWN_CELL_SIZE==32
  #define swapcell  swap32
#elif PAWN_CELL_SIZE==64 && (defined _I64_MAX || defined HAVE_I64)
  #define swapcell  swap64
#else
  #error Unsupported cell size
#endif

#if defined AMX_FLAGS
int AMXAPI amx_Flags(AMX *amx,uint16_t *flags)
{
  AMX_HEADER *hdr;

  *flags=0;
  if (amx==NULL)
    return AMX_ERR_FORMAT;
  hdr=(AMX_HEADER *)amx->base;
  if (hdr->magic!=AMX_MAGIC)
    return AMX_ERR_FORMAT;
  if (hdr->file_version>CUR_FILE_VERSION || hdr->amx_version<MIN_FILE_VERSION)
    return AMX_ERR_VERSION;
  *flags=hdr->flags;
  return AMX_ERR_NONE;
}
#endif /* AMX_FLAGS */

#if defined AMX_DEFCALLBACK
int AMXAPI amx_Callback(AMX *amx, cell index, cell *result, const cell *params)
{
  AMX_HEADER *hdr;
  AMX_NATIVE f;

  assert(amx!=NULL);
  hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  assert(hdr->natives<=hdr->libraries);
  assert(index>=0 && index<(cell)NUMNATIVES(hdr));
  f=(AMX_NATIVE)amx->natives[(size_t)index];
  assert(f!=NULL);

  /* Now that we have found the function, patch the program so that any
   * subsequent call will call the function directly (bypassing this
   * callback).
   * This trick cannot work in the JIT, because the program would need to
   * be re-JIT-compiled after patching a P-code instruction.
   */
  #if defined JIT && !defined NDEBUG
    if ((amx->flags & AMX_FLAG_JITC)!=0)
      assert(amx->sysreq_d==0);
  #endif
  if (amx->sysreq_d!=0) {
    /* at the point of the call, the CIP pseudo-register points directly
     * behind the SYSREQ instruction and its parameter(s)
     */
    unsigned char *code=amx->base+(int)hdr->cod+(int)amx->cip-sizeof(cell);
    assert(amx->cip >= 4 && amx->cip < (hdr->dat - hdr->cod));
    assert_static(sizeof(f)<=sizeof(cell)); /* function pointer must fit in a cell */
    if (amx->flags & AMX_FLAG_SYSREQN)		/* SYSREQ.N has 2 parameters */
      code-=sizeof(cell);
#if defined AMX_EXEC_USE_JUMP_TABLE && !defined AMX_DONT_RELOCATE
    if (*(cell*)code==index) {
#else
    if (*(cell*)code!=OP_SYSREQ_PRI) {
      assert(*(cell*)(code-sizeof(cell))==OP_SYSREQ_C || *(cell*)(code-sizeof(cell))==OP_SYSREQ_N);
      assert(*(cell*)code==index);
#endif
      *(cell*)(code-sizeof(cell))=amx->sysreq_d;
      *(cell*)code=(cell)f;
    } /* if */
  } /* if */

  /* Note:
   *   params[0] == number of bytes for the additional parameters passed to the native function
   *   params[1] == first argument
   *   etc.
   */

  amx->error=AMX_ERR_NONE;
  *result = f(amx,params);
  return amx->error;
}
#endif /* defined AMX_DEFCALLBACK */


#if defined JIT
  extern int AMXAPI getMaxCodeSize(void);
  extern int AMXAPI asm_runJIT(void *sourceAMXbase, void *jumparray, void *compiledAMXbase);
#endif

#if defined AMX_INIT

#if AMX_COMPACTMARGIN > 2
static void expand(unsigned char *code, long codesize, long memsize)
{
  ucell c;
  struct {
    long memloc;
    ucell c;
  } spare[AMX_COMPACTMARGIN];
  int sh=0,st=0,sc=0;
  int shift;

  /* for in-place expansion, move from the end backward */
  assert(memsize % sizeof(cell) == 0);
  while (codesize>0) {
    c=0;
    shift=0;
    do {
      codesize--;
      /* no input byte should be shifted out completely */
      assert(shift<8*sizeof(cell));
      /* we work from the end of a sequence backwards; the final code in
       * a sequence may not have the continuation bit set */
      assert(shift>0 || (code[(size_t)codesize] & 0x80)==0);
      c|=(ucell)(code[(size_t)codesize] & 0x7f) << shift;
      shift+=7;
    } while (codesize>0 && (code[(size_t)codesize-1] & 0x80)!=0);
    /* sign expand */
    if ((code[(size_t)codesize] & 0x40)!=0) {
      while (shift < (int)(8*sizeof(cell))) {
        c|=(ucell)0xff << shift;
        shift+=8;
      } /* while */
    } /* if */
    /* store */
    while (sc && (spare[sh].memloc>codesize)) {
      *(ucell *)(code+(int)spare[sh].memloc)=spare[sh].c;
      sh=(sh+1)%AMX_COMPACTMARGIN;
      sc--;
    } /* while */
    memsize -= sizeof(cell);
    assert(memsize>=0);
    if ((memsize>codesize)||((memsize==codesize)&&(memsize==0))) {
      *(ucell *)(code+(size_t)memsize)=c;
    } else {
      assert(sc<AMX_COMPACTMARGIN);
      spare[st].memloc=memsize;
      spare[st].c=c;
      st=(st+1)%AMX_COMPACTMARGIN;
      sc++;
    } /* if */
  } /* while */
  /* when all bytes have been expanded, the complete memory block should be done */
  assert(memsize==0);
}
#endif /* AMX_COMPACTMARGIN > 2 */

int AMXAPI amx_Init(AMX *amx,void *program)
{
  AMX_HEADER *hdr;
  int err,i;
  unsigned char *data;
  int numnatives;
  #if (defined _Windows || defined LINUX || defined __FreeBSD__ || defined __OpenBSD__) && !defined AMX_NODYNALOAD
    #if defined _Windows
      char libname[sNAMEMAX+8]; /* +1 for '\0', +3 for 'amx' prefix, +4 for extension */
      typedef int (FAR WINAPI *AMX_ENTRY)(AMX _FAR *amx);
      HINSTANCE hlib;
    #else
      char libname[_MAX_PATH];
      char *root;
      typedef int (*AMX_ENTRY)(AMX *amx);
      void *hlib;
      #if !defined AMX_LIBPATH
        #define AMX_LIBPATH     "AMXLIB"
      #endif
    #endif
    int numlibraries;
    AMX_FUNCSTUB *lib;
    AMX_ENTRY libinit;
  #endif
  (void)i;

  if ((amx->flags & AMX_FLAG_RELOC)!=0)
    return AMX_ERR_INIT;  /* already initialized (may not do so twice) */

  hdr=(AMX_HEADER *)program;
  /* the header is in Little Endian, on a Big Endian machine, swap all
   * multi-byte words
   */
  assert(check_endian());
  #if BYTE_ORDER==BIG_ENDIAN
    amx_Align32((uint32_t*)&hdr->size);
    amx_Align16(&hdr->magic);
    amx_Align16((uint16_t*)&hdr->flags);
    amx_Align16((uint16_t*)&hdr->defsize);
    amx_Align32((uint32_t*)&hdr->cod);
    amx_Align32((uint32_t*)&hdr->dat);
    amx_Align32((uint32_t*)&hdr->hea);
    amx_Align32((uint32_t*)&hdr->stp);
    amx_Align32((uint32_t*)&hdr->cip);
    amx_Align32((uint32_t*)&hdr->publics);
    amx_Align32((uint32_t*)&hdr->natives);
    amx_Align32((uint32_t*)&hdr->libraries);
    amx_Align32((uint32_t*)&hdr->pubvars);
    amx_Align32((uint32_t*)&hdr->tags);
  #endif

  if (hdr->magic!=AMX_MAGIC)
    return AMX_ERR_FORMAT;
  if (hdr->file_version>CUR_FILE_VERSION || hdr->amx_version<MIN_FILE_VERSION)
    return AMX_ERR_VERSION;
  if (hdr->defsize!=sizeof(AMX_FUNCSTUB) && hdr->defsize!=sizeof(AMX_FUNCSTUBNT))
    return AMX_ERR_FORMAT;
  if (USENAMETABLE(hdr)) {
    uint16_t *namelength;
    /* when there is a separate name table, check the maximum name length
     * in that table
     */
    amx_Align32((uint32_t*)&hdr->nametable);
    namelength=(uint16_t*)((unsigned char*)program + (unsigned)hdr->nametable);
    amx_Align16(namelength);
    if (*namelength>sNAMEMAX)
      return AMX_ERR_FORMAT;
  } /* if */
  if (hdr->stp<=0)
    return AMX_ERR_FORMAT;
  #if BYTE_ORDER==BIG_ENDIAN
    if ((hdr->flags & AMX_FLAG_COMPACT)==0) {
      ucell *code=(ucell *)((unsigned char *)program+(int)hdr->cod);
      while (code<(ucell *)((unsigned char *)program+(int)hdr->hea))
        swapcell(code++);
    } /* if */
  #endif
  assert((hdr->flags & AMX_FLAG_COMPACT)!=0 || hdr->hea == hdr->size);
  if ((hdr->flags & AMX_FLAG_COMPACT)!=0) {
    #if AMX_COMPACTMARGIN > 2
      expand((unsigned char *)program+(int)hdr->cod,
             hdr->size - hdr->cod, hdr->hea - hdr->cod);
    #else
      return AMX_ERR_FORMAT;
    #endif
  } /* if */

  amx->base=(unsigned char *)program;

  amx->libraries=NULL;
  amx->natives = NULL;
  numnatives=(int)NUMNATIVES(hdr);
  if (numnatives!=0) {
    amx->natives=(void **)malloc(sizeof(void *)*(size_t)numnatives);
    if (amx->natives==NULL)
      return AMX_ERR_MEMORY;
    for (i=0; i<numnatives; ++i)
      amx->natives[(size_t)i]=NULL;
  }

  /* set initial values */
  amx->hlw=hdr->hea - hdr->dat; /* stack and heap relative to data segment */
  amx->stp=hdr->stp - hdr->dat - sizeof(cell);
  amx->hea=amx->hlw;
  amx->stk=amx->stp;
  #if defined AMX_DEFCALLBACK
    if (amx->callback==NULL)
      amx->callback=amx_Callback;
  #endif
  /* to split the data segment off the code segment, the "data" field must
   * be set to a non-NULL value on initialization, before calling amx_Init()
   */
  if (amx->data!=NULL) {
    data=amx->data;
    memcpy(data,amx->base+(int)hdr->dat,(size_t)(hdr->hea-hdr->dat));
  } else {
    data=amx->base+(int)hdr->dat;
  } /* if */

  /* Set a zero cell at the top of the stack, which functions
   * as a sentinel for strings.
   */
  * (cell *)(data+(int)(hdr->stp-hdr->dat-sizeof(cell)))=0;

  /* also align all addresses in the public function, public variable,
   * public tag and native function tables --offsets into the name table
   * (if present) must also be swapped.
   */
  #if BYTE_ORDER==BIG_ENDIAN
  { /* local */
    AMX_FUNCSTUB *fs;
    int num;

    fs=GETENTRY(hdr,natives,0);
    num=numnatives;
    for (i=0; i<num; i++) {
      amx_AlignCell(&fs->address);      /* redundant, because it should be zero */
      if (USENAMETABLE(hdr))
        amx_Align32(&((AMX_FUNCSTUBNT*)fs)->nameofs);
      fs=(AMX_FUNCSTUB*)((unsigned char *)fs+hdr->defsize);
    } /* for */

    fs=GETENTRY(hdr,publics,0);
    assert(hdr->publics<=hdr->natives);
    num=NUMPUBLICS(hdr);
    for (i=0; i<num; i++) {
      amx_AlignCell(&fs->address);
      if (USENAMETABLE(hdr))
        amx_Align32(&((AMX_FUNCSTUBNT*)fs)->nameofs);
      fs=(AMX_FUNCSTUB*)((unsigned char *)fs+hdr->defsize);
    } /* for */

    fs=GETENTRY(hdr,pubvars,0);
    assert(hdr->pubvars<=hdr->tags);
    num=NUMPUBVARS(hdr);
    for (i=0; i<num; i++) {
      amx_AlignCell(&fs->address);
      if (USENAMETABLE(hdr))
        amx_Align32(&((AMX_FUNCSTUBNT*)fs)->nameofs);
      fs=(AMX_FUNCSTUB*)((unsigned char *)fs+hdr->defsize);
    } /* for */

    fs=GETENTRY(hdr,tags,0);
    if (hdr->file_version<7) {
      assert(hdr->tags<=hdr->cod);
      num=NUMENTRIES(hdr,tags,cod);
    } else {
      assert(hdr->tags<=hdr->nametable);
      num=NUMENTRIES(hdr,tags,nametable);
    } /* if */
    for (i=0; i<num; i++) {
      amx_AlignCell(&fs->address);
      if (USENAMETABLE(hdr))
        amx_Align32(&((AMX_FUNCSTUBNT*)fs)->nameofs);
      fs=(AMX_FUNCSTUB*)((unsigned char *)fs+hdr->defsize);
    } /* for */
  } /* local */
  #endif

  /* relocate call and jump instructions */
  if ((err=VerifyRelocateBytecode(amx))!=AMX_ERR_NONE)
    return err;

  /* load any extension modules that the AMX refers to */
  #if (defined _Windows || defined LINUX || defined __FreeBSD__ || defined __OpenBSD__) && !defined AMX_NODYNALOAD
    #if defined LINUX || defined __FreeBSD__ || defined __OpenBSD__
      root=getenv("AMXLIB");
    #endif
    hdr=(AMX_HEADER *)amx->base;
    numlibraries=(int)NUMLIBRARIES(hdr);
    amx->libraries=(void **)malloc(sizeof(void *)*(size_t)numlibraries);
    if (amx->libraries==NULL)
      return AMX_ERR_MEMORY;
    for (i=0; i<numlibraries; i++) {
      lib=GETENTRY(hdr,libraries,i);
      libname[0]='\0';
      #if defined LINUX || defined __FreeBSD__ || defined __OpenBSD__
        if (root!=NULL && *root!='\0') {
          strcpy(libname,root);
          if (libname[strlen(libname)-1]!='/')
            strcat(libname,"/");
        } /* if */
      #endif
      strcat(libname,"amx");
      strcat(libname,GETENTRYNAME(hdr,lib));
      #if defined _Windows
        strcat(libname,".dll");
        #if defined __WIN32__
          hlib=LoadLibraryA(libname);
        #else
          hlib=LoadLibrary(libname);
          if (hlib<=HINSTANCE_ERROR)
            hlib=NULL;
        #endif
      #elif defined LINUX || defined __FreeBSD__ || defined __OpenBSD__
        strcat(libname,".so");
        hlib=dlopen(libname,RTLD_NOW);
      #endif
      if (hlib!=NULL) {
        /* a library that cannot be loaded or that does not have the required
         * initialization function is simply ignored
         */
        char funcname[sNAMEMAX+9]; /* +1 for '\0', +4 for 'amx_', +4 for 'Init' */
        strcpy(funcname,"amx_");
        strcat(funcname,GETENTRYNAME(hdr,lib));
        strcat(funcname,"Init");
        #if defined _Windows
          libinit=(AMX_ENTRY)GetProcAddress(hlib,funcname);
        #elif defined LINUX || defined __FreeBSD__ || defined __OpenBSD__
          libinit=(AMX_ENTRY)dlsym(hlib,funcname);
        #endif
        if (libinit!=NULL)
          libinit(amx);
      } /* if */
      amx->libraries[i]=(void *)hlib;
    } /* for */
  #endif

  return AMX_ERR_NONE;
}

#if defined JIT

  #define CODESIZE_JIT    8192  /* approximate size of the code for the JIT */

  #if defined __WIN32__   /* this also applies to Win32 "console" applications */

    #define ALIGN(addr)     (addr)

    #define PROT_READ       0x1         /* page can be read */
    #define PROT_WRITE      0x2         /* page can be written */
    #define PROT_EXEC       0x4         /* page can be executed */
    #define PROT_NONE       0x0         /* page can not be accessed */

    static int mprotect(void *addr, size_t len, int prot)
    {
      DWORD prev, p = 0;
      if ((prot & PROT_WRITE)!=0)
        p = PAGE_EXECUTE_READWRITE;
      else
        p |= PAGE_EXECUTE_READ;
      return !VirtualProtect(addr, len, p, &prev);
    }

  #elif defined LINUX || defined __FreeBSD__ || defined __OpenBSD__

    /* Linux already has mprotect() */
    #define ALIGN(addr) (char *)(((long)addr + sysconf(_SC_PAGESIZE)-1) & ~(sysconf(_SC_PAGESIZE)-1))

  #else

    // TODO: Add cases for Linux, Unix, OS/2, ...

    /* DOS32 has no imposed limits on its segments */
    #define ALIGN(addr)     (addr)
    #define mprotect(addr, len, prot)   (0)

  #endif /* #if defined __WIN32 __ */

int AMXAPI amx_InitJIT(AMX *amx, void *reloc_table, void *native_code)
{
  int res;
  AMX_HEADER *hdr;

  if ((amx->flags & AMX_FLAG_JITC)==0)
    return AMX_ERR_INIT_JIT;    /* flag not set, this AMX is not prepared for JIT */
  if (hdr->file_version>MAX_FILE_VER_JIT)
    return AMX_ERR_VERSION;     /* JIT may not support the newest file version(s) */

  /* Patching SYSREQ.C opcodes to SYSREQ.D cannot work in the JIT, because the
   * program would need to be re-JIT-compiled after patching a P-code
   * instruction. If this field is not zero, something went wrong with the
   * amx_BrowseRelocate().
   */
  assert(amx->sysreq_d==0);

  if (mprotect(ALIGN(asm_runJIT), CODESIZE_JIT, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    return AMX_ERR_INIT_JIT;

  /* copy the prefix */
  memcpy(native_code, amx->base, ((AMX_HEADER *)(amx->base))->cod);
  hdr = native_code;

  /* JIT rulz! (TM) */
  /* MP: added check for correct compilation */
  if ((res = asm_runJIT(amx->base, reloc_table, native_code)) == 0) {
    /* update the required memory size (the previous value was a
     * conservative estimate, now we know the exact size)
     */
    amx->code_size = (hdr->dat + hdr->stp + sizeof(cell)) & ~(sizeof(cell)-1);
    /* The compiled code is relocatable, since only relative jumps are
     * used for destinations within the generated code, and absolute
     * addresses are only for jumps into the runtime, which is fixed
     * in memory.
     */
    /* set the new pointers */
    amx->base = (unsigned char*) native_code;
    amx->cip = hdr->cip;
    amx->hea = hdr->hea;
    amx->hlw = hdr->hea;
    amx->stp = hdr->stp - sizeof(cell);
    /* also put a sentinel for strings at the top of the stack */
    *(cell *)((char*)native_code + hdr->dat + hdr->stp - sizeof(cell)) = 0;
    amx->stk = amx->stp;
  } /* if */

  return (res == 0) ? AMX_ERR_NONE : AMX_ERR_INIT_JIT;
}

#else /* #if defined JIT */

int AMXAPI amx_InitJIT(AMX *amx,void *compiled_program,void *reloc_table)
{
  (void)amx;
  (void)compiled_program;
  (void)reloc_table;
  return AMX_ERR_INIT_JIT;
}

#endif  /* #if defined JIT */

#endif  /* AMX_INIT */

#if defined AMX_CLEANUP
int AMXAPI amx_Cleanup(AMX *amx)
{
  #if (defined _Windows || defined LINUX || defined __FreeBSD__ || defined __OpenBSD__) && !defined AMX_NODYNALOAD
    #if defined _Windows
      typedef int (FAR WINAPI *AMX_ENTRY)(AMX FAR *amx);
    #else
      typedef int (*AMX_ENTRY)(AMX *amx);
    #endif
    AMX_HEADER *hdr;
    int numlibraries,i;
    AMX_FUNCSTUB *lib;
    AMX_ENTRY libcleanup;
  #endif

  /* unload all extension modules */
  #if (defined _Windows || defined LINUX || defined __FreeBSD__ || defined __OpenBSD__) && !defined AMX_NODYNALOAD
    hdr=(AMX_HEADER *)amx->base;
    assert(hdr->magic==AMX_MAGIC);
    numlibraries=NUMLIBRARIES(hdr);
    for (i=0; i<numlibraries; i++) {
      lib=GETENTRY(hdr,libraries,i);
      if (amx->libraries[i]!=0) {
        char funcname[sNAMEMAX+12]; /* +1 for '\0', +4 for 'amx_', +7 for 'Cleanup' */
        strcpy(funcname,"amx_");
        strcat(funcname,GETENTRYNAME(hdr,lib));
        strcat(funcname,"Cleanup");
        #if defined _Windows
          libcleanup=(AMX_ENTRY)GetProcAddress((HINSTANCE)amx->libraries[i],funcname);
        #elif defined LINUX || defined __FreeBSD__ || defined __OpenBSD__
          libcleanup=(AMX_ENTRY)dlsym(amx->libraries[i],funcname);
        #endif
        if (libcleanup!=NULL)
          libcleanup(amx);
        #if defined _Windows
          FreeLibrary((HINSTANCE)amx->libraries[i]);
        #elif defined LINUX || defined __FreeBSD__ || defined __OpenBSD__
          dlclose(amx->libraries[i]);
        #endif
      } /* if */
    } /* for */
  #endif
  free(amx->libraries);
  free(amx->natives);
  return AMX_ERR_NONE;
}
#endif /* AMX_CLEANUP */

#if defined AMX_CLONE
int AMXAPI amx_Clone(AMX *amxClone, AMX *amxSource, void *data)
{
  AMX_HEADER *hdr;
  unsigned char _FAR *dataSource;

  if (amxSource==NULL)
    return AMX_ERR_FORMAT;
  if (amxClone==NULL)
    return AMX_ERR_PARAMS;
  if ((amxSource->flags & AMX_FLAG_RELOC)==0)
    return AMX_ERR_INIT;
  hdr=(AMX_HEADER *)amxSource->base;
  if (hdr->magic!=AMX_MAGIC)
    return AMX_ERR_FORMAT;
  if (hdr->file_version>CUR_FILE_VERSION || hdr->amx_version<MIN_FILE_VERSION)
    return AMX_ERR_VERSION;

  /* set initial values */
  amxClone->base=amxSource->base;
  amxClone->hlw=hdr->hea - hdr->dat; /* stack and heap relative to data segment */
  amxClone->stp=hdr->stp - hdr->dat - sizeof(cell);
  amxClone->hea=amxClone->hlw;
  amxClone->stk=amxClone->stp;
  if (amxClone->callback==NULL)
    amxClone->callback=amxSource->callback;
  if (amxClone->debug==NULL)
    amxClone->debug=amxSource->debug;
  amxClone->flags=amxSource->flags;

  /* copy the data segment; the stack and the heap can be left uninitialized */
  assert(data!=NULL);
  amxClone->data=(unsigned char _FAR *)data;
  dataSource=(amxSource->data!=NULL) ? amxSource->data : amxSource->base+(int)hdr->dat;
  memcpy(amxClone->data,dataSource,(size_t)(hdr->hea-hdr->dat));

  /* Set a zero cell at the top of the stack, which functions
   * as a sentinel for strings.
   */
  * (cell *)(amxClone->data+(int)amxClone->stp) = 0;

  return AMX_ERR_NONE;
}
#endif /* AMX_CLONE */

#if defined AMX_MEMINFO
int AMXAPI amx_MemInfo(AMX *amx, long *codesize, long *datasize, long *stackheap)
{
  AMX_HEADER *hdr;

  if (amx==NULL)
    return AMX_ERR_FORMAT;
  hdr=(AMX_HEADER *)amx->base;
  if (hdr->magic!=AMX_MAGIC)
    return AMX_ERR_FORMAT;
  if (hdr->file_version>CUR_FILE_VERSION || hdr->amx_version<MIN_FILE_VERSION)
    return AMX_ERR_VERSION;

  if (codesize!=NULL)
    *codesize=hdr->dat - hdr->cod;
  if (datasize!=NULL)
    *datasize=hdr->hea - hdr->dat;
  if (stackheap!=NULL)
    *stackheap=hdr->stp - hdr->hea;

  return AMX_ERR_NONE;
}
#endif /* AMX_MEMINFO */

#if defined AMX_NAMELENGTH
int AMXAPI amx_NameLength(AMX *amx, int *length)
{
  AMX_HEADER *hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  if (USENAMETABLE(hdr)) {
    uint16_t *namelength=(uint16_t*)(amx->base + (unsigned)hdr->nametable);
    *length=*namelength;
    assert(hdr->file_version>=7); /* name table exists only for file version 7+ */
  } else {
    *length=hdr->defsize - sizeof(ucell);
  } /* if */
  return AMX_ERR_NONE;
}
#endif /* AMX_NAMELENGTH */

#if defined AMX_XXXNATIVES
int AMXAPI amx_NumNatives(AMX *amx, int *number)
{
  AMX_HEADER *hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  assert(hdr->natives<=hdr->libraries);
  *number=NUMNATIVES(hdr);
  return AMX_ERR_NONE;
}

int AMXAPI amx_GetNative(AMX *amx, int index, char *funcname)
{
  AMX_HEADER *hdr;
  AMX_FUNCSTUB *func;

  hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  assert(hdr->natives<=hdr->libraries);
  if (index>=(cell)NUMNATIVES(hdr))
    return AMX_ERR_INDEX;

  func=GETENTRY(hdr,natives,index);
  strcpy(funcname,GETENTRYNAME(hdr,func));
  return AMX_ERR_NONE;
}

int AMXAPI amx_FindNative(AMX *amx, const char *name, int *index)
{
  int idx,last;
  char pname[sNAMEMAX+1];

  amx_NumNatives(amx, &last);
  /* linear search, the natives table is not sorted alphabetically */
  for (idx=0; idx<last; idx++) {
    amx_GetNative(amx,idx,pname);
    if (strcmp(pname,name)==0) {
      *index=idx;
      return AMX_ERR_NONE;
    } /* if */
  } /* for */
  *index=INT_MAX;
  return AMX_ERR_NOTFOUND;
}
#endif /* AMX_XXXNATIVES */

#if defined AMX_XXXPUBLICS
int AMXAPI amx_NumPublics(AMX *amx, int *number)
{
  AMX_HEADER *hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  assert(hdr->publics<=hdr->natives);
  *number=NUMPUBLICS(hdr);
  return AMX_ERR_NONE;
}

int AMXAPI amx_GetPublic(AMX *amx, int index, char *funcname)
{
  AMX_HEADER *hdr;
  AMX_FUNCSTUB *func;

  hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  assert(hdr->publics<=hdr->natives);
  if (index>=(cell)NUMPUBLICS(hdr))
    return AMX_ERR_INDEX;

  func=GETENTRY(hdr,publics,index);
  strcpy(funcname,GETENTRYNAME(hdr,func));
  return AMX_ERR_NONE;
}

int AMXAPI amx_FindPublic(AMX *amx, const char *name, int *index)
{
  int first,last,mid,result;
  char pname[sNAMEMAX+1];

  amx_NumPublics(amx, &last);
  last--;       /* last valid index is 1 less than the number of functions */
  first=0;
  /* binary search */
  while (first<=last) {
    mid=(first+last)/2;
    amx_GetPublic(amx,mid,pname);
    result=strcmp(pname,name);
    if (result>0) {
      last=mid-1;
    } else if (result<0) {
      first=mid+1;
    } else {
      *index=mid;
      return AMX_ERR_NONE;
    } /* if */
  } /* while */
  /* not found, set to an invalid index, so amx_Exec() on this index will fail
   * with an error
   */
  *index=INT_MAX;
  return AMX_ERR_NOTFOUND;
}
#endif /* AMX_XXXPUBLICS */

#if defined AMX_XXXPUBVARS
int AMXAPI amx_NumPubVars(AMX *amx, int *number)
{
  AMX_HEADER *hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  assert(hdr->pubvars<=hdr->tags);
  *number=NUMPUBVARS(hdr);
  return AMX_ERR_NONE;
}

int AMXAPI amx_GetPubVar(AMX *amx, int index, char *varname, cell *amx_addr)
{
  AMX_HEADER *hdr;
  AMX_FUNCSTUB *var;

  hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  assert(hdr->pubvars<=hdr->tags);
  if (index>=(cell)NUMPUBVARS(hdr))
    return AMX_ERR_INDEX;

  var=GETENTRY(hdr,pubvars,index);
  strcpy(varname,GETENTRYNAME(hdr,var));
  *amx_addr=var->address;
  return AMX_ERR_NONE;
}

int AMXAPI amx_FindPubVar(AMX *amx, const char *varname, cell *amx_addr)
{
  int first,last,mid,result;
  char pname[sNAMEMAX+1];
  cell paddr;

  amx_NumPubVars(amx, &last);
  last--;       /* last valid index is 1 less than the number of functions */
  first=0;
  /* binary search */
  while (first<=last) {
    mid=(first+last)/2;
    amx_GetPubVar(amx, mid, pname, &paddr);
    result=strcmp(pname,varname);
    if (result>0) {
      last=mid-1;
    } else if (result<0) {
      first=mid+1;
    } else {
      *amx_addr=paddr;
      return AMX_ERR_NONE;
    } /* if */
  } /* while */
  /* not found */
  *amx_addr=0;
  return AMX_ERR_NOTFOUND;
}
#endif /* AMX_XXXPUBVARS */

#if defined AMX_XXXTAGS
int AMXAPI amx_NumTags(AMX *amx, int *number)
{
  AMX_HEADER *hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  if (hdr->file_version<5) {    /* the tagname table appeared in file format 5 */
    *number=0;
    return AMX_ERR_VERSION;
  } /* if */
  if (hdr->file_version<7) {
    assert(hdr->tags<=hdr->cod);
    *number=NUMENTRIES(hdr,tags,cod);
  } else {
    assert(hdr->tags<=hdr->nametable);
    *number=NUMENTRIES(hdr,tags,nametable);
  } /* if */
  return AMX_ERR_NONE;
}

int AMXAPI amx_GetTag(AMX *amx, int index, char *tagname, cell *tag_id)
{
  AMX_HEADER *hdr;
  AMX_FUNCSTUB *tag;

  hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  if (hdr->file_version<5) {    /* the tagname table appeared in file format 5 */
    *tagname='\0';
    *tag_id=0;
    return AMX_ERR_VERSION;
  } /* if */

  if (hdr->file_version<7) {
    assert(hdr->tags<=hdr->cod);
    if (index>=(cell)NUMENTRIES(hdr,tags,cod))
      return AMX_ERR_INDEX;
  } else {
    assert(hdr->tags<=hdr->nametable);
    if (index>=(cell)NUMENTRIES(hdr,tags,nametable))
      return AMX_ERR_INDEX;
  } /* if */

  tag=GETENTRY(hdr,tags,index);
  strcpy(tagname,GETENTRYNAME(hdr,tag));
  *tag_id=tag->address;

  return AMX_ERR_NONE;
}

int AMXAPI amx_FindTagId(AMX *amx, cell tag_id, char *tagname)
{
  int first,last,mid;
  cell mid_id;

  #if !defined NDEBUG
    /* verify that the tagname table is sorted on the tag_id */
    amx_NumTags(amx, &last);
    if (last>0) {
      cell cur_id;
      amx_GetTag(amx,0,tagname,&cur_id);
      for (first=1; first<last; first++) {
        amx_GetTag(amx,first,tagname,&mid_id);
        assert(cur_id<mid_id);
        cur_id=mid_id;
      } /* for */
    } /* if */
  #endif

  amx_NumTags(amx, &last);
  last--;       /* last valid index is 1 less than the number of functions */
  first=0;
  /* binary search */
  while (first<=last) {
    mid=(first+last)/2;
    amx_GetTag(amx,mid,tagname,&mid_id);
    if (mid_id>tag_id)
      last=mid-1;
    else if (mid_id<tag_id)
      first=mid+1;
    else
      return AMX_ERR_NONE;
  } /* while */
  /* not found */
  *tagname='\0';
  return AMX_ERR_NOTFOUND;
}
#endif /* AMX_XXXTAGS */

#if defined AMX_XXXUSERDATA
int AMXAPI amx_GetUserData(AMX *amx, long tag, void **ptr)
{
  int index;

  assert(amx!=NULL);
  assert(tag!=0);
  for (index=0; index<AMX_USERNUM && amx->usertags[index]!=tag; index++)
    /* nothing */;
  if (index>=AMX_USERNUM)
    return AMX_ERR_USERDATA;
  *ptr=amx->userdata[index];
  return AMX_ERR_NONE;
}

int AMXAPI amx_SetUserData(AMX *amx, long tag, void *ptr)
{
  int index;

  assert(amx!=NULL);
  assert(tag!=0);
  /* try to find existing tag */
  for (index=0; index<AMX_USERNUM && amx->usertags[index]!=tag; index++)
    /* nothing */;
  /* if not found, try to find empty tag */
  if (index>=AMX_USERNUM)
    for (index=0; index<AMX_USERNUM && amx->usertags[index]!=0; index++)
      /* nothing */;
  /* if still not found, quit with error */
  if (index>=AMX_USERNUM)
    return AMX_ERR_INDEX;
  /* set the tag and the value */
  amx->usertags[index]=tag;
  amx->userdata[index]=ptr;
  return AMX_ERR_NONE;
}
#endif /* AMX_XXXUSERDATA */

#if defined AMX_REGISTER || defined AMX_EXEC || defined AMX_INIT
static AMX_NATIVE findfunction(const char *name, const AMX_NATIVE_INFO *list, int number)
{
  int i;

  assert(list!=NULL);
  for (i=0; list[i].name!=NULL && (i<number || number==-1); i++)
    if (strcmp(name,list[i].name)==0)
      return list[i].func;
  return NULL;
}

int AMXAPI amx_Register(AMX *amx, const AMX_NATIVE_INFO *list, int number)
{
  AMX_FUNCSTUB *func;
  AMX_HEADER *hdr;
  int i,numnatives,err;
  AMX_NATIVE funcptr;

  hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  assert(hdr->natives<=hdr->libraries);
  numnatives=NUMNATIVES(hdr);

  err=AMX_ERR_NONE;
  func=GETENTRY(hdr,natives,0);
  for (i=0; i<numnatives; i++) {
    if (amx->natives[(size_t)i]==0) {
      /* this function is not yet located */
      funcptr=(list!=NULL) ? findfunction(GETENTRYNAME(hdr,func),list,number) : NULL;
      if (funcptr!=NULL)
        amx->natives[(size_t)i]=(void *)funcptr;
      else
        err=AMX_ERR_NOTFOUND;
    } /* if */
    func=(AMX_FUNCSTUB*)((unsigned char*)func+hdr->defsize);
  } /* for */
  if (err==AMX_ERR_NONE)
    amx->flags|=AMX_FLAG_NTVREG;
  return err;
}
#endif /* AMX_REGISTER || AMX_EXEC || AMX_INIT */

#if defined AMX_NATIVEINFO
AMX_NATIVE_INFO * AMXAPI amx_NativeInfo(const char *name, AMX_NATIVE func)
{
  static AMX_NATIVE_INFO n;
  n.name=name;
  n.func=func;
  return &n;
}
#endif /* AMX_NATIVEINFO */

#if defined AMX_PUSHXXX

int AMXAPI amx_Push(AMX *amx, cell value)
{
  AMX_HEADER *hdr;
  unsigned char *data;

  if (amx->hea+STKMARGIN>amx->stk)
    return AMX_ERR_STACKERR;
  hdr=(AMX_HEADER *)amx->base;
  data=(amx->data!=NULL) ? amx->data : amx->base+(int)hdr->dat;
  amx->stk-=sizeof(cell);
  amx->paramcount+=1;
  *(cell *)(data+(int)amx->stk)=value;
  return AMX_ERR_NONE;
}

int AMXAPI amx_PushArray(AMX *amx, cell *amx_addr, cell **phys_addr, const cell array[], int numcells)
{
  cell *paddr,xaddr;
  int err;

  assert(amx!=NULL);
  assert(array!=NULL);

  err=amx_Allot(amx,numcells,&xaddr,&paddr);
  if (err==AMX_ERR_NONE) {
    if (amx_addr!=NULL)
      *amx_addr=xaddr;
    if (phys_addr!=NULL)
      *phys_addr=paddr;
    memcpy(paddr,array,numcells*sizeof(cell));
    err=amx_Push(amx,xaddr);
  } /* if */
  return err;
}

int AMXAPI amx_PushString(AMX *amx, cell *amx_addr, cell **phys_addr, const char *string, int pack, int use_wchar)
{
  cell *paddr, xaddr;
  int numcells,err;

  assert(amx!=NULL);
  assert(string!=NULL);

  #if defined AMX_ANSIONLY
    numcells=strlen(string) + 1;
  #else
    numcells= (use_wchar ? wcslen((const wchar_t*)string) : strlen(string)) + 1;
  #endif
  if (pack)
    numcells=(numcells+sizeof(cell)-1)/sizeof(cell);
  err=amx_Allot(amx,numcells,&xaddr,&paddr);
  if (err==AMX_ERR_NONE) {
    if (amx_addr!=NULL)
      *amx_addr=xaddr;
    if (phys_addr!=NULL)
      *phys_addr=paddr;
    amx_SetString(paddr,string,pack,use_wchar,UNLIMITED);
    err=amx_Push(amx,xaddr);
  } /* if */
  return err;
}
#endif /* AMX_PUSHXXX */

#if defined AMX_SETCALLBACK
int AMXAPI amx_SetCallback(AMX *amx,AMX_CALLBACK callback)
{
  assert(amx!=NULL);
  assert(callback!=NULL);
  amx->callback=callback;
  return AMX_ERR_NONE;
}
#endif /* AMX_SETCALLBACK */

#if defined AMX_SETDEBUGHOOK
int AMXAPI amx_SetDebugHook(AMX *amx,AMX_DEBUG debug)
{
  assert(amx!=NULL);
  amx->debug=debug;
  return AMX_ERR_NONE;
}
#endif /* AMX_SETDEBUGHOOK */

#if defined AMX_RAISEERROR
int AMXAPI amx_RaiseError(AMX *amx, int error)
{
  assert(error>0);
  amx->error=error;
  return AMX_ERR_NONE;
}
#endif /* AMX_RAISEERROR */

#if defined AMX_GETADDR
int AMXAPI amx_GetAddr(AMX *amx,cell amx_addr,cell **phys_addr)
{
  AMX_HEADER *hdr;
  unsigned char *data;

  assert(amx!=NULL);
  hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  data=(amx->data!=NULL) ? amx->data : amx->base+(int)hdr->dat;

  assert(phys_addr!=NULL);
  if (amx_addr>=amx->hea && amx_addr<amx->stk || amx_addr<0 || amx_addr>=amx->stp) {
    *phys_addr=NULL;
    return AMX_ERR_MEMACCESS;
  } /* if */

  *phys_addr=(cell *)(data + (int)amx_addr);
  return AMX_ERR_NONE;
}
#endif /* AMX_GETADDR */

#if defined AMX_ALLOT || defined AMX_EXEC
int AMXAPI amx_Allot(AMX *amx,int cells,cell *amx_addr,cell **phys_addr)
{
  AMX_HEADER *hdr;
  unsigned char *data;

  assert(amx!=NULL);
  hdr=(AMX_HEADER *)amx->base;
  assert(hdr!=NULL);
  assert(hdr->magic==AMX_MAGIC);
  data=(amx->data!=NULL) ? amx->data : amx->base+(int)hdr->dat;

  if (amx->stk - amx->hea - cells*sizeof(cell) < STKMARGIN)
    return AMX_ERR_MEMORY;
  assert(amx_addr!=NULL);
  assert(phys_addr!=NULL);
  *amx_addr=amx->hea;
  *phys_addr=(cell *)(data + (int)amx->hea);
  amx->hea += cells*sizeof(cell);
  return AMX_ERR_NONE;
}

int AMXAPI amx_Release(AMX *amx,cell amx_addr)
{
  if (amx->hea > amx_addr)
    amx->hea=amx_addr;
  return AMX_ERR_NONE;
}
#endif /* AMX_ALLOT */

#if defined AMX_XXXSTRING || defined AMX_UTF8XXX

#define CHARBITS        (8*sizeof(char))
#if PAWN_CELL_SIZE==16
  #define CHARMASK      (0xffffu << 8*(2-sizeof(char)))
#elif PAWN_CELL_SIZE==32
  #define CHARMASK      (0xffffffffuL << 8*(4-sizeof(char)))
#elif PAWN_CELL_SIZE==64
  #define CHARMASK      (0xffffffffffffffffuLL << 8*(8-sizeof(char)))
#else
  #error Unsupported cell size
#endif

int AMXAPI amx_StrLen(const cell *cstr, int *length)
{
  int len;
  #if BYTE_ORDER==LITTLE_ENDIAN
    cell c;
  #endif

  assert(length!=NULL);
  if (cstr==NULL) {
    *length=0;
    return AMX_ERR_PARAMS;
  } /* if */

  if ((ucell)*cstr>UNPACKEDMAX) {
    /* packed string */
    assert_static(sizeof(char)==1);
    len=strlen((char *)cstr);           /* find '\0' */
    assert(check_endian());
    #if BYTE_ORDER==LITTLE_ENDIAN
      /* on Little Endian machines, toggle the last bytes */
      c=cstr[len/sizeof(cell)];         /* get last cell */
      len=len - len % sizeof(cell);     /* len = multiple of "cell" bytes */
      while ((c & CHARMASK)!=0) {
        len++;
        c <<= 8*sizeof(char);
      } /* if */
    #endif
  } else {
    for (len=0; cstr[len]!=0; len++)
      /* nothing */;
  } /* if */
  *length = len;
  return AMX_ERR_NONE;
}
#endif

#if defined AMX_XXXSTRING || defined AMX_EXEC
int AMXAPI amx_SetString(cell *dest,const char *source,int pack,int use_wchar,size_t size)
{                 /* the memory blocks should not overlap */
  int len, i;

  assert_static(UNLIMITED>0);
  #if defined AMX_ANSIONLY
    (void)use_wchar;
    len=strlen(source);
  #else
    len= use_wchar ? wcslen((const wchar_t*)source) : strlen(source);
  #endif
  if (pack) {
    /* create a packed string */
    if (size<UNLIMITED/sizeof(cell) && (size_t)len>=size*sizeof(cell))
      len=size*sizeof(cell)-1;
    dest[len/sizeof(cell)]=0;   /* clear last bytes of last (semi-filled) cell*/
    #if defined AMX_ANSIONLY
      memcpy(dest,source,len);
    #else
      if (use_wchar) {
        for (i=0; i<len; i++)
          ((char*)dest)[i]=(char)(((wchar_t*)source)[i]);
      } else {
        memcpy(dest,source,len);
      } /* if */
    #endif
    /* On Big Endian machines, the characters are well aligned in the
     * cells; on Little Endian machines, we must swap all cells.
     */
    assert(check_endian());
    #if BYTE_ORDER==LITTLE_ENDIAN
      len /= sizeof(cell);
      while (len>=0)
        swapcell((ucell *)&dest[len--]);
    #endif

  } else {
    /* create an unpacked string */
    if (size<UNLIMITED && (size_t)len>=size)
      len=size-1;
    #if defined AMX_ANSIONLY
      for (i=0; i<len; i++)
        dest[i]=(cell)source[i];
    #else
      if (use_wchar) {
        for (i=0; i<len; i++)
          dest[i]=(cell)(((wchar_t*)source)[i]);
      } else {
        for (i=0; i<len; i++)
          dest[i]=(cell)source[i];
      } /* if */
    #endif
    dest[len]=0;
  } /* if */
  return AMX_ERR_NONE;
}
#endif

#if defined AMX_XXXSTRING
int AMXAPI amx_GetString(char *dest,const cell *source,int use_wchar,size_t size)
{
  int len=0;
  #if defined AMX_ANSIONLY
    (void)use_wchar;    /* unused parameter (if ANSI only) */
  #endif
  if ((ucell)*source>UNPACKEDMAX) {
    /* source string is packed */
    cell c=0;           /* initialize to 0 to avoid a compiler warning */
    int i=sizeof(cell)-1;
    char ch;
    while ((size_t)len<size) {
      if (i==sizeof(cell)-1)
        c=*source++;
      ch=(char)(c >> i*CHARBITS);
      if (ch=='\0')
        break;          /* terminating zero character found */
      #if defined AMX_ANSIONLY
        dest[len++]=ch;
      #else
        if (use_wchar)
          ((wchar_t*)dest)[len++]=ch;
        else
          dest[len++]=ch;
      #endif
      i=(i+sizeof(cell)-1) % sizeof(cell);
    } /* while */
  } else {
    /* source string is unpacked */
    #if defined AMX_ANSIONLY
      while (*source!=0 && (size_t)len<size)
        dest[len++]=(char)*source++;
    #else
      if (use_wchar) {
        while (*source!=0 && (size_t)len<size)
          ((wchar_t*)dest)[len++]=(wchar_t)*source++;
      } else {
        while (*source!=0 && (size_t)len<size)
          dest[len++]=(char)*source++;
      } /* if */
    #endif
  } /* if */
  /* store terminator */
  if ((size_t)len>=size)
    len=size-1;
  if (len>=0) {
    #if defined AMX_ANSIONLY
      dest[len]='\0';
    #else
      if (use_wchar)
        ((wchar_t*)dest)[len]=0;
      else
        dest[len]='\0';
    #endif
  } /* IF */
  return AMX_ERR_NONE;
}
#endif /* AMX_XXXSTRING */

#if defined AMX_UTF8XXX
  #if defined __BORLANDC__
    #pragma warn -amb -8000     /* ambiguous operators need parentheses */
  #endif
/* amx_UTF8Get()
 * Extract a single UTF-8 encoded character from a string and return a pointer
 * to the character just behind that UTF-8 character. The parameters "endptr"
 * and "value" may be NULL.
 * If the code is not valid UTF-8, "endptr" has the value of the input
 * parameter "string" and "value" is zero.
 */
int AMXAPI amx_UTF8Get(const char *string, const char **endptr, cell *value)
{
static const char utf8_count[16]={ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 4 };
static const long utf8_lowmark[5] = { 0x80, 0x800, 0x10000L, 0x200000L, 0x4000000L };
  unsigned char c;
  cell result;
  int followup;

  assert(string!=NULL);
  if (value!=NULL)      /* preset, in case of an error */
    *value=0;
  if (endptr!=NULL)
    *endptr=string;

  c = *(const unsigned char*)string++;
  if (c<0x80) {
    /* ASCII */
    result=c;
  } else {
    if (c<0xc0 || c>=0xfe)
      return AMX_ERR_PARAMS;  /* invalid or "follower" code, quit with error */
    /* At this point we know that the two top bits of c are ones. The two
     * bottom bits are always part of the code. We only need to consider
     * the 4 remaining bits; i.e., a 16-byte table. This is "utf8_count[]".
     * (Actually the utf8_count[] table records the number of follow-up
     * bytes minus 1. This is just for convenience.)
     */
    assert((c & 0xc0)==0xc0);
    followup=(int)utf8_count[(c >> 2) & 0x0f];
    /* The mask depends on the code length; this is just a very simple
     * relation.
     */
    #define utf8_mask   (0x1f >> followup)
    result= c & utf8_mask;
    /* Collect the follow-up codes using a drop-through switch statement;
     * this avoids a loop. In each case, verify the two leading bits.
     */
    assert(followup>=0 && followup<=4);
    switch (followup) {
    case 4:
      if (((c=*string++) & 0xc0) != 0x80) goto error;
      result = (result << 6) | c & 0x3f;
    case 3:
      if (((c=*string++) & 0xc0) != 0x80) goto error;
      result = (result << 6) | c & 0x3f;
    case 2:
      if (((c=*string++) & 0xc0) != 0x80) goto error;
      result = (result << 6) | c & 0x3f;
    case 1:
      if (((c=*string++) & 0xc0) != 0x80) goto error;
      result = (result << 6) | c & 0x3f;
    case 0:
      if (((c=*string++) & 0xc0) != 0x80) goto error;
      result = (result << 6) | c & 0x3f;
    } /* switch */
    /* Do additional checks: shortest encoding & reserved positions. The
     * lowmark limits also depends on the code length; it can be read from
     * a table with 5 elements. This is "utf8_lowmark[]".
     */
    if (result<utf8_lowmark[followup])
      goto error;
    if (result>=0xd800 && result<=0xdfff || result==0xfffe || result==0xffff)
      goto error;
  } /* if */

  if (value!=NULL)
    *value=result;
  if (endptr!=NULL)
    *endptr=string;

  return AMX_ERR_NONE;

error:
  return AMX_ERR_PARAMS;
}

/* amx_UTF8Put()
 * Encode a single character into a byte string. The character may result in
 * a string of up to 6 bytes. The function returns an error code if "maxchars"
 * is lower than the required number of characters; in this case nothing is
 * stored.
 * The function does not zero-terminate the string.
 */
int AMXAPI amx_UTF8Put(char *string, char **endptr, int maxchars, cell value)
{
  assert(string!=NULL);
  if (endptr!=NULL)     /* preset, in case of an error */
    *endptr=string;

  if (value<0x80) {
    /* 0xxxxxxx */
    if (maxchars < 1) goto error;
    *string++ = (char)value;
  } else if (value<0x800) {
    /* 110xxxxx 10xxxxxx */
    if (maxchars < 2) goto error;
    *string++ = (char)((value>>6) & 0x1f | 0xc0);
    *string++ = (char)(value & 0x3f | 0x80);
  } else if (value<0x10000) {
    /* 1110xxxx 10xxxxxx 10xxxxxx (16 bits, BMP plane) */
    if (maxchars < 3) goto error;
    if (value>=0xd800 && value<=0xdfff || value==0xfffe || value==0xffff)
      goto error;       /* surrogate pairs and invalid characters */
    *string++ = (char)((value>>12) & 0x0f | 0xe0);
    *string++ = (char)((value>>6) & 0x3f | 0x80);
    *string++ = (char)(value & 0x3f | 0x80);
  } else if (value<0x200000) {
    /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if (maxchars < 4) goto error;
    *string++ = (char)((value>>18) & 0x07 | 0xf0);
    *string++ = (char)((value>>12) & 0x3f | 0x80);
    *string++ = (char)((value>>6) & 0x3f | 0x80);
    *string++ = (char)(value & 0x3f | 0x80);
  } else if (value<0x4000000) {
    /* 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if (maxchars < 5) goto error;
    *string++ = (char)((value>>24) & 0x03 | 0xf8);
    *string++ = (char)((value>>18) & 0x3f | 0x80);
    *string++ = (char)((value>>12) & 0x3f | 0x80);
    *string++ = (char)((value>>6) & 0x3f | 0x80);
    *string++ = (char)(value & 0x3f | 0x80);
  } else {
    /* 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx (31 bits) */
    if (maxchars < 6) goto error;
    *string++ = (char)((value>>30) & 0x01 | 0xfc);
    *string++ = (char)((value>>24) & 0x3f | 0x80);
    *string++ = (char)((value>>18) & 0x3f | 0x80);
    *string++ = (char)((value>>12) & 0x3f | 0x80);
    *string++ = (char)((value>>6) & 0x3f | 0x80);
    *string++ = (char)(value & 0x3f | 0x80);
  } /* if */

  if (endptr!=NULL)
    *endptr=string;
  return AMX_ERR_NONE;

error:
  return AMX_ERR_PARAMS;
}

/* amx_UTF8Check()
 * Run through a zero-terminated string and check the validity of the UTF-8
 * encoding. The function returns an error code, it is AMX_ERR_NONE if the
 * string is valid UTF-8 (or valid ASCII for that matter).
 */
int AMXAPI amx_UTF8Check(const char *string, int *length)
{
  int err=AMX_ERR_NONE;
  int len=0;
  while (err==AMX_ERR_NONE && *string!='\0') {
    err=amx_UTF8Get(string,&string,NULL);
    len++;
  } /* while */
  if (length!=NULL)
    *length=len;
  return err;
}

/* amx_UTF8Len()
 * Run through a wide string and return how many 8-bit characters are needed to
 * store the string in UTF-8 format. The returned cound excludes the terminating
 * zero byte. The function returns an error code.
 */
int AMXAPI amx_UTF8Len(const cell *cstr, int *length)
{
  int err;

  assert(length!=NULL);
  err=amx_StrLen(cstr, length);
  if (err==AMX_ERR_NONE && (ucell)*cstr<=UNPACKEDMAX) {
    char buffer[10];  /* maximum UTF-8 code is 6 characters */
    char *endptr;
    int len=*length, count=0;
    while (len-->0) {
      amx_UTF8Put(buffer, &endptr, sizeof buffer, *cstr++);
      count+=(int)(endptr-buffer);
    } /* while */
    *length=count;
  } /* while */
  return err;
}
#endif /* AMX_UTF8XXX */
