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
#if defined __GNUC__ || defined __ICC || defined ASM32
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


#define STKMARGIN       ((cell)(16*sizeof(cell)))

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

#if defined AMX_EXEC || defined AMX_INIT

/* It is assumed that the abstract machine can simply access the memory area
 * for the global data and the stack. If this is not the case, you need to
 * define the macro sets _R() and _W(), for reading and writing to memory.
 */
#if !defined _R
  #define _R_DEFAULT            /* mark default memory access */
  #define _R(base,addr)         (* (cell *)((unsigned char*)(base)+(int)(addr)))
  #define _R8(base,addr)        (* (unsigned char *)((unsigned char*)(base)+(int)(addr)))
  #define _R16(base,addr)       (* (uint16_t *)((unsigned char*)(base)+(int)(addr)))
  #define _R32(base,addr)       (* (uint32_t *)((unsigned char*)(base)+(int)(addr)))
#endif
#if !defined _W
  #define _W_DEFAULT            /* mark default memory access */
  #define _W(base,addr,value)   ((*(cell *)((unsigned char*)(base)+(int)(addr)))=(cell)(value))
  #define _W8(base,addr,value)  ((*(unsigned char *)((unsigned char*)(base)+(int)(addr)))=(unsigned char)(value))
  #define _W16(base,addr,value) ((*(uint16_t *)((unsigned char*)(base)+(int)(addr)))=(uint16_t)(value))
  #define _W32(base,addr,value) ((*(uint32_t *)((unsigned char*)(base)+(int)(addr)))=(uint32_t)(value))
#endif

#if -8/3==-2 && 8/-3==-2
  #define TRUNC_SDIV    /* signed divisions are truncated on this platform */
#else
  #define IABS(a)       ((a)>=0 ? (a) : (-a))
#endif

/* The pseudo-instructions come from the code stream. Normally, these are just
 * accessed from memory. When the instructions must be fetched in some other
 * way, the definition below must be pre-defined.
 * N.B.:
 *   - reading from a code address should increment the instruction pointer
 *     (called "cip")
 *   - only cell-sized accesses occur in code memory
 */
#if !defined _RCODE
  #define _RCODE()      ( *cip++ )
#endif

#if !defined GETPARAM
  #define GETPARAM(v)   ( v=_RCODE() )   /* read a parameter from the opcode stream */
#endif
#define SKIPPARAM(n)    ( cip=(cell *)cip+(n) ) /* for obsolete opcodes */

/* PUSH() and POP() are defined in terms of the _R() and _W() macros */
#define PUSH(v)         ( stk-=sizeof(cell), _W(data,stk,v) )
#define POP(v)          ( v=_R(data,stk), stk+=sizeof(cell) )

#define ABORT(amx,v)    { (amx)->stk=reset_stk; (amx)->hea=reset_hea; return v; }

#define CHKMARGIN()     if (hea+STKMARGIN>stk) return AMX_ERR_STACKERR
#define CHKSTACK()      if (stk>amx->stp) return AMX_ERR_STACKLOW
#define CHKHEAP()       if (hea<amx->hlw) return AMX_ERR_HEAPLOW

#if (defined __GNUC__ || defined __ICC) && !(defined ASM32 || defined JIT)
    /* GNU C version uses the "labels as values" extension to create
     * fast "indirect threaded" interpreter. The Intel C/C++ compiler
     * supports this too.
     */

#define NEXT(cip)       goto **cip++

int AMXAPI amx_Exec(AMX *amx, cell *retval, int index)
{
static const void * const amx_opcodelist[] = {
        &&op_none,      &&op_load_pri,  &&op_load_alt,  &&op_load_s_pri,
        &&op_load_s_alt,&&op_lref_pri,  &&op_lref_alt,  &&op_lref_s_pri,
        &&op_lref_s_alt,&&op_load_i,    &&op_lodb_i,    &&op_const_pri,
        &&op_const_alt, &&op_addr_pri,  &&op_addr_alt,  &&op_stor_pri,
        &&op_stor_alt,  &&op_stor_s_pri,&&op_stor_s_alt,&&op_sref_pri,
        &&op_sref_alt,  &&op_sref_s_pri,&&op_sref_s_alt,&&op_stor_i,
        &&op_strb_i,    &&op_lidx,      &&op_lidx_b,    &&op_idxaddr,
        &&op_idxaddr_b, &&op_align_pri, &&op_align_alt, &&op_lctrl,
        &&op_sctrl,     &&op_move_pri,  &&op_move_alt,  &&op_xchg,
        &&op_push_pri,  &&op_push_alt,  &&op_push_r,    &&op_push_c,
        &&op_push,      &&op_push_s,    &&op_pop_pri,   &&op_pop_alt,
        &&op_stack,     &&op_heap,      &&op_proc,      &&op_ret,
        &&op_retn,      &&op_call,      &&op_call_pri,  &&op_jump,
        &&op_jrel,      &&op_jzer,      &&op_jnz,       &&op_jeq,
        &&op_jneq,      &&op_jless,     &&op_jleq,      &&op_jgrtr,
        &&op_jgeq,      &&op_jsless,    &&op_jsleq,     &&op_jsgrtr,
        &&op_jsgeq,     &&op_shl,       &&op_shr,       &&op_sshr,
        &&op_shl_c_pri, &&op_shl_c_alt, &&op_shr_c_pri, &&op_shr_c_alt,
        &&op_smul,      &&op_sdiv,      &&op_sdiv_alt,  &&op_umul,
        &&op_udiv,      &&op_udiv_alt,  &&op_add,       &&op_sub,
        &&op_sub_alt,   &&op_and,       &&op_or,        &&op_xor,
        &&op_not,       &&op_neg,       &&op_invert,    &&op_add_c,
        &&op_smul_c,    &&op_zero_pri,  &&op_zero_alt,  &&op_zero,
        &&op_zero_s,    &&op_sign_pri,  &&op_sign_alt,  &&op_eq,
        &&op_neq,       &&op_less,      &&op_leq,       &&op_grtr,
        &&op_geq,       &&op_sless,     &&op_sleq,      &&op_sgrtr,
        &&op_sgeq,      &&op_eq_c_pri,  &&op_eq_c_alt,  &&op_inc_pri,
        &&op_inc_alt,   &&op_inc,       &&op_inc_s,     &&op_inc_i,
        &&op_dec_pri,   &&op_dec_alt,   &&op_dec,       &&op_dec_s,
        &&op_dec_i,     &&op_movs,      &&op_cmps,      &&op_fill,
        &&op_halt,      &&op_bounds,    &&op_sysreq_pri,&&op_sysreq_c,
        &&op_file,      &&op_line,      &&op_symbol,    &&op_srange,
        &&op_jump_pri,  &&op_switch,    &&op_casetbl,   &&op_swap_pri,
        &&op_swap_alt,  &&op_push_adr,  &&op_nop,       &&op_sysreq_n,
        &&op_symtag,    &&op_break,     &&op_push2_c,   &&op_push2,
        &&op_push2_s,   &&op_push2_adr, &&op_push3_c,   &&op_push3,
        &&op_push3_s,   &&op_push3_adr, &&op_push4_c,   &&op_push4,
        &&op_push4_s,   &&op_push4_adr, &&op_push5_c,   &&op_push5,
        &&op_push5_s,   &&op_push5_adr, &&op_load_both, &&op_load_s_both,
        &&op_const,     &&op_const_s,   &&op_sysreq_d,  &&op_sysreq_nd };
  AMX_HEADER *hdr;
  AMX_FUNCSTUB *func;
  unsigned char *code, *data;
  cell pri,alt,stk,frm,hea;
  cell reset_stk, reset_hea, *cip;
  cell offs,val;
  ucell codesize;
  int num,i;

  /* HACK: return label table (for amx_BrowseRelocate) if amx structure
   * has the AMX_FLAG_BROWSE flag set.
   */
  assert(amx!=NULL);
  if ((amx->flags & AMX_FLAG_BROWSE)==AMX_FLAG_BROWSE) {
    assert_static(sizeof(cell)==sizeof(void *));
    assert(retval!=NULL);
    *retval=(cell)amx_opcodelist;
    return 0;
  } /* if */

  if (amx->callback==NULL)
    return AMX_ERR_CALLBACK;
  if ((amx->flags & AMX_FLAG_RELOC)==0)
    return AMX_ERR_INIT;
  if ((amx->flags & AMX_FLAG_NTVREG)==0) {
    if ((num=amx_Register(amx,NULL,0))!=AMX_ERR_NONE)
      return num;
  } /* if */
  assert((amx->flags & AMX_FLAG_BROWSE)==0);

  /* set up the registers */
  hdr=(AMX_HEADER *)amx->base;
  assert(hdr->magic==AMX_MAGIC);
  codesize=(ucell)(hdr->dat-hdr->cod);
  code=amx->base+(int)hdr->cod;
  data=(amx->data!=NULL) ? amx->data : amx->base+(int)hdr->dat;
  hea=amx->hea;
  stk=amx->stk;
  reset_stk=stk;
  reset_hea=hea;
  alt=frm=pri=0;/* just to avoid compiler warnings */
  num=0;        /* just to avoid compiler warnings */

  /* get the start address */
  if (index==AMX_EXEC_MAIN) {
    if (hdr->cip<0)
      return AMX_ERR_INDEX;
    cip=(cell *)(code + (int)hdr->cip);
  } else if (index==AMX_EXEC_CONT) {
    /* all registers: pri, alt, frm, cip, hea, stk, reset_stk, reset_hea */
    frm=amx->frm;
    stk=amx->stk;
    hea=amx->hea;
    pri=amx->pri;
    alt=amx->alt;
    reset_stk=amx->reset_stk;
    reset_hea=amx->reset_hea;
    cip=(cell *)(code + (int)amx->cip);
  } else if (index<0) {
    return AMX_ERR_INDEX;
  } else {
    if (index>=(int)NUMPUBLICS(hdr))
      return AMX_ERR_INDEX;
    func=GETENTRY(hdr,publics,index);
    cip=(cell *)(code + (int)func->address);
  } /* if */
  /* check values just copied */
  CHKSTACK();
  CHKHEAP();
  assert(check_endian());

  /* sanity checks */
  assert_static(OP_PUSH_PRI==36);
  assert_static(OP_PROC==46);
  assert_static(OP_SHL==65);
  assert_static(OP_SMUL==72);
  assert_static(OP_EQ==95);
  assert_static(OP_INC_PRI==107);
  assert_static(OP_MOVS==117);
  assert_static(OP_SYMBOL==126);
  #if PAWN_CELL_SIZE==16
    assert_static(sizeof(cell)==2);
  #elif PAWN_CELL_SIZE==32
    assert_static(sizeof(cell)==4);
  #elif PAWN_CELL_SIZE==64
    assert_static(sizeof(cell)==8);
  #else
    #error Unsupported cell size
  #endif

  if (index!=AMX_EXEC_CONT) {
    reset_stk+=amx->paramcount*sizeof(cell);
    PUSH(amx->paramcount*sizeof(cell));
    amx->paramcount=0;          /* push the parameter count to the stack & reset */
    PUSH(0);                    /* zero return address */
  } /* if */
  /* check stack/heap before starting to run */
  CHKMARGIN();

  /* start running */
  NEXT(cip);

  op_none:
    ABORT(amx,AMX_ERR_INVINSTR);
  op_load_pri:
    GETPARAM(offs);
    pri=_R(data,offs);
    NEXT(cip);
  op_load_alt:
    GETPARAM(offs);
    alt=_R(data,offs);
    NEXT(cip);
  op_load_s_pri:
    GETPARAM(offs);
    pri=_R(data,frm+offs);
    NEXT(cip);
  op_load_s_alt:
    GETPARAM(offs);
    alt=_R(data,frm+offs);
    NEXT(cip);
  op_lref_pri:
    GETPARAM(offs);
    offs=_R(data,offs);
    pri=_R(data,offs);
    NEXT(cip);
  op_lref_alt:
    GETPARAM(offs);
    offs=_R(data,offs);
    alt=_R(data,offs);
    NEXT(cip);
  op_lref_s_pri:
    GETPARAM(offs);
    offs=_R(data,frm+offs);
    pri=_R(data,offs);
    NEXT(cip);
  op_lref_s_alt:
    GETPARAM(offs);
    offs=_R(data,frm+offs);
    alt=_R(data,offs);
    NEXT(cip);
  op_load_i:
    /* verify address */
    if (pri>=hea && pri<stk || (ucell)pri>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    pri=_R(data,pri);
    NEXT(cip);
  op_lodb_i:
    GETPARAM(offs);
    /* verify address */
    if (pri>=hea && pri<stk || (ucell)pri>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    switch (offs) {
    case 1:
      pri=_R8(data,pri);
      break;
    case 2:
      pri=_R16(data,pri);
      break;
    case 4:
      pri=_R32(data,pri);
      break;
    } /* switch */
    NEXT(cip);
  op_const_pri:
    GETPARAM(pri);
    NEXT(cip);
  op_const_alt:
    GETPARAM(alt);
    NEXT(cip);
  op_addr_pri:
    GETPARAM(pri);
    pri+=frm;
    NEXT(cip);
  op_addr_alt:
    GETPARAM(alt);
    alt+=frm;
    NEXT(cip);
  op_stor_pri:
    GETPARAM(offs);
    _W(data,offs,pri);
    NEXT(cip);
  op_stor_alt:
    GETPARAM(offs);
    _W(data,offs,alt);
    NEXT(cip);
  op_stor_s_pri:
    GETPARAM(offs);
    _W(data,frm+offs,pri);
    NEXT(cip);
  op_stor_s_alt:
    GETPARAM(offs);
    _W(data,frm+offs,alt);
    NEXT(cip);
  op_sref_pri:
    GETPARAM(offs);
    offs=_R(data,offs);
    _W(data,offs,pri);
    NEXT(cip);
  op_sref_alt:
    GETPARAM(offs);
    offs=_R(data,offs);
    _W(data,offs,alt);
    NEXT(cip);
  op_sref_s_pri:
    GETPARAM(offs);
    offs=_R(data,frm+offs);
    _W(data,offs,pri);
    NEXT(cip);
  op_sref_s_alt:
    GETPARAM(offs);
    offs=_R(data,frm+offs);
    _W(data,offs,alt);
    NEXT(cip);
  op_stor_i:
    /* verify address */
    if (alt>=hea && alt<stk || (ucell)alt>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    _W(data,alt,pri);
    NEXT(cip);
  op_strb_i:
    GETPARAM(offs);
    /* verify address */
    if (alt>=hea && alt<stk || (ucell)alt>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    switch (offs) {
    case 1:
      _W8(data,alt,pri);
      break;
    case 2:
      _W16(data,alt,pri);
      break;
    case 4:
      _W32(data,alt,pri);
      break;
    } /* switch */
    NEXT(cip);
  op_lidx:
    offs=pri*sizeof(cell)+alt;  /* implicit shift value for a cell */
    /* verify address */
    if (offs>=hea && offs<stk || (ucell)offs>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    pri=_R(data,offs);
    NEXT(cip);
  op_lidx_b:
    GETPARAM(offs);
    offs=(pri << (int)offs)+alt;
    /* verify address */
    if (offs>=hea && offs<stk || (ucell)offs>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    pri=_R(data,offs);
    NEXT(cip);
  op_idxaddr:
    pri=pri*sizeof(cell)+alt;
    NEXT(cip);
  op_idxaddr_b:
    GETPARAM(offs);
    pri=(pri << (int)offs)+alt;
    NEXT(cip);
  op_align_pri:
    GETPARAM(offs);
    #if BYTE_ORDER==LITTLE_ENDIAN
      if (offs<(int)sizeof(cell))
        pri ^= sizeof(cell)-offs;
    #endif
    NEXT(cip);
  op_align_alt:
    GETPARAM(offs);
    #if BYTE_ORDER==LITTLE_ENDIAN
      if (offs<(int)sizeof(cell))
        alt ^= sizeof(cell)-offs;
    #endif
    NEXT(cip);
  op_lctrl:
    GETPARAM(offs);
    switch (offs) {
    case 0:
      pri=hdr->cod;
      break;
    case 1:
      pri=hdr->dat;
      break;
    case 2:
      pri=hea;
      break;
    case 3:
      pri=amx->stp;
      break;
    case 4:
      pri=stk;
      break;
    case 5:
      pri=frm;
      break;
    case 6:
      pri=(cell)((unsigned char *)cip - code);
      break;
    } /* switch */
    NEXT(cip);
  op_sctrl:
    GETPARAM(offs);
    switch (offs) {
    case 0:
    case 1:
    case 3:
      /* cannot change these parameters */
      break;
    case 2:
      hea=pri;
      break;
    case 4:
      stk=pri;
      break;
    case 5:
      frm=pri;
      break;
    case 6:
      cip=(cell *)(code + (int)pri);
      break;
    } /* switch */
    NEXT(cip);
  op_move_pri:
    pri=alt;
    NEXT(cip);
  op_move_alt:
    alt=pri;
    NEXT(cip);
  op_xchg:
    offs=pri;         /* offs is a temporary variable */
    pri=alt;
    alt=offs;
    NEXT(cip);
  op_push_pri:
    PUSH(pri);
    NEXT(cip);
  op_push_alt:
    PUSH(alt);
    NEXT(cip);
  op_push_c:
    GETPARAM(offs);
    PUSH(offs);
    NEXT(cip);
  op_push_r:
    GETPARAM(offs);
    while (offs--)
      PUSH(pri);
    NEXT(cip);
  op_push:
    GETPARAM(offs);
    PUSH(_R(data,offs));
    NEXT(cip);
  op_push_s:
    GETPARAM(offs);
    PUSH(_R(data,frm+offs));
    NEXT(cip);
  op_pop_pri:
    POP(pri);
    NEXT(cip);
  op_pop_alt:
    POP(alt);
    NEXT(cip);
  op_stack:
    GETPARAM(offs);
    alt=stk;
    stk+=offs;
    CHKMARGIN();
    CHKSTACK();
    NEXT(cip);
  op_heap:
    GETPARAM(offs);
    alt=hea;
    hea+=offs;
    CHKMARGIN();
    CHKHEAP();
    NEXT(cip);
  op_proc:
    PUSH(frm);
    frm=stk;
    CHKMARGIN();
    NEXT(cip);
  op_ret:
    POP(frm);
    POP(offs);
    /* verify the return address */
    if ((ucell)offs>=codesize)
      ABORT(amx,AMX_ERR_MEMACCESS);
    cip=(cell *)(code+(int)offs);
    NEXT(cip);
  op_retn:
    POP(frm);
    POP(offs);
    /* verify the return address */
    if ((ucell)offs>=codesize)
      ABORT(amx,AMX_ERR_MEMACCESS);
    cip=(cell *)(code+(int)offs);
    stk+= _R(data,stk) + sizeof(cell);  /* remove parameters from the stack */
    NEXT(cip);
  op_call:
    PUSH(((unsigned char *)cip-code)+sizeof(cell));/* push address behind instruction */
    cip=JUMPABS(code, cip);                     /* jump to the address */
    NEXT(cip);
  op_call_pri:
    PUSH((unsigned char *)cip-code);
    cip=(cell *)(code+(int)pri);
    NEXT(cip);
  op_jump:
    /* since the GETPARAM() macro modifies cip, you cannot
     * do GETPARAM(cip) directly */
    cip=JUMPABS(code, cip);
    NEXT(cip);
  op_jrel:
    offs=*cip;
    cip=(cell *)((unsigned char *)cip + (int)offs + sizeof(cell));
    NEXT(cip);
  op_jzer:
    if (pri==0)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jnz:
    if (pri!=0)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jeq:
    if (pri==alt)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jneq:
    if (pri!=alt)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jless:
    if ((ucell)pri < (ucell)alt)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jleq:
    if ((ucell)pri <= (ucell)alt)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jgrtr:
    if ((ucell)pri > (ucell)alt)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jgeq:
    if ((ucell)pri >= (ucell)alt)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jsless:
    if (pri<alt)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jsleq:
    if (pri<=alt)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jsgrtr:
    if (pri>alt)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_jsgeq:
    if (pri>=alt)
      cip=JUMPABS(code, cip);
    else
      cip=(cell *)((unsigned char *)cip+sizeof(cell));
    NEXT(cip);
  op_shl:
    pri<<=alt;
    NEXT(cip);
  op_shr:
    pri=(ucell)pri >> (ucell)alt;
    NEXT(cip);
  op_sshr:
    pri>>=alt;
    NEXT(cip);
  op_shl_c_pri:
    GETPARAM(offs);
    pri<<=offs;
    NEXT(cip);
  op_shl_c_alt:
    GETPARAM(offs);
    alt<<=offs;
    NEXT(cip);
  op_shr_c_pri:
    GETPARAM(offs);
    pri=(ucell)pri >> (ucell)offs;
    NEXT(cip);
  op_shr_c_alt:
    GETPARAM(offs);
    alt=(ucell)alt >> (ucell)offs;
    NEXT(cip);
  op_smul:
    pri*=alt;
    NEXT(cip);
  op_sdiv:
    if (alt==0)
      ABORT(amx,AMX_ERR_DIVIDE);
    /* use floored division and matching remainder */
    offs=alt;
    #if defined TRUNC_SDIV
      pri=pri/offs;
      alt=pri%offs;
    #else
      val=pri;                  /* portable routine for truncated division */
      pri=IABS(pri)/IABS(offs);
      if ((cell)(val ^ offs)<0)
        pri=-pri;
      alt=val-pri*offs;         /* calculate the matching remainder */
    #endif
    /* now "fiddle" with the values to get floored division */
    if (alt!=0 && (cell)(alt ^ offs)<0) {
      pri--;
      alt+=offs;
    } /* if */
    NEXT(cip);
  op_sdiv_alt:
    if (pri==0)
      ABORT(amx,AMX_ERR_DIVIDE);
    /* use floored division and matching remainder */
    offs=pri;
    #if defined TRUNC_SDIV
      pri=alt/offs;
      alt=alt%offs;
    #else
      val=alt;                  /* portable routine for truncated division */
      pri=IABS(alt)/IABS(offs);
      if ((cell)(val ^ offs)<0)
        pri=-pri;
      alt=val-pri*offs;         /* calculate the matching remainder */
    #endif
    /* now "fiddle" with the values to get floored division */
    if (alt!=0 && (cell)(alt ^ offs)<0) {
      pri--;
      alt+=offs;
    } /* if */
    NEXT(cip);
  op_umul:
    pri=(ucell)pri * (ucell)alt;
    NEXT(cip);
  op_udiv:
    if (alt==0)
      ABORT(amx,AMX_ERR_DIVIDE);
    offs=(ucell)pri % (ucell)alt;     /* temporary storage */
    pri=(ucell)pri / (ucell)alt;
    alt=offs;
    NEXT(cip);
  op_udiv_alt:
    if (pri==0)
      ABORT(amx,AMX_ERR_DIVIDE);
    offs=(ucell)alt % (ucell)pri;     /* temporary storage */
    pri=(ucell)alt / (ucell)pri;
    alt=offs;
    NEXT(cip);
  op_add:
    pri+=alt;
    NEXT(cip);
  op_sub:
    pri-=alt;
    NEXT(cip);
  op_sub_alt:
    pri=alt-pri;
    NEXT(cip);
  op_and:
    pri&=alt;
    NEXT(cip);
  op_or:
    pri|=alt;
    NEXT(cip);
  op_xor:
    pri^=alt;
    NEXT(cip);
  op_not:
    pri=!pri;
    NEXT(cip);
  op_neg:
    pri=-pri;
    NEXT(cip);
  op_invert:
    pri=~pri;
    NEXT(cip);
  op_add_c:
    GETPARAM(offs);
    pri+=offs;
    NEXT(cip);
  op_smul_c:
    GETPARAM(offs);
    pri*=offs;
    NEXT(cip);
  op_zero_pri:
    pri=0;
    NEXT(cip);
  op_zero_alt:
    alt=0;
    NEXT(cip);
  op_zero:
    GETPARAM(offs);
    _W(data,offs,0);
    NEXT(cip);
  op_zero_s:
    GETPARAM(offs);
    _W(data,frm+offs,0);
    NEXT(cip);
  op_sign_pri:
    if ((pri & 0xff)>=0x80)
      pri|= ~ (ucell)0xff;
    NEXT(cip);
  op_sign_alt:
    if ((alt & 0xff)>=0x80)
      alt|= ~ (ucell)0xff;
    NEXT(cip);
  op_eq:
    pri= pri==alt ? 1 : 0;
    NEXT(cip);
  op_neq:
    pri= pri!=alt ? 1 : 0;
    NEXT(cip);
  op_less:
    pri= (ucell)pri < (ucell)alt ? 1 : 0;
    NEXT(cip);
  op_leq:
    pri= (ucell)pri <= (ucell)alt ? 1 : 0;
    NEXT(cip);
  op_grtr:
    pri= (ucell)pri > (ucell)alt ? 1 : 0;
    NEXT(cip);
  op_geq:
    pri= (ucell)pri >= (ucell)alt ? 1 : 0;
    NEXT(cip);
  op_sless:
    pri= pri<alt ? 1 : 0;
    NEXT(cip);
  op_sleq:
    pri= pri<=alt ? 1 : 0;
    NEXT(cip);
  op_sgrtr:
    pri= pri>alt ? 1 : 0;
    NEXT(cip);
  op_sgeq:
    pri= pri>=alt ? 1 : 0;
    NEXT(cip);
  op_eq_c_pri:
    GETPARAM(offs);
    pri= pri==offs ? 1 : 0;
    NEXT(cip);
  op_eq_c_alt:
    GETPARAM(offs);
    pri= alt==offs ? 1 : 0;
    NEXT(cip);
  op_inc_pri:
    pri++;
    NEXT(cip);
  op_inc_alt:
    alt++;
    NEXT(cip);
  op_inc:
    GETPARAM(offs);
    #if defined _R_DEFAULT
      *(cell *)(data+(int)offs) += 1;
    #else
      val=_R(data,offs);
      _W(data,offs,val+1);
    #endif
    NEXT(cip);
  op_inc_s:
    GETPARAM(offs);
    #if defined _R_DEFAULT
      *(cell *)(data+(int)(frm+offs)) += 1;
    #else
      val=_R(data,frm+offs);
      _W(data,frm+offs,val+1);
    #endif
    NEXT(cip);
  op_inc_i:
    #if defined _R_DEFAULT
      *(cell *)(data+(int)pri) += 1;
    #else
      val=_R(data,pri);
      _W(data,pri,val+1);
    #endif
    NEXT(cip);
  op_dec_pri:
    pri--;
    NEXT(cip);
  op_dec_alt:
    alt--;
    NEXT(cip);
  op_dec:
    GETPARAM(offs);
    #if defined _R_DEFAULT
      *(cell *)(data+(int)offs) -= 1;
    #else
      val=_R(data,offs);
      _W(data,offs,val-1);
    #endif
    NEXT(cip);
  op_dec_s:
    GETPARAM(offs);
    #if defined _R_DEFAULT
      *(cell *)(data+(int)(frm+offs)) -= 1;
    #else
      val=_R(data,frm+offs);
      _W(data,frm+offs,val-1);
    #endif
    NEXT(cip);
  op_dec_i:
    #if defined _R_DEFAULT
      *(cell *)(data+(int)pri) -= 1;
    #else
      val=_R(data,pri);
      _W(data,pri,val-1);
    #endif
    NEXT(cip);
  op_movs:
    GETPARAM(offs);
    /* verify top & bottom memory addresses, for both source and destination
     * addresses
     */
    if (pri>=hea && pri<stk || (ucell)pri>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    if ((pri+offs)>hea && (pri+offs)<stk || (ucell)(pri+offs)>(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    if (alt>=hea && alt<stk || (ucell)alt>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    if ((alt+offs)>hea && (alt+offs)<stk || (ucell)(alt+offs)>(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    #if defined _R_DEFAULT
      memcpy(data+(int)alt, data+(int)pri, (int)offs);
    #else
      for (i=0; i+4<offs; i+=4) {
        val=_R32(data,pri+i);
        _W32(data,alt+i,val);
      } /* for */
      for ( ; i<offs; i++) {
        val=_R8(data,pri+i);
        _W8(data,alt+i,val);
      } /* for */
    #endif
    NEXT(cip);
  op_cmps:
    GETPARAM(offs);
    /* verify top & bottom memory addresses, for both source and destination
     * addresses
     */
    if (pri>=hea && pri<stk || (ucell)pri>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    if ((pri+offs)>hea && (pri+offs)<stk || (ucell)(pri+offs)>(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    if (alt>=hea && alt<stk || (ucell)alt>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    if ((alt+offs)>hea && (alt+offs)<stk || (ucell)(alt+offs)>(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    #if defined _R_DEFAULT
      pri=memcmp(data+(int)alt, data+(int)pri, (int)offs);
    #else
      pri=0;
      for (i=0; i+4<offs && pri==0; i+=4)
        pri=_R32(data,alt+i)-_R32(data,pri+i);
      for ( ; i<offs && pri==0; i++)
        pri=_R8(data,alt+i)-_R8(data,pri+i);
    #endif
    NEXT(cip);
  op_fill:
    GETPARAM(offs);
    /* verify top & bottom memory addresses */
    if (alt>=hea && alt<stk || (ucell)alt>=(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    if ((alt+offs)>hea && (alt+offs)<stk || (ucell)(alt+offs)>(ucell)amx->stp)
      ABORT(amx,AMX_ERR_MEMACCESS);
    for (i=(int)alt; offs>=(int)sizeof(cell); i+=sizeof(cell), offs-=sizeof(cell))
      _W32(data,i,pri);
    NEXT(cip);
  op_halt:
    GETPARAM(offs);
    if (retval!=NULL)
      *retval=pri;
    /* store complete status (stk and hea are already set in the ABORT macro) */
    amx->frm=frm;
    amx->pri=pri;
    amx->alt=alt;
    amx->cip=(cell)((unsigned char*)cip-code);
    if (offs==AMX_ERR_SLEEP) {
      amx->stk=stk;
      amx->hea=hea;
      amx->reset_stk=reset_stk;
      amx->reset_hea=reset_hea;
      return (int)offs;
    } /* if */
    ABORT(amx,(int)offs);
  op_bounds:
    GETPARAM(offs);
    if ((ucell)pri>(ucell)offs) {
      amx->cip=(cell)((unsigned char *)cip-code);
      ABORT(amx,AMX_ERR_BOUNDS);
    } /* if */
    NEXT(cip);
  op_sysreq_pri:
    /* save a few registers */
    amx->cip=(cell)((unsigned char *)cip-code);
    amx->hea=hea;
    amx->frm=frm;
    amx->stk=stk;
    num=amx->callback(amx,pri,&pri,(cell *)(data+(int)stk));
    if (num!=AMX_ERR_NONE) {
      if (num==AMX_ERR_SLEEP) {
        amx->pri=pri;
        amx->alt=alt;
        amx->reset_stk=reset_stk;
        amx->reset_hea=reset_hea;
        return num;
      } /* if */
      ABORT(amx,num);
    } /* if */
    NEXT(cip);
  op_sysreq_c:
    GETPARAM(offs);
    /* save a few registers */
    amx->cip=(cell)((unsigned char *)cip-code);
    amx->hea=hea;
    amx->frm=frm;
    amx->stk=stk;
    num=amx->callback(amx,offs,&pri,(cell *)(data+(int)stk));
    if (num!=AMX_ERR_NONE) {
      if (num==AMX_ERR_SLEEP) {
        amx->pri=pri;
        amx->alt=alt;
        amx->reset_stk=reset_stk;
        amx->reset_hea=reset_hea;
        return num;
      } /* if */
      ABORT(amx,num);
    } /* if */
    NEXT(cip);
  op_file:
    assert(0);                  /* this code should not occur during execution */
    ABORT(amx,AMX_ERR_INVINSTR);
  op_line:
    SKIPPARAM(2);
    NEXT(cip);
  op_symbol:
    GETPARAM(offs);
    cip=(cell *)((unsigned char *)cip + (int)offs);
    NEXT(cip);
  op_srange:
    SKIPPARAM(2);
    NEXT(cip);
  op_symtag:
    SKIPPARAM(1);
    NEXT(cip);
  op_jump_pri:
    cip=(cell *)(code+(int)pri);
    NEXT(cip);
  op_switch: {
    cell *cptr;
    cptr=JUMPABS(code,cip)+1;   /* +1, to skip the "casetbl" opcode */
    cip=JUMPABS(code,cptr+1);   /* preset to "none-matched" case */
    num=(int)*cptr;             /* number of records in the case table */
    for (cptr+=2; num>0 && *cptr!=pri; num--,cptr+=2)
      /* nothing */;
    if (num>0)
      cip=JUMPABS(code,cptr+1); /* case found */
    NEXT(cip);
    }
  op_casetbl:
    assert(0);                  /* this should not occur during execution */
    ABORT(amx,AMX_ERR_INVINSTR);
  op_swap_pri:
    offs=_R(data,stk);
    _W(data,stk,pri);
    pri=offs;
    NEXT(cip);
  op_swap_alt:
    offs=_R(data,stk);
    _W(data,stk,alt);
    alt=offs;
    NEXT(cip);
  op_push_adr:
    GETPARAM(offs);
    PUSH(frm+offs);
    NEXT(cip);
  op_nop:
    NEXT(cip);
#if !defined AMX_NO_MACRO_INSTR
  op_sysreq_n:
    GETPARAM(offs);
    GETPARAM(val);
    PUSH(val);
    /* save a few registers */
    amx->cip=(cell)((unsigned char *)cip-code);
    amx->hea=hea;
    amx->frm=frm;
    amx->stk=stk;
    num=amx->callback(amx,offs,&pri,(cell *)(data+(int)stk));
    stk+=val+4;
    if (num!=AMX_ERR_NONE) {
      if (num==AMX_ERR_SLEEP) {
        amx->pri=pri;
        amx->alt=alt;
        amx->reset_stk=reset_stk;
        amx->reset_hea=reset_hea;
        return num;
      } /* if */
      ABORT(amx,num);
    } /* if */
    NEXT(cip);
#endif
  op_break:
    assert((amx->flags & AMX_FLAG_BROWSE)==0);
    if (amx->debug!=NULL) {
      /* store status */
      amx->frm=frm;
      amx->stk=stk;
      amx->hea=hea;
      amx->cip=(cell)((unsigned char*)cip-code);
      num=amx->debug(amx);
      if (num!=AMX_ERR_NONE) {
        if (num==AMX_ERR_SLEEP) {
          amx->pri=pri;
          amx->alt=alt;
          amx->reset_stk=reset_stk;
          amx->reset_hea=reset_hea;
          return num;
        } /* if */
        ABORT(amx,num);
      } /* if */
    } /* if */
    NEXT(cip);
#if !defined AMX_NO_MACRO_INSTR
  op_push5:
    GETPARAM(offs);
    PUSH(_R(data,offs));
    /* drop through */
  op_push4:
    GETPARAM(offs);
    PUSH(_R(data,offs));
    /* drop through */
  op_push3:
    GETPARAM(offs);
    PUSH(_R(data,offs));
    /* drop through */
  op_push2:
    GETPARAM(offs);
    PUSH(_R(data,offs));
    GETPARAM(offs);
    PUSH(_R(data,offs));
    NEXT(cip);
  op_push5_s:
    GETPARAM(offs);
    PUSH(_R(data,frm+offs));
    /* drop through */
  op_push4_s:
    GETPARAM(offs);
    PUSH(_R(data,frm+offs));
    /* drop through */
  op_push3_s:
    GETPARAM(offs);
    PUSH(_R(data,frm+offs));
    /* drop through */
  op_push2_s:
    GETPARAM(offs);
    PUSH(_R(data,frm+offs));
    GETPARAM(offs);
    PUSH(_R(data,frm+offs));
    NEXT(cip);
  op_push5_c:
    GETPARAM(offs);
    PUSH(offs);
    /* drop through */
  op_push4_c:
    GETPARAM(offs);
    PUSH(offs);
    /* drop through */
  op_push3_c:
    GETPARAM(offs);
    PUSH(offs);
    /* drop through */
  op_push2_c:
    GETPARAM(offs);
    PUSH(offs);
    GETPARAM(offs);
    PUSH(offs);
    NEXT(cip);
  op_push5_adr:
    GETPARAM(offs);
    PUSH(frm+offs);
    /* drop through */
  op_push4_adr:
    GETPARAM(offs);
    PUSH(frm+offs);
    /* drop through */
  op_push3_adr:
    GETPARAM(offs);
    PUSH(frm+offs);
    /* drop through */
  op_push2_adr:
    GETPARAM(offs);
    PUSH(frm+offs);
    GETPARAM(offs);
    PUSH(frm+offs);
    NEXT(cip);
  op_load_both:
    GETPARAM(offs);
    pri=_R(data,offs);
    GETPARAM(offs);
    alt=_R(data,offs);
    NEXT(cip);
  op_load_s_both:
    GETPARAM(offs);
    pri=_R(data,frm+offs);
    GETPARAM(offs);
    alt=_R(data,frm+offs);
    NEXT(cip);
  op_const:
    GETPARAM(offs);
    GETPARAM(val);
    _W(data,offs,val);
    NEXT(cip);
  op_const_s:
    GETPARAM(offs);
    GETPARAM(val);
    _W(data,frm+offs,val);
    NEXT(cip);
#endif
#if !defined AMX_NO_MACRO_INSTR
  op_sysreq_d:          /* see op_sysreq_c */
    GETPARAM(offs);
    /* save a few registers */
    amx->cip=(cell)((unsigned char *)cip-code);
    amx->hea=hea;
    amx->frm=frm;
    amx->stk=stk;
    pri=((AMX_NATIVE)offs)(amx,(cell *)(data+(int)stk));
    if (amx->error!=AMX_ERR_NONE) {
      if (amx->error==AMX_ERR_SLEEP) {
        amx->pri=pri;
        amx->alt=alt;
        amx->reset_stk=reset_stk;
        amx->reset_hea=reset_hea;
        return AMX_ERR_SLEEP;
      } /* if */
      ABORT(amx,amx->error);
    } /* if */
    NEXT(cip);
#endif
#if !defined AMX_NO_MACRO_INSTR && !defined AMX_NO_MACRO_INSTR
  op_sysreq_nd:    /* see op_sysreq_n */
    GETPARAM(offs);
    GETPARAM(val);
    PUSH(val);
    /* save a few registers */
    amx->cip=(cell)((unsigned char *)cip-code);
    amx->hea=hea;
    amx->frm=frm;
    amx->stk=stk;
    pri=((AMX_NATIVE)offs)(amx,(cell *)(data+(int)stk));
    stk+=val+4;
    if (amx->error!=AMX_ERR_NONE) {
      if (amx->error==AMX_ERR_SLEEP) {
        amx->pri=pri;
        amx->alt=alt;
        amx->reset_stk=reset_stk;
        amx->reset_hea=reset_hea;
        return AMX_ERR_SLEEP;
      } /* if */
      ABORT(amx,amx->error);
    } /* if */
    NEXT(cip);
#endif
}

#else
    /* ANSI C & assembler versions */

#if defined ASM32 || defined JIT
  /* For Watcom C/C++ use register calling convention (faster); for
   * Microsoft C/C++ (and most other C compilers) use "cdecl".
   * The important point is that you assemble AMXEXEC.ASM with the matching
   * calling convention, or the right JIT, respectively.
   * AMXJITR.ASM is for Watcom's register calling convention, AMXJITS.ASM and
   * AMXJITSN.ASM are for "cdecl".
   */
  #if defined __WATCOMC__
    #if !defined STACKARGS  /* for AMX32.DLL */
      extern cell amx_exec_asm(cell *regs,cell *retval,cell stp,cell hea);
            /* The following pragma tells the compiler into which registers
             * the parameters have to go. */
            #pragma aux amx_exec_asm parm [eax] [edx] [ebx] [ecx];
      extern cell amx_exec_jit(cell *regs,cell *retval,cell stp,cell hea);
            #pragma aux amx_exec_jit parm [eax] [edx] [ebx] [ecx];
    #else
      extern cell __cdecl amx_exec_asm(cell *regs,cell *retval,cell stp,cell hea);
      extern cell __cdecl amx_exec_jit(cell *regs,cell *retval,cell stp,cell hea);
    #endif
  #elif defined __arm__
    /* AAPCS compliant */
    extern cell amx_exec_asm(cell *regs,cell *retval,cell stp,cell hea);
    extern cell amx_exec_jit(cell *regs,cell *retval,cell stp,cell hea);
  #elif defined __GNUC__
    /* force "cdecl" by adding an "attribute" to the declaration */
    extern cell amx_exec_asm(cell *regs,cell *retval,cell stp,cell hea) __attribute__((cdecl));
    extern cell amx_exec_jit(cell *regs,cell *retval,cell stp,cell hea) __attribute__((cdecl));
  #else
    /* force "cdecl" by specifying it as a "function class" with the "__cdecl" keyword */
    extern cell __cdecl amx_exec_asm(cell *regs,cell *retval,cell stp,cell hea);
    extern cell __cdecl amx_exec_jit(cell *regs,cell *retval,cell stp,cell hea);
  #endif
#endif /* ASM32 || JIT */

int AMXAPI amx_Exec(AMX *amx, cell *retval, int index)
{
  AMX_HEADER *hdr;
  AMX_FUNCSTUB *func;
  unsigned char *code, *data;
  cell pri,alt,stk,frm,hea;
  cell reset_stk, reset_hea, *cip;
  ucell codesize;
  int i;
  #if defined ASM32 || defined JIT
    cell  parms[9];     /* registers and parameters for assembler AMX */
  #else
    OPCODE op;
    cell offs,val;
    int num;
  #endif
  #if defined ASM32
    extern void const *amx_opcodelist[];
    #ifdef __WATCOMC__
      #pragma aux amx_opcodelist "_*"
    #endif
  #endif
  #if defined JIT
    extern void const *amx_opcodelist_jit[];
    #ifdef __WATCOMC__
      #pragma aux amx_opcodelist_jit "_*"
    #endif
  #endif

  assert(amx!=NULL);
  #if defined ASM32 || defined JIT
    /* HACK: return label table (for amx_BrowseRelocate) if amx structure
     * is not passed.
     */
    if ((amx->flags & AMX_FLAG_BROWSE)==AMX_FLAG_BROWSE) {
      assert_static(sizeof(cell)==sizeof(void *));
      assert(retval!=NULL);
      #if defined ASM32 && defined JIT
        if ((amx->flags & AMX_FLAG_JITC)!=0)
          *retval=(cell)amx_opcodelist_jit;
        else
          *retval=(cell)amx_opcodelist;
      #elif defined ASM32
        *retval=(cell)amx_opcodelist;
      #else
        *retval=(cell)amx_opcodelist_jit;
      #endif
      return 0;
    } /* if */
  #endif

  if (amx->callback==NULL)
    return AMX_ERR_CALLBACK;
  if ((amx->flags & AMX_FLAG_RELOC)==0)
    return AMX_ERR_INIT;
  if ((amx->flags & AMX_FLAG_NTVREG)==0) {
    if ((i=amx_Register(amx,NULL,0))!=AMX_ERR_NONE)
      return i;
  } /* if */
  assert((amx->flags & AMX_FLAG_BROWSE)==0);

  /* set up the registers */
  hdr=(AMX_HEADER *)amx->base;
  assert(hdr->magic==AMX_MAGIC);
  codesize=(ucell)(hdr->dat-hdr->cod);
  code=amx->base+(int)hdr->cod;
  data=(amx->data!=NULL) ? amx->data : amx->base+(int)hdr->dat;
  hea=amx->hea;
  stk=amx->stk;
  reset_stk=stk;
  reset_hea=hea;
  alt=frm=pri=0;/* just to avoid compiler warnings */

  /* get the start address */
  if (index==AMX_EXEC_MAIN) {
    if (hdr->cip<0)
      return AMX_ERR_INDEX;
    cip=(cell *)(code + (int)hdr->cip);
  } else if (index==AMX_EXEC_CONT) {
    /* all registers: pri, alt, frm, cip, hea, stk, reset_stk, reset_hea */
    frm=amx->frm;
    stk=amx->stk;
    hea=amx->hea;
    pri=amx->pri;
    alt=amx->alt;
    reset_stk=amx->reset_stk;
    reset_hea=amx->reset_hea;
    cip=(cell *)(code + (int)amx->cip);
  } else if (index<0) {
    return AMX_ERR_INDEX;
  } else {
    if (index>=(cell)NUMPUBLICS(hdr))
      return AMX_ERR_INDEX;
    func=GETENTRY(hdr,publics,index);
    cip=(cell *)(code + (int)func->address);
  } /* if */
  /* check values just copied */
  CHKSTACK();
  CHKHEAP();
  assert(check_endian());

  /* sanity checks */
  assert_static(OP_PUSH_PRI==36);
  assert_static(OP_PROC==46);
  assert_static(OP_SHL==65);
  assert_static(OP_SMUL==72);
  assert_static(OP_EQ==95);
  assert_static(OP_INC_PRI==107);
  assert_static(OP_MOVS==117);
  assert_static(OP_SYMBOL==126);
  assert_static(OP_PUSH2_C==138);
  assert_static(OP_LOAD_BOTH==154);
  #if PAWN_CELL_SIZE==16
    assert_static(sizeof(cell)==2);
  #elif PAWN_CELL_SIZE==32
    assert_static(sizeof(cell)==4);
  #elif PAWN_CELL_SIZE==64
    assert_static(sizeof(cell)==8);
  #else
    #error Unsupported cell size
  #endif

  if (index!=AMX_EXEC_CONT) {
    reset_stk+=amx->paramcount*sizeof(cell);
    PUSH(amx->paramcount*sizeof(cell));
    amx->paramcount=0;          /* push the parameter count to the stack & reset */
    #if defined ASM32 || defined JIT
      PUSH(RELOC_VALUE(code,0));/* relocated zero return address */
    #else
      PUSH(0);                  /* zero return address */
    #endif
  } /* if */
  /* check stack/heap before starting to run */
  CHKMARGIN();

  /* start running */
#if defined ASM32 || defined JIT
  /* either the assembler abstract machine or the JIT; both by Marc Peter */

  parms[0] = pri;
  parms[1] = alt;
  parms[2] = (cell)cip;
  parms[3] = (cell)data;
  parms[4] = stk;
  parms[5] = frm;
  parms[6] = (cell)amx;
  parms[7] = (cell)code;
  parms[8] = (cell)codesize;

  #if defined ASM32 && defined JIT
    if ((amx->flags & AMX_FLAG_JITC)!=0)
      i = amx_exec_jit(parms,retval,amx->stp,hea);
    else
      i = amx_exec_asm(parms,retval,amx->stp,hea);
  #elif defined ASM32
    i = amx_exec_asm(parms,retval,amx->stp,hea);
  #else
    i = amx_exec_jit(parms,retval,amx->stp,hea);
  #endif
  if (i == AMX_ERR_SLEEP) {
    amx->reset_stk=reset_stk;
    amx->reset_hea=reset_hea;
  } else {
    /* remove parameters from the stack; do this the "hard" way, because
     * the assembler version has no internal knowledge of the local
     * variables, so any "clean" way would be a kludge anyway.
     */
    amx->stk=reset_stk;
    amx->hea=reset_hea;
  } /* if */
  return i;

#else

  for ( ;; ) {
    op=(OPCODE) _RCODE();
    switch (op) {
    case OP_LOAD_PRI:
      GETPARAM(offs);
      pri=_R(data,offs);
      break;
    case OP_LOAD_ALT:
      GETPARAM(offs);
      alt=_R(data,offs);
      break;
    case OP_LOAD_S_PRI:
      GETPARAM(offs);
      pri=_R(data,frm+offs);
      break;
    case OP_LOAD_S_ALT:
      GETPARAM(offs);
      alt=_R(data,frm+offs);
      break;
    case OP_LREF_PRI:
      GETPARAM(offs);
      offs=_R(data,offs);
      pri=_R(data,offs);
      break;
    case OP_LREF_ALT:
      GETPARAM(offs);
      offs=_R(data,offs);
      alt=_R(data,offs);
      break;
    case OP_LREF_S_PRI:
      GETPARAM(offs);
      offs=_R(data,frm+offs);
      pri=_R(data,offs);
      break;
    case OP_LREF_S_ALT:
      GETPARAM(offs);
      offs=_R(data,frm+offs);
      alt=_R(data,offs);
      break;
    case OP_LOAD_I:
      /* verify address */
      if (pri>=hea && pri<stk || (ucell)pri>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      pri=_R(data,pri);
      break;
    case OP_LODB_I:
      GETPARAM(offs);
      /* verify address */
      if (pri>=hea && pri<stk || (ucell)pri>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      switch ((int)offs) {
      case 1:
        pri=_R8(data,pri);
        break;
      case 2:
        pri=_R16(data,pri);
        break;
      case 4:
        pri=_R32(data,pri);
        break;
      } /* switch */
      break;
    case OP_CONST_PRI:
      GETPARAM(pri);
      break;
    case OP_CONST_ALT:
      GETPARAM(alt);
      break;
    case OP_ADDR_PRI:
      GETPARAM(pri);
      pri+=frm;
      break;
    case OP_ADDR_ALT:
      GETPARAM(alt);
      alt+=frm;
      break;
    case OP_STOR_PRI:
      GETPARAM(offs);
      _W(data,offs,pri);
      break;
    case OP_STOR_ALT:
      GETPARAM(offs);
      _W(data,offs,alt);
      break;
    case OP_STOR_S_PRI:
      GETPARAM(offs);
      _W(data,frm+offs,pri);
      break;
    case OP_STOR_S_ALT:
      GETPARAM(offs);
      _W(data,frm+offs,alt);
      break;
    case OP_SREF_PRI:
      GETPARAM(offs);
      offs=_R(data,offs);
      _W(data,offs,pri);
      break;
    case OP_SREF_ALT:
      GETPARAM(offs);
      offs=_R(data,offs);
      _W(data,offs,alt);
      break;
    case OP_SREF_S_PRI:
      GETPARAM(offs);
      offs=_R(data,frm+offs);
      _W(data,offs,pri);
      break;
    case OP_SREF_S_ALT:
      GETPARAM(offs);
      offs=_R(data,frm+offs);
      _W(data,offs,alt);
      break;
    case OP_STOR_I:
      /* verify address */
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      _W(data,alt,pri);
      break;
    case OP_STRB_I:
      GETPARAM(offs);
      /* verify address */
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      switch ((int)offs) {
      case 1:
        _W8(data,alt,pri);
        break;
      case 2:
        _W16(data,alt,pri);
        break;
      case 4:
        _W32(data,alt,pri);
        break;
      } /* switch */
      break;
    case OP_LIDX:
      offs=pri*sizeof(cell)+alt;
      /* verify address */
      if (offs>=hea && offs<stk || (ucell)offs>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      pri=_R(data,offs);
      break;
    case OP_LIDX_B:
      GETPARAM(offs);
      offs=(pri << (int)offs)+alt;
      /* verify address */
      if (offs>=hea && offs<stk || (ucell)offs>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      pri=_R(data,offs);
      break;
    case OP_IDXADDR:
      pri=pri*sizeof(cell)+alt;
      break;
    case OP_IDXADDR_B:
      GETPARAM(offs);
      pri=(pri << (int)offs)+alt;
      break;
    case OP_ALIGN_PRI:
      GETPARAM(offs);
      #if BYTE_ORDER==LITTLE_ENDIAN
        if ((size_t)offs<sizeof(cell))
          pri ^= sizeof(cell)-offs;
      #endif
      break;
    case OP_ALIGN_ALT:
      GETPARAM(offs);
      #if BYTE_ORDER==LITTLE_ENDIAN
        if ((size_t)offs<sizeof(cell))
          alt ^= sizeof(cell)-offs;
      #endif
      break;
    case OP_LCTRL:
      GETPARAM(offs);
      switch ((int)offs) {
      case 0:
        pri=hdr->cod;
        break;
      case 1:
        pri=hdr->dat;
        break;
      case 2:
        pri=hea;
        break;
      case 3:
        pri=amx->stp;
        break;
      case 4:
        pri=stk;
        break;
      case 5:
        pri=frm;
        break;
      case 6:
        pri=(cell)((unsigned char *)cip - code);
        break;
      } /* switch */
      break;
    case OP_SCTRL:
      GETPARAM(offs);
      switch ((int)offs) {
      case 0:
      case 1:
      case 3:
        /* cannot change these parameters */
        break;
      case 2:
        hea=pri;
        break;
      case 4:
        stk=pri;
        break;
      case 5:
        frm=pri;
        break;
      case 6:
        cip=(cell *)(code + (int)pri);
        break;
      } /* switch */
      break;
    case OP_MOVE_PRI:
      pri=alt;
      break;
    case OP_MOVE_ALT:
      alt=pri;
      break;
    case OP_XCHG:
      offs=pri;         /* offs is a temporary variable */
      pri=alt;
      alt=offs;
      break;
    case OP_PUSH_PRI:
      PUSH(pri);
      break;
    case OP_PUSH_ALT:
      PUSH(alt);
      break;
    case OP_PUSH_C:
      GETPARAM(offs);
      PUSH(offs);
      break;
    case OP_PUSH_R:
      GETPARAM(offs);
      while (offs--)
        PUSH(pri);
      break;
    case OP_PUSH:
      GETPARAM(offs);
      PUSH(_R(data,offs));
      break;
    case OP_PUSH_S:
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      break;
    case OP_POP_PRI:
      POP(pri);
      break;
    case OP_POP_ALT:
      POP(alt);
      break;
    case OP_STACK:
      GETPARAM(offs);
      alt=stk;
      stk+=offs;
      CHKMARGIN();
      CHKSTACK();
      break;
    case OP_HEAP:
      GETPARAM(offs);
      alt=hea;
      hea+=offs;
      CHKMARGIN();
      CHKHEAP();
      break;
    case OP_PROC:
      PUSH(frm);
      frm=stk;
      CHKMARGIN();
      break;
    case OP_RET:
      POP(frm);
      POP(offs);
      /* verify the return address */
      if ((ucell)offs>=codesize)
        ABORT(amx,AMX_ERR_MEMACCESS);
      cip=(cell *)(code+(int)offs);
      break;
    case OP_RETN:
      POP(frm);
      POP(offs);
      /* verify the return address */
      if ((ucell)offs>=codesize)
        ABORT(amx,AMX_ERR_MEMACCESS);
      cip=(cell *)(code+(int)offs);
      stk+=_R(data,stk)+sizeof(cell);   /* remove parameters from the stack */
      amx->stk=stk;
      break;
    case OP_CALL:
      PUSH(((unsigned char *)cip-code)+sizeof(cell));/* skip address */
      cip=JUMPABS(code, cip);                   /* jump to the address */
      break;
    case OP_CALL_PRI:
      PUSH((unsigned char *)cip-code);
      cip=(cell *)(code+(int)pri);
      break;
    case OP_JUMP:
      /* since the GETPARAM() macro modifies cip, you cannot
       * do GETPARAM(cip) directly */
      cip=JUMPABS(code, cip);
      break;
    case OP_JREL:
      offs=*cip;
      cip=(cell *)((unsigned char *)cip + (int)offs + sizeof(cell));
      break;
    case OP_JZER:
      if (pri==0)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JNZ:
      if (pri!=0)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JEQ:
      if (pri==alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JNEQ:
      if (pri!=alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JLESS:
      if ((ucell)pri < (ucell)alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JLEQ:
      if ((ucell)pri <= (ucell)alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JGRTR:
      if ((ucell)pri > (ucell)alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JGEQ:
      if ((ucell)pri >= (ucell)alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JSLESS:
      if (pri<alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JSLEQ:
      if (pri<=alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JSGRTR:
      if (pri>alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_JSGEQ:
      if (pri>=alt)
        cip=JUMPABS(code, cip);
      else
        cip=(cell *)((unsigned char *)cip+sizeof(cell));
      break;
    case OP_SHL:
      pri<<=alt;
      break;
    case OP_SHR:
      pri=(ucell)pri >> (int)alt;
      break;
    case OP_SSHR:
      pri>>=alt;
      break;
    case OP_SHL_C_PRI:
      GETPARAM(offs);
      pri<<=offs;
      break;
    case OP_SHL_C_ALT:
      GETPARAM(offs);
      alt<<=offs;
      break;
    case OP_SHR_C_PRI:
      GETPARAM(offs);
      pri=(ucell)pri >> (int)offs;
      break;
    case OP_SHR_C_ALT:
      GETPARAM(offs);
      alt=(ucell)alt >> (int)offs;
      break;
    case OP_SMUL:
      pri*=alt;
      break;
    case OP_SDIV:
      if (alt==0)
        ABORT(amx,AMX_ERR_DIVIDE);
      /* use floored division and matching remainder */
      offs=alt;
      #if defined TRUNC_SDIV
        pri=pri/offs;
        alt=pri%offs;
      #else
        val=pri;                /* portable routine for truncated division */
        pri=IABS(pri)/IABS(offs);
        if ((cell)(val ^ offs)<0)
          pri=-pri;
        alt=val-pri*offs;       /* calculate the matching remainder */
      #endif
      /* now "fiddle" with the values to get floored division */
      if (alt!=0 && (cell)(alt ^ offs)<0) {
        pri--;
        alt+=offs;
      } /* if */
      break;
    case OP_SDIV_ALT:
      if (pri==0)
        ABORT(amx,AMX_ERR_DIVIDE);
      /* use floored division and matching remainder */
      offs=pri;
      #if defined TRUNC_SDIV
        pri=alt/offs;
        alt=alt%offs;
      #else
        val=alt;                /* portable routine for truncated division */
        pri=IABS(alt)/IABS(offs);
        if ((cell)(val ^ offs)<0)
          pri=-pri;
        alt=val-pri*offs;       /* calculate the matching remainder */
      #endif
      /* now "fiddle" with the values to get floored division */
      if (alt!=0 && (cell)(alt ^ offs)<0) {
        pri--;
        alt+=offs;
      } /* if */
      break;
    case OP_UMUL:
      pri=(ucell)pri * (ucell)alt;
      break;
    case OP_UDIV:
      if (alt==0)
        ABORT(amx,AMX_ERR_DIVIDE);
      offs=(ucell)pri % (ucell)alt;     /* temporary storage */
      pri=(ucell)pri / (ucell)alt;
      alt=offs;
      break;
    case OP_UDIV_ALT:
      if (pri==0)
        ABORT(amx,AMX_ERR_DIVIDE);
      offs=(ucell)alt % (ucell)pri;     /* temporary storage */
      pri=(ucell)alt / (ucell)pri;
      alt=offs;
      break;
    case OP_ADD:
      pri+=alt;
      break;
    case OP_SUB:
      pri-=alt;
      break;
    case OP_SUB_ALT:
      pri=alt-pri;
      break;
    case OP_AND:
      pri&=alt;
      break;
    case OP_OR:
      pri|=alt;
      break;
    case OP_XOR:
      pri^=alt;
      break;
    case OP_NOT:
      pri=!pri;
      break;
    case OP_NEG:
      pri=-pri;
      break;
    case OP_INVERT:
      pri=~pri;
      break;
    case OP_ADD_C:
      GETPARAM(offs);
      pri+=offs;
      break;
    case OP_SMUL_C:
      GETPARAM(offs);
      pri*=offs;
      break;
    case OP_ZERO_PRI:
      pri=0;
      break;
    case OP_ZERO_ALT:
      alt=0;
      break;
    case OP_ZERO:
      GETPARAM(offs);
      _W(data,offs,0);
      break;
    case OP_ZERO_S:
      GETPARAM(offs);
      _W(data,frm+offs,0);
      break;
    case OP_SIGN_PRI:
      if ((pri & 0xff)>=0x80)
        pri|= ~ (ucell)0xff;
      break;
    case OP_SIGN_ALT:
      if ((alt & 0xff)>=0x80)
        alt|= ~ (ucell)0xff;
      break;
    case OP_EQ:
      pri= pri==alt ? 1 : 0;
      break;
    case OP_NEQ:
      pri= pri!=alt ? 1 : 0;
      break;
    case OP_LESS:
      pri= (ucell)pri < (ucell)alt ? 1 : 0;
      break;
    case OP_LEQ:
      pri= (ucell)pri <= (ucell)alt ? 1 : 0;
      break;
    case OP_GRTR:
      pri= (ucell)pri > (ucell)alt ? 1 : 0;
      break;
    case OP_GEQ:
      pri= (ucell)pri >= (ucell)alt ? 1 : 0;
      break;
    case OP_SLESS:
      pri= pri<alt ? 1 : 0;
      break;
    case OP_SLEQ:
      pri= pri<=alt ? 1 : 0;
      break;
    case OP_SGRTR:
      pri= pri>alt ? 1 : 0;
      break;
    case OP_SGEQ:
      pri= pri>=alt ? 1 : 0;
      break;
    case OP_EQ_C_PRI:
      GETPARAM(offs);
      pri= pri==offs ? 1 : 0;
      break;
    case OP_EQ_C_ALT:
      GETPARAM(offs);
      pri= alt==offs ? 1 : 0;
      break;
    case OP_INC_PRI:
      pri++;
      break;
    case OP_INC_ALT:
      alt++;
      break;
    case OP_INC:
      GETPARAM(offs);
      #if defined _R_DEFAULT
        *(cell *)(data+(int)offs) += 1;
      #else
        val=_R(data,offs);
        _W(data,offs,val+1);
      #endif
      break;
    case OP_INC_S:
      GETPARAM(offs);
      #if defined _R_DEFAULT
        *(cell *)(data+(int)(frm+offs)) += 1;
      #else
        val=_R(data,frm+offs);
        _W(data,frm+offs,val+1);
      #endif
      break;
    case OP_INC_I:
      #if defined _R_DEFAULT
        *(cell *)(data+(int)pri) += 1;
      #else
        val=_R(data,pri);
        _W(data,pri,val+1);
      #endif
      break;
    case OP_DEC_PRI:
      pri--;
      break;
    case OP_DEC_ALT:
      alt--;
      break;
    case OP_DEC:
      GETPARAM(offs);
      #if defined _R_DEFAULT
        *(cell *)(data+(int)offs) -= 1;
      #else
        val=_R(data,offs);
        _W(data,offs,val-1);
      #endif
      break;
    case OP_DEC_S:
      GETPARAM(offs);
      #if defined _R_DEFAULT
        *(cell *)(data+(int)(frm+offs)) -= 1;
      #else
        val=_R(data,frm+offs);
        _W(data,frm+offs,val-1);
      #endif
      break;
    case OP_DEC_I:
      #if defined _R_DEFAULT
        *(cell *)(data+(int)pri) -= 1;
      #else
        val=_R(data,pri);
        _W(data,pri,val-1);
      #endif
      break;
    case OP_MOVS:
      GETPARAM(offs);
      /* verify top & bottom memory addresses, for both source and destination
       * addresses
       */
      if (pri>=hea && pri<stk || (ucell)pri>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      if ((pri+offs)>hea && (pri+offs)<stk || (ucell)(pri+offs)>(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      if ((alt+offs)>hea && (alt+offs)<stk || (ucell)(alt+offs)>(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      #if defined _R_DEFAULT
        memcpy(data+(int)alt, data+(int)pri, (int)offs);
      #else
        for (i=0; i+4<offs; i+=4) {
          val=_R32(data,pri+i);
          _W32(data,alt+i,val);
        } /* for */
        for ( ; i<offs; i++) {
          val=_R8(data,pri+i);
          _W8(data,alt+i,val);
        } /* for */
      #endif
      break;
    case OP_CMPS:
      GETPARAM(offs);
      /* verify top & bottom memory addresses, for both source and destination
       * addresses
       */
      if (pri>=hea && pri<stk || (ucell)pri>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      if ((pri+offs)>hea && (pri+offs)<stk || (ucell)(pri+offs)>(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      if ((alt+offs)>hea && (alt+offs)<stk || (ucell)(alt+offs)>(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      #if defined _R_DEFAULT
        pri=memcmp(data+(int)alt, data+(int)pri, (int)offs);
      #else
        pri=0;
        for (i=0; i+4<offs && pri==0; i+=4)
          pri=_R32(data,alt+i)-_R32(data,pri+i);
        for ( ; i<offs && pri==0; i++)
          pri=_R8(data,alt+i)-_R8(data,pri+i);
      #endif
      break;
    case OP_FILL:
      GETPARAM(offs);
      /* verify top & bottom memory addresses (destination only) */
      if (alt>=hea && alt<stk || (ucell)alt>=(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      if ((alt+offs)>hea && (alt+offs)<stk || (ucell)(alt+offs)>(ucell)amx->stp)
        ABORT(amx,AMX_ERR_MEMACCESS);
      for (i=(int)alt; (size_t)offs>=sizeof(cell); i+=sizeof(cell), offs-=sizeof(cell))
        _W32(data,i,pri);
      break;
    case OP_HALT:
      GETPARAM(offs);
      if (retval!=NULL)
        *retval=pri;
      /* store complete status (stk and hea are already set in the ABORT macro) */
      amx->frm=frm;
      amx->pri=pri;
      amx->alt=alt;
      amx->cip=(cell)((unsigned char*)cip-code);
      if (offs==AMX_ERR_SLEEP) {
        amx->stk=stk;
        amx->hea=hea;
        amx->reset_stk=reset_stk;
        amx->reset_hea=reset_hea;
        return (int)offs;
      } /* if */
      ABORT(amx,(int)offs);
    case OP_BOUNDS:
      GETPARAM(offs);
      if ((ucell)pri>(ucell)offs) {
        amx->cip=(cell)((unsigned char *)cip-code);
        ABORT(amx,AMX_ERR_BOUNDS);
      } /* if */
      break;
    case OP_SYSREQ_PRI:
      /* save a few registers */
      amx->cip=(cell)((unsigned char *)cip-code);
      amx->hea=hea;
      amx->frm=frm;
      amx->stk=stk;
      num=amx->callback(amx,pri,&pri,(cell *)(data+(int)stk));
      if (num!=AMX_ERR_NONE) {
        if (num==AMX_ERR_SLEEP) {
          amx->pri=pri;
          amx->alt=alt;
          amx->reset_stk=reset_stk;
          amx->reset_hea=reset_hea;
          return num;
        } /* if */
        ABORT(amx,num);
      } /* if */
      break;
    case OP_SYSREQ_C:
      GETPARAM(offs);
      /* save a few registers */
      amx->cip=(cell)((unsigned char *)cip-code);
      amx->hea=hea;
      amx->frm=frm;
      amx->stk=stk;
      num=amx->callback(amx,offs,&pri,(cell *)(data+(int)stk));
      if (num!=AMX_ERR_NONE) {
        if (num==AMX_ERR_SLEEP) {
          amx->pri=pri;
          amx->alt=alt;
          amx->reset_stk=reset_stk;
          amx->reset_hea=reset_hea;
          return num;
        } /* if */
        ABORT(amx,num);
      } /* if */
      break;
    case OP_LINE:
      SKIPPARAM(2);
      break;
    case OP_SYMBOL:
      GETPARAM(offs);
      cip=(cell *)((unsigned char *)cip + (int)offs);
      break;
    case OP_SRANGE:
      SKIPPARAM(2);
      break;
    case OP_SYMTAG:
      SKIPPARAM(1);
      break;
    case OP_JUMP_PRI:
      cip=(cell *)(code+(int)pri);
      break;
    case OP_SWITCH: {
      cell *cptr;

      cptr=JUMPABS(code,cip)+1; /* +1, to skip the "casetbl" opcode */
      cip=JUMPABS(code,cptr+1); /* preset to "none-matched" case */
      num=(int)*cptr;           /* number of records in the case table */
      for (cptr+=2; num>0 && *cptr!=pri; num--,cptr+=2)
        /* nothing */;
      if (num>0)
        cip=JUMPABS(code,cptr+1); /* case found */
      break;
    } /* case */
    case OP_SWAP_PRI:
      offs=_R(data,stk);
      _W32(data,stk,pri);
      pri=offs;
      break;
    case OP_SWAP_ALT:
      offs=_R(data,stk);
      _W32(data,stk,alt);
      alt=offs;
      break;
    case OP_PUSH_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      break;
    case OP_NOP:
      break;
#if !defined AMX_NO_MACRO_INSTR
    case OP_SYSREQ_N:
      GETPARAM(offs);
      GETPARAM(val);
      PUSH(val);
      /* save a few registers */
      amx->cip=(cell)((unsigned char *)cip-code);
      amx->hea=hea;
      amx->frm=frm;
      amx->stk=stk;
      num=amx->callback(amx,offs,&pri,(cell *)(data+(int)stk));
      stk+=val+4;
      if (num!=AMX_ERR_NONE) {
        if (num==AMX_ERR_SLEEP) {
          amx->pri=pri;
          amx->alt=alt;
          amx->reset_stk=reset_stk;
          amx->reset_hea=reset_hea;
          return num;
        } /* if */
        ABORT(amx,num);
      } /* if */
      break;
#endif
    case OP_BREAK:
      assert((amx->flags & AMX_FLAG_BROWSE)==0);
      if (amx->debug!=NULL) {
        /* store status */
        amx->frm=frm;
        amx->stk=stk;
        amx->hea=hea;
        amx->cip=(cell)((unsigned char*)cip-code);
        num=amx->debug(amx);
        if (num!=AMX_ERR_NONE) {
          if (num==AMX_ERR_SLEEP) {
            amx->pri=pri;
            amx->alt=alt;
            amx->reset_stk=reset_stk;
            amx->reset_hea=reset_hea;
            return num;
          } /* if */
          ABORT(amx,num);
        } /* if */
      } /* if */
      break;
#if !defined AMX_NO_MACRO_INSTR
    case OP_PUSH5:
      GETPARAM(offs);
      PUSH(_R(data,offs));
      /* drop through */
    case OP_PUSH4:
      GETPARAM(offs);
      PUSH(_R(data,offs));
      /* drop through */
    case OP_PUSH3:
      GETPARAM(offs);
      PUSH(_R(data,offs));
      /* drop through */
    case OP_PUSH2:
      GETPARAM(offs);
      PUSH(_R(data,offs));
      GETPARAM(offs);
      PUSH(_R(data,offs));
      break;
    case OP_PUSH5_S:
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      /* drop through */
    case OP_PUSH4_S:
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      /* drop through */
    case OP_PUSH3_S:
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      /* drop through */
    case OP_PUSH2_S:
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      GETPARAM(offs);
      PUSH(_R(data,frm+offs));
      break;
    case OP_PUSH5_C:
      GETPARAM(offs);
      PUSH(offs);
      /* drop through */
    case OP_PUSH4_C:
      GETPARAM(offs);
      PUSH(offs);
      /* drop through */
    case OP_PUSH3_C:
      GETPARAM(offs);
      PUSH(offs);
      /* drop through */
    case OP_PUSH2_C:
      GETPARAM(offs);
      PUSH(offs);
      GETPARAM(offs);
      PUSH(offs);
      break;
    case OP_PUSH5_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      /* drop through */
    case OP_PUSH4_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      /* drop through */
    case OP_PUSH3_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      /* drop through */
    case OP_PUSH2_ADR:
      GETPARAM(offs);
      PUSH(frm+offs);
      GETPARAM(offs);
      PUSH(frm+offs);
      break;
    case OP_LOAD_BOTH:
      GETPARAM(offs);
      pri=_R(data,offs);
      GETPARAM(offs);
      alt=_R(data,offs);
      break;
    case OP_LOAD_S_BOTH:
      GETPARAM(offs);
      pri=_R(data,frm+offs);
      GETPARAM(offs);
      alt=_R(data,frm+offs);
      break;
    case OP_CONST:
      GETPARAM(offs);
      GETPARAM(val);
      _W32(data,offs,val);
      break;
    case OP_CONST_S:
      GETPARAM(offs);
      GETPARAM(val);
      _W32(data,frm+offs,val);
      break;
#endif
#if !defined AMX_DONT_RELOCATE
    case OP_SYSREQ_D: /* see OP_SYSREQ_C */
      GETPARAM(offs);
      /* save a few registers */
      amx->cip=(cell)((unsigned char *)cip-code);
      amx->hea=hea;
      amx->frm=frm;
      amx->stk=stk;
      pri=((AMX_NATIVE)offs)(amx,(cell *)(data+(int)stk));
      if (amx->error!=AMX_ERR_NONE) {
        if (amx->error==AMX_ERR_SLEEP) {
          amx->pri=pri;
          amx->alt=alt;
          amx->reset_stk=reset_stk;
          amx->reset_hea=reset_hea;
          return AMX_ERR_SLEEP;
        } /* if */
        ABORT(amx,amx->error);
      } /* if */
      break;
#endif
#if !defined AMX_NO_MACRO_INSTR && !defined AMX_DONT_RELOCATE
    case OP_SYSREQ_ND:    /* see SYSREQ_N */
      GETPARAM(offs);
      GETPARAM(val);
      PUSH(val);
      /* save a few registers */
      amx->cip=(cell)((unsigned char *)cip-code);
      amx->hea=hea;
      amx->frm=frm;
      amx->stk=stk;
      pri=((AMX_NATIVE)offs)(amx,(cell *)(data+(int)stk));
      stk+=val+4;
      if (amx->error!=AMX_ERR_NONE) {
        if (amx->error==AMX_ERR_SLEEP) {
          amx->pri=pri;
          amx->alt=alt;
          amx->reset_stk=reset_stk;
          amx->reset_hea=reset_hea;
          return AMX_ERR_SLEEP;
        } /* if */
        ABORT(amx,amx->error);
      } /* if */
      break;
#endif
    default:
      /* case OP_FILE:          should not occur during execution
       * case OP_CASETBL:       should not occur during execution
       */
      assert(0);
      ABORT(amx,AMX_ERR_INVINSTR);
    } /* switch */
  } /* for */
#endif
}

#endif  /* __GNUC__ || __ICC */

#endif /* AMX_EXEC || AMX_INIT */

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
