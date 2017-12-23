/*  AMX bytecode (P-Code) verifier.
 *
 *  Copyright (c) Stanislav Gromov, 2016-2017
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
 */

#include "amx.h"
#include "amx_internal.h"


#ifdef AMX_INIT

#ifdef __cplusplus
  extern "C" {
#endif /* __cplusplus */
int VerifyRelocateBytecode(AMX *amx);
#ifdef __cplusplus
  }
#endif /* __cplusplus */

#define CELLADDR(base,offs)             PTR_TO_CELLPTR((size_t)(base)+(size_t)(offs))
#define PARAMADDR(ip,n)                 PTR_TO_CELLPTR((size_t)(ip)+(size_t)(n)*sizeof(cell))
#define GETOFFS(base,ptr)               (PTR_TO_MEMSIZE(ptr)-PTR_TO_MEMSIZE(base))

#if !defined AMX_DONT_RELOCATE
  #define RELOC_CODE_OFFS(code,argaddr) ((*(ucell *)argaddr)+=(ucell)PTR_TO_MEMSIZE(code))
#else
  #define RELOC_CODE_OFFS(code,argaddr) ((void)(code),(void)(arg_addr))
#endif
#if !defined AMX_DONT_RELOCATE && !defined _R && defined AMX_USE_NEW_AMXEXEC
  #define RELOC_DATA_OFFS(data,argaddr) ((*(ucell *)argaddr)+=(ucell)PTR_TO_MEMSIZE(data))
#else
  #define RELOC_DATA_OFFS(data,argaddr) ((void)(data),(void)(argaddr))
#endif


#if defined _MSC_VER
  #define VHANDLER_CALL __fastcall
#elif defined __GNUC__
  #if defined __clang__
    #define VHANDLER_CALL __attribute__((fastcall))
  #elif (defined __i386__ || defined __x86_64__ || defined __amd64__)
    #if !defined __x86_64__ && !defined __amd64__ && (__GNUC__>=4 || __GNUC__==3 && __GNUC_MINOR__>=4)
      #define VHANDLER_CALL __attribute__((fastcall))
    #else
      #define VHANDLER_CALL __attribute__((regparm(3)))
    #endif
  #else
    #define VHANDLER_CALL
  #endif
#else
  #define VHANDLER_CALL
#endif

typedef struct tagVERIFICATION_DATA {
  cell *cip;
  unsigned char *code;
  unsigned char *data;
  ucell num_natives;
  ucell codesize, datasize, stacksize;
  int sysreq_flag;
  int err;
} VERIFICATION_DATA;
typedef int (VHANDLER_CALL *VHANDLER)(VERIFICATION_DATA *vdata);

static int VHANDLER_CALL v_parm0(VERIFICATION_DATA *vdata)
{
  return 0;
}

static int VHANDLER_CALL v_parm1_number(VERIFICATION_DATA *vdata)
{
  return 1;
}

static int VHANDLER_CALL v_parm1_codeoffs(VERIFICATION_DATA *vdata)
{
  cell *arg_addr;

  arg_addr=PARAMADDR(vdata->cip, 1);
  if (IS_INVALID_CODE_OFFS_NORELOC(*arg_addr, vdata->codesize)) {
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }

  RELOC_CODE_OFFS(vdata->code, arg_addr);
  return 1;
}

static int VHANDLER_CALL v_parm1_dataoffs(VERIFICATION_DATA *vdata)
{
  cell *arg_addr;

  arg_addr=PARAMADDR(vdata->cip, 1);
  if (IS_INVALID_DATA_OFFS(*arg_addr, vdata->datasize)) {
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }

  RELOC_DATA_OFFS(vdata->data, arg_addr);
  return 1;
}

static int VHANDLER_CALL v_parm2_number(VERIFICATION_DATA *vdata)
{
  return 2;
}

static int VHANDLER_CALL v_parm2_dataoffs(VERIFICATION_DATA *vdata)
{
  cell const *cip=vdata->cip;
  unsigned char const *data=vdata->data;
  const ucell datasize=vdata->datasize;
  cell *arg_addr;

  arg_addr=PARAMADDR(cip, 1);
  if (IS_INVALID_DATA_OFFS(*arg_addr, datasize)) {
err:
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }
  RELOC_DATA_OFFS(data, arg_addr);

  arg_addr=PARAMADDR(cip, 2);
  if (IS_INVALID_DATA_OFFS(*arg_addr, datasize))
    goto err;
  RELOC_DATA_OFFS(data, arg_addr);

  return 2;
}

static int VHANDLER_CALL v_parm2_dataoffs_number(VERIFICATION_DATA *vdata)
{
  /* the 2'nd argument is a generic number value, we don't need to check it;
   * just check the 1'st argument (data offset)
   */
  v_parm1_dataoffs(vdata);
  return 2;
}

static int VHANDLER_CALL v_parm3_number(VERIFICATION_DATA *vdata)
{
  return 3;
}

static int VHANDLER_CALL v_parm3_dataoffs(VERIFICATION_DATA *vdata)
{
  cell const *cip=vdata->cip;
  unsigned char const *data=vdata->data;
  const ucell datasize=vdata->datasize;
  cell *arg_addr;
  cell arg;

  arg_addr=PARAMADDR(cip, 1);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize)) {
err:
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }
  RELOC_DATA_OFFS(data, arg_addr);

  arg_addr=PARAMADDR(cip, 2);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize))
    goto err;
  RELOC_DATA_OFFS(data, arg_addr);

  arg_addr=PARAMADDR(cip, 3);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize))
    goto err;
  RELOC_DATA_OFFS(data, arg_addr);

  return 3;
}

static int VHANDLER_CALL v_parm4_number(VERIFICATION_DATA *vdata)
{
  return 4;
}

static int VHANDLER_CALL v_parm4_dataoffs(VERIFICATION_DATA *vdata)
{
  cell const *cip=vdata->cip;
  unsigned char const *data=vdata->data;
  const ucell datasize=vdata->datasize;
  cell *arg_addr;
  cell arg;

  arg_addr=PARAMADDR(cip, 1);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize)) {
err:
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }
  RELOC_DATA_OFFS(data, arg_addr);

  arg_addr=PARAMADDR(cip, 2);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize))
    goto err;
  RELOC_DATA_OFFS(data, arg_addr);

  arg_addr=PARAMADDR(cip, 3);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize))
    goto err;
  RELOC_DATA_OFFS(data, arg_addr);

  arg_addr=PARAMADDR(cip, 4);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize))
    goto err;
  RELOC_DATA_OFFS(data, arg_addr);

  return 4;
}

static int VHANDLER_CALL v_parm5_number(VERIFICATION_DATA *vdata)
{
  return 5;
}

static int VHANDLER_CALL v_parm5_dataoffs(VERIFICATION_DATA *vdata)
{
  cell const *cip=vdata->cip;
  unsigned char const *data=vdata->data;
  const ucell datasize=vdata->datasize;
  cell *arg_addr;
  cell arg;

  arg_addr=PARAMADDR(cip, 1);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize)) {
err:
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }
  RELOC_DATA_OFFS(data, arg_addr);

  arg_addr=PARAMADDR(cip, 2);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize))
    goto err;
  RELOC_DATA_OFFS(data, arg_addr);

  arg_addr=PARAMADDR(cip, 3);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize))
    goto err;
  RELOC_DATA_OFFS(data, arg_addr);

  arg_addr=PARAMADDR(cip, 4);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize))
    goto err;
  RELOC_DATA_OFFS(data, arg_addr);

  arg_addr=PARAMADDR(cip, 5);
  arg=*arg_addr;
  if (IS_INVALID_DATA_OFFS(arg, datasize))
    goto err;
  RELOC_DATA_OFFS(data, arg_addr);

  return 5;
}

static int VHANDLER_CALL v_invalid(VERIFICATION_DATA *vdata)
{
  vdata->err=AMX_ERR_INVINSTR;
  return -1;
}

static int VHANDLER_CALL v_lodb_i_strb_i(VERIFICATION_DATA *vdata)
{
  const cell arg=*PARAMADDR(vdata->cip, 1);
  if (AMX_UNLIKELY(arg!=1 && arg!=2 && arg!=4)) {
    vdata->err=AMX_ERR_PARAMS;
    return -1;
  }
  return 1;
}

static int VHANDLER_CALL v_lctrl(VERIFICATION_DATA *vdata)
{
  cell *arg_addr=PARAMADDR(vdata->cip, 1);
  const cell arg=*arg_addr;
  if (AMX_UNLIKELY(arg<0)) {
    vdata->err=AMX_ERR_PARAMS;
    return -1;
  } /* if */
  if (AMX_UNLIKELY(arg>8))
    *arg_addr=-1; /* replace unknown ID by -1 */
  return 1;
}

static int VHANDLER_CALL v_sctrl(VERIFICATION_DATA *vdata)
{
  cell *arg_addr=PARAMADDR(vdata->cip, 1);
  const cell arg=*arg_addr;
  switch (arg) {
  case 0:
  case 1:
  case 3:
  case 7:
  replace_id:
    *arg_addr = (cell)-1; /* replace unknown/read-only ID by -1 */
    /* drop through */
  case 2:
  case 4:
  case 5:
  case 6:
  case 8:
    break;
  default:
    if (arg>8)
      goto replace_id;
    vdata->err=AMX_ERR_PARAMS;
    return -1;
  } /* switch */
  return 1;
}

static int VHANDLER_CALL v_stack_heap(VERIFICATION_DATA *vdata)
{
  cell arg=*PARAMADDR(vdata->cip, 1);
  if (arg<0)
    arg=-arg;
  if ((ucell)arg>=vdata->stacksize) {
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }
  return 1;
}

static int VHANDLER_CALL v_jrel(VERIFICATION_DATA *vdata)
{
  const cell arg=(cell)((size_t)(vdata->cip)-(size_t)(vdata->code))+*PARAMADDR(vdata->cip, 1);
  if (IS_INVALID_CODE_OFFS_NORELOC(arg, vdata->codesize)) {
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }
  return 1;
}

static int VHANDLER_CALL v_sysreq(VERIFICATION_DATA *vdata)
{
  /* verify the native function ID and replace it by another one
   * from the global native table if relocation is possible
   */
  cell const *cip=vdata->cip;
  cell arg;

  arg=*PARAMADDR(cip, 1);
#if defined AMX_NATIVETABLE
  if (arg>(*(cell *)&vdata->num_natives)) {
#else
  if ((*(ucell *)&arg)>vdata->num_natives) {
#endif
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }

  if (*cip==(cell)OP_SYSREQ_C) {
    if (AMX_UNLIKELY(vdata->sysreq_flag&0x02)) {
      vdata->err=AMX_ERR_ASSERT;
      return -1;
    }
    vdata->sysreq_flag|=0x01;
    return 1;
  }

  if (AMX_UNLIKELY(vdata->sysreq_flag&0x01)) {
    vdata->err=AMX_ERR_ASSERT;
    return -1;
  }
  arg=*PARAMADDR(cip, 2);
  if (arg<0)
    arg=-arg;
  if (*(ucell *)&arg>=vdata->stacksize) {
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }
  vdata->sysreq_flag|=0x02;

  return 2;
}

static int VHANDLER_CALL v_switch(VERIFICATION_DATA *vdata)
{
  unsigned char const *code=vdata->code;
  const cell *cip=vdata->cip;
  const cell *arg_addr=PARAMADDR(cip, 1);
  const cell arg=*arg_addr;

  if (IS_INVALID_CODE_OFFS_NORELOC(arg, vdata->codesize)) {
    vdata->err=AMX_ERR_BOUNDS;
    return -1;
  }
  if (AMX_UNLIKELY(*CELLADDR(code, arg) != (cell)OP_CASETBL)) {
    vdata->err=AMX_ERR_PARAMS;
    return -1;
  }

  RELOC_CODE_OFFS(code, arg_addr);

  return 1;
}

static int VHANDLER_CALL v_casetbl(VERIFICATION_DATA *vdata)
{
  AMX_REGISTER_VAR ucell i, num_args;
  cell const *cip=vdata->cip;
  const ucell codesize=vdata->codesize;
  unsigned char const *code=vdata->code;
  cell *arg_addr;
  cell arg;

  /* get the total number of opcode parameters, then loop
   * through all of the case table entries and verify jump addresses
   * (also check the "default" branch jump address in the 2'nd argument)
   */
  num_args=2+((int)*PARAMADDR(cip, 1))*2;
  for (i=2; i<=num_args; i+=2) {
    arg_addr=PARAMADDR(cip, i);
    arg=*arg_addr;
    if (IS_INVALID_CODE_OFFS_NORELOC(arg, codesize)) {
      vdata->err=AMX_ERR_BOUNDS;
      return -1;
    }
    RELOC_CODE_OFFS(code, arg_addr);
  }

  return num_args;
}

static const VHANDLER handlers[256] = {
/* ---------------------------------- */
/* OP_NONE */        v_invalid,
/* OP_LOAD_PRI */    v_parm1_dataoffs,
/* OP_LOAD_ALT */    v_parm1_dataoffs,
/* OP_LOAD_S_PRI */  v_parm1_number,
/* OP_LOAD_S_ALT */  v_parm1_number,
/* OP_LREF_PRI */    v_parm1_dataoffs,
/* OP_LREF_ALT */    v_parm1_dataoffs,
/* OP_LREF_S_PRI */  v_parm1_number,
/* OP_LREF_S_ALT */  v_parm1_number,
/* OP_LOAD_I */      v_parm0,
/* OP_LODB_I */      v_lodb_i_strb_i,
/* OP_CONST_PRI */   v_parm1_number,
/* OP_CONST_ALT */   v_parm1_number,
/* OP_ADDR_PRI */    v_parm1_number,
/* OP_ADDR_ALT */    v_parm1_number,
/* OP_STOR_PRI */    v_parm1_dataoffs,
/* OP_STOR_ALT */    v_parm1_dataoffs,
/* OP_STOR_S_PRI */  v_parm1_number,
/* OP_STOR_S_ALT */  v_parm1_number,
/* OP_SREF_PRI */    v_parm1_dataoffs,
/* OP_SREF_ALT */    v_parm1_dataoffs,
/* OP_SREF_S_PRI */  v_parm1_number,
/* OP_SREF_S_ALT */  v_parm1_number,
/* OP_STOR_I */      v_parm0,
/* OP_STRB_I */      v_lodb_i_strb_i,
/* OP_LIDX */        v_parm0,
/* OP_LIDX_B */      v_parm1_number,
/* OP_IDXADDR */     v_parm0,
/* OP_IDXADDR_B */   v_parm1_number,
/* OP_ALIGN_PRI */   v_parm1_number,
/* OP_ALIGN_ALT */   v_parm1_number,
/* OP_LCTRL */       v_lctrl,
/* OP_SCTRL */       v_sctrl,
/* OP_MOVE_PRI */    v_parm0,
/* OP_MOVE_ALT */    v_parm0,
/* OP_XCHG */        v_parm0,
/* OP_PUSH_PRI */    v_parm0,
/* OP_PUSH_ALT */    v_parm0,
/* OP_PUSH_R */      v_parm1_number,
/* OP_PUSH_C */      v_parm1_number,
/* OP_PUSH */        v_parm1_dataoffs,
/* OP_PUSH_S */      v_parm1_number,
/* OP_POP_PRI */     v_parm0,
/* OP_POP_ALT */     v_parm0,
/* OP_STACK */       v_stack_heap,
/* OP_HEAP */        v_stack_heap,
/* OP_PROC */        v_parm0,
/* OP_RET */         v_parm0,
/* OP_RETN */        v_parm0,
/* OP_CALL */        v_parm1_codeoffs,
/* OP_CALL_PRI */    v_parm0,
/* OP_JUMP */        v_parm1_codeoffs,
/* OP_JREL */        v_jrel,
/* OP_JZER */        v_parm1_codeoffs,
/* OP_JNZ */         v_parm1_codeoffs,
/* OP_JEQ */         v_parm1_codeoffs,
/* OP_JNEQ */        v_parm1_codeoffs,
/* OP_JLESS */       v_parm1_codeoffs,
/* OP_JLEQ */        v_parm1_codeoffs,
/* OP_JGRTR */       v_parm1_codeoffs,
/* OP_JGEQ */        v_parm1_codeoffs,
/* OP_JSLESS */      v_parm1_codeoffs,
/* OP_JSLEQ */       v_parm1_codeoffs,
/* OP_JSGRTR */      v_parm1_codeoffs,
/* OP_JSGEQ */       v_parm1_codeoffs,
/* OP_SHL */         v_parm0,
/* OP_SHR */         v_parm0,
/* OP_SSHR */        v_parm0,
/* OP_SHL_C_PRI */   v_parm1_number,
/* OP_SHL_C_ALT */   v_parm1_number,
/* OP_SHR_C_PRI */   v_parm1_number,
/* OP_SHR_C_ALT */   v_parm1_number,
/* OP_SMUL */        v_parm0,
/* OP_SDIV */        v_parm0,
/* OP_SDIV_ALT */    v_parm0,
/* OP_UMUL */        v_parm0,
/* OP_UDIV */        v_parm0,
/* OP_UDIV_ALT */    v_parm0,
/* OP_ADD */         v_parm0,
/* OP_SUB */         v_parm0,
/* OP_SUB_ALT */     v_parm0,
/* OP_AND */         v_parm0,
/* OP_OR */          v_parm0,
/* OP_XOR */         v_parm0,
/* OP_NOT */         v_parm0,
/* OP_NEG */         v_parm0,
/* OP_INVERT */      v_parm0,
/* OP_ADD_C */       v_parm1_number,
/* OP_SMUL_C */      v_parm1_number,
/* OP_ZERO_PRI */    v_parm0,
/* OP_ZERO_ALT */    v_parm0,
/* OP_ZERO */        v_parm1_dataoffs,
/* OP_ZERO_S */      v_parm1_number,
/* OP_SIGN_PRI */    v_parm0,
/* OP_SIGN_ALT */    v_parm0,
/* OP_EQ */          v_parm0,
/* OP_NEQ */         v_parm0,
/* OP_LESS */        v_parm0,
/* OP_LEQ */         v_parm0,
/* OP_GRTR */        v_parm0,
/* OP_GEQ */         v_parm0,
/* OP_SLESS */       v_parm0,
/* OP_SLEQ */        v_parm0,
/* OP_SGRTR */       v_parm0,
/* OP_SGEQ */        v_parm0,
/* OP_EQ_C_PRI */    v_parm1_number,
/* OP_EQ_C_ALT */    v_parm1_number,
/* OP_INC_PRI */     v_parm0,
/* OP_INC_ALT */     v_parm0,
/* OP_INC */         v_parm1_dataoffs,
/* OP_INC_S */       v_parm1_number,
/* OP_INC_I */       v_parm0,
/* OP_DEC_PRI */     v_parm0,
/* OP_DEC_ALT */     v_parm0,
/* OP_DEC */         v_parm1_dataoffs,
/* OP_DEC_S */       v_parm1_number,
/* OP_DEC_I */       v_parm0,
/* OP_MOVS */        v_parm1_number,
/* OP_CMPS */        v_parm1_number,
/* OP_FILL */        v_parm1_number,
/* OP_HALT */        v_parm1_number,
/* OP_BOUNDS */      v_parm1_number,
/* OP_SYSREQ_PRI */  v_parm0,
/* OP_SYSREQ_C */    v_sysreq,
/* OP_FILE */        v_invalid,
/* OP_LINE */        v_invalid,
/* OP_SYMBOL */      v_invalid,
/* OP_SRANGE */      v_invalid,
/* OP_JUMP_PRI */    v_parm0,
/* OP_SWITCH */      v_switch,
/* OP_CASETBL */     v_casetbl,
/* OP_SWAP_PRI */    v_parm0,
/* OP_SWAP_ALT */    v_parm0,
/* OP_PUSH_ADR */    v_parm1_number,
/* OP_NOP */         v_parm0,
/* OP_SYSREQ_N */    v_sysreq,
/* OP_SYMTAG */      v_invalid,
/* OP_BREAK */       v_parm0,
/* ---------------------------------- */
/* macro instructions */
/* OP_PUSH2_C */     v_parm2_number,
/* OP_PUSH2 */       v_parm2_dataoffs,
/* OP_PUSH2_S */     v_parm2_number,
/* OP_PUSH2_ADR */   v_parm2_number,
/* OP_PUSH3_C */     v_parm3_number,
/* OP_PUSH3 */       v_parm3_dataoffs,
/* OP_PUSH3_S */     v_parm3_number,
/* OP_PUSH3_ADR */   v_parm3_number,
/* OP_PUSH4_C */     v_parm4_number,
/* OP_PUSH4 */       v_parm4_dataoffs,
/* OP_PUSH4_S */     v_parm4_number,
/* OP_PUSH4_ADR */   v_parm4_number,
/* OP_PUSH5_C */     v_parm5_number,
/* OP_PUSH5 */       v_parm5_dataoffs,
/* OP_PUSH5_S */     v_parm5_number,
/* OP_PUSH5_ADR */   v_parm5_number,
/* OP_LOAD_BOTH */   v_parm2_dataoffs,
/* OP_LOAD_S_BOTH */ v_parm2_number,
/* OP_CONST */       v_parm2_dataoffs_number,
/* OP_CONST_S */     v_parm2_number,
/* ---------------------------------- */
/* patched at runtime (not generated by the compiler) */
/* OP_SYSREQ_D */    v_invalid,
/* OP_SYSREQ_ND */   v_invalid,
/* ---------------------------------- */
/* invalid x96 */
#define vhnd_dup10(x) x, x, x, x, x, x, x, x, x, x
/* 30 */ vhnd_dup10(v_invalid), vhnd_dup10(v_invalid), vhnd_dup10(v_invalid),
/* 60 */ vhnd_dup10(v_invalid), vhnd_dup10(v_invalid), vhnd_dup10(v_invalid),
/* 90 */ vhnd_dup10(v_invalid), vhnd_dup10(v_invalid), vhnd_dup10(v_invalid),
/* 96 */ v_invalid, v_invalid, v_invalid, v_invalid, v_invalid, v_invalid
#undef vhnd_dup10
};

#if defined AMX_EXEC_USE_JUMP_TABLE && !defined AMX_DONT_RELOCATE
static size_t *amx_exec_jump_table = NULL;
#endif

#ifdef __cplusplus
  extern "C"
#endif /* __cplusplus */
int VerifyRelocateBytecode(AMX *amx)
{
  VERIFICATION_DATA vdata;
  AMX_HEADER const *hdr=(AMX_HEADER *)amx->base;
  cell *code_end;
  AMX_REGISTER_VAR size_t op;
  AMX_REGISTER_VAR int num_args;

  amx->flags |= AMX_FLAG_BROWSE;
#if defined AMX_EXEC_USE_JUMP_TABLE && !defined AMX_DONT_RELOCATE
  if (NULL == amx_exec_jump_table) {
    amx_Exec(amx, (cell*)(void*)&amx_exec_jump_table, 0);
  }
#endif

  // Make sure there's 256 handlers total.
  assert_static(1<<(sizeof(char)*8)==sizeof(handlers)/sizeof(handlers[0]));

  amx->cip=0;
  vdata.codesize=(ucell)(hdr->dat-hdr->cod);
  vdata.datasize=(ucell)(hdr->hea-hdr->dat);
  vdata.stacksize=(ucell)(hdr->stp-hdr->hea);
  vdata.code=amx->base+(size_t)hdr->cod;
  vdata.data=amx->base+(size_t)hdr->dat;
  vdata.cip=PTR_TO_CELLPTR(vdata.code);
  vdata.num_natives=(ucell)NUMNATIVES(hdr);
  vdata.err=AMX_ERR_NONE;
  vdata.sysreq_flag=0;

  /* Make sure the code section starts with OP_HALT instruction. */
  if (*(vdata.cip)!=OP_HALT) {
    amx->flags &= ~AMX_FLAG_BROWSE;
    return (amx->error=AMX_ERR_INVINSTR);
  }

  /* Verify all instructions in the script. */
  code_end=PTR_TO_CELLPTR(vdata.code+(size_t)vdata.codesize);
  while (vdata.cip<code_end) {
    op=(size_t)(unsigned char)*vdata.cip;
    num_args=handlers[op](&vdata);
    if (num_args==-1) {
      amx->cip=(cell)((size_t)vdata.cip-(size_t)vdata.code);
      amx->flags &= ~AMX_FLAG_BROWSE;
      return (amx->error=AMX_ERR_INVINSTR);
    }
#if defined AMX_EXEC_USE_JUMP_TABLE && !defined AMX_DONT_RELOCATE
    /* Replace the operation code by a jump address
     * to the instruction handling code in amx_Exec.
     */
    *(vdata.cip)=*PTR_TO_CELLPTR(&amx_exec_jump_table[op]);
#endif
    vdata.cip+=(size_t)(1+num_args);
  }
#ifndef AMX_DONT_RELOCATE
  switch (vdata.sysreq_flag) {
  case 0x0:
    break;
  case 0x1:
  #if defined AMX_EXEC_USE_JUMP_TABLE && !defined AMX_DONT_RELOCATE
    amx->sysreq_d=*PTR_TO_CELLPTR(&amx_exec_jump_table[(size_t)OP_SYSREQ_D]);
  #else
    amx->sysreq_d=OP_SYSREQ_D;
  #endif
    break;
  case 0x2:
  #if defined AMX_EXEC_USE_JUMP_TABLE && !defined AMX_DONT_RELOCATE
    amx->sysreq_d=*PTR_TO_CELLPTR(&amx_exec_jump_table[(size_t)OP_SYSREQ_ND]);
  #else
    amx->sysreq_d=OP_SYSREQ_ND;
  #endif
    amx->flags |= AMX_FLAG_SYSREQN;
    break;
  default:
    return AMX_ERR_ASSERT;
  }
#endif
  amx->flags |= AMX_FLAG_RELOC;
  amx->flags &= ~AMX_FLAG_BROWSE;
  return AMX_ERR_NONE;
}

#endif /* AMX_INIT */
