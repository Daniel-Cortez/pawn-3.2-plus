/*  A simpler runner based on source/amx/pawnrun/prun1.c.
 *
 *  Copyright (c) ITB CompuPhase, 2001-2005
 *
 *  This file may be freely used. No warranties of any kind.
 */

#include <stdio.h>
#include <stdlib.h>             /* for exit() */
#include <signal.h>
#include <string.h>             /* for memset() (on some compilers) */
#include "../amx/amx.h"
#include "../amx/amxaux.h"

extern int AMXEXPORT AMXAPI amx_CoreInit(AMX *amx);
extern int AMXEXPORT AMXAPI amx_CoreCleanup(AMX *amx);
extern int AMXEXPORT AMXAPI amx_ConsoleInit(AMX *amx);
extern int AMXEXPORT AMXAPI amx_ConsoleCleanup(AMX *amx);

static void ErrorExit(AMX *amx, int errorcode)
{
  printf("Run time error %d: \"%s\"\n", errorcode, aux_StrError(errorcode));
  exit(1);
}

static void PrintUsage(char *program)
{
  printf("Usage: %s <filename>\n<filename> is a compiled script.\n", program);
  exit(1);
}

int main(int argc,char *argv[])
{
  extern AMX_NATIVE_INFO console_Natives[];
  extern AMX_NATIVE_INFO core_Natives[];

  AMX amx;
  cell ret = 0;
  int err;

  if (argc != 2)
    PrintUsage(argv[0]);

  err = aux_LoadProgram(&amx, argv[1], NULL);
  if (err != AMX_ERR_NONE)
    ErrorExit(&amx, err);

  amx_CoreInit(&amx);
  err = amx_ConsoleInit(&amx);
  if (err != AMX_ERR_NONE)
    ErrorExit(&amx, err);

  err = amx_Exec(&amx, &ret, AMX_EXEC_MAIN);
  if (err != AMX_ERR_NONE)
    ErrorExit(&amx, err);
  printf("%s returns %ld\n", argv[1], (long)ret);

  amx_ConsoleCleanup(&amx);
  amx_CoreCleanup(&amx);
  aux_FreeProgram(&amx);
  return 0;
}
