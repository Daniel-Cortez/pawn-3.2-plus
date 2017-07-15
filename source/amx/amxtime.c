/*  Date/time module for the Pawn Abstract Machine
 *
 *  Copyright (c) ITB CompuPhase, 2001-2008
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
 *  Version: $Id: amxtime.c 3856 2007-11-27 13:55:27Z thiadmer $
 */

#include <time.h>
#include <assert.h>
#include "amx.h"
#if defined __WIN32__ || defined _WIN32 || defined _Windows
  #include <windows.h>
  #include <mmsystem.h>
#endif

#define CELLMIN   (-1 << (8*sizeof(cell) - 1))

#define SECONDS_PER_MINUTE	60
#define SECONDS_PER_HOUR	3600
#define SECONDS_PER_DAY		86400
#define SECONDS_PER_YEAR	31556952	/* based on 365.2425 days per year */

#if !defined CLOCKS_PER_SEC
  #define CLOCKS_PER_SEC CLK_TCK
#endif
#if defined __WIN32__ || defined _WIN32 || defined WIN32
  static int timerset = 0;
  /* timeGetTime() is more accurate on WindowsNT if timeBeginPeriod(1) is set */
  #define INIT_TIMER()    \
    if (!timerset) {      \
      timeBeginPeriod(1); \
      timerset=1;         \
    }
#else
  #define INIT_TIMER()
#endif
static unsigned long timestamp;
static unsigned long timelimit;
static int timerepeat;

static const unsigned char monthdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static int wrap(int value, int min, int max)
{
  if (value<min)
    value=max;
  else if (value>max)
    value=min;
  return value;
}

static unsigned long gettimestamp(void)
{
  unsigned long value;

  #if defined __WIN32__ || defined _WIN32 || defined WIN32
    value = (unsigned long)timeGetTime(); /* this value is already in milliseconds */
  #else
    value = (unsigned long)clock();
    /* convert to milliseconds */
    if ((unsigned long)CLOCKS_PER_SEC<1000UL)
      value = (1000UL*value) / (unsigned long)CLOCKS_PER_SEC;
    else if ((unsigned long)CLOCKS_PER_SEC>1000UL)
      value = value / ((unsigned long)CLOCKS_PER_SEC / 1000UL);
  #endif
  return (unsigned long)(cell)value;
}

void stamp2datetime(unsigned long sec1970,
                    int *year, int *month, int *day,
                    int *hour, int *minute, int *second)
{
  int days, seconds;

  /* find the year */
  assert(year!=NULL);
  for (*year = 1970; ; *year += 1) {
    days = 365 + ((*year & 0x03) == 0); /* clumsy "leap-year" routine, fails for 2100 */
    seconds = days * SECONDS_PER_DAY;
    if ((unsigned long)seconds > sec1970)
      break;
    sec1970 -= seconds;
  } /* if */

  /* find the month */
  assert(month!=NULL);
  for (*month = 1; ; *month += 1) {
    days = monthdays[*month - 1];
    seconds = days * SECONDS_PER_DAY;
    if ((unsigned long)seconds > sec1970)
      break;
    sec1970 -= seconds;
  } /* if */

  /* find the day */
  assert(day!=NULL);
  for (*day = 1; sec1970 >= SECONDS_PER_DAY; *day += 1)
    sec1970 -= SECONDS_PER_DAY;

  /* find the hour */
  assert(hour!=NULL);
  for (*hour = 0; sec1970 >= SECONDS_PER_HOUR; *hour += 1)
    sec1970 -= SECONDS_PER_HOUR;

  /* find the minute */
  assert(minute!=NULL);
  for (*minute = 0; sec1970 >= SECONDS_PER_MINUTE; *minute += 1)
    sec1970 -= SECONDS_PER_MINUTE;

  /* remainder is the number of seconds */
  assert(second!=NULL);
  *second = (int)sec1970;
}

static void settime(cell hour,cell minute,cell second)
{
  #if defined __WIN32__ || defined _WIN32 || defined WIN32
    SYSTEMTIME systim;

    GetLocalTime(&systim);
    if (hour!=CELLMIN)
      systim.wHour=(WORD)wrap((int)hour,0,23);
    if (minute!=CELLMIN)
      systim.wMinute=(WORD)wrap((int)minute,0,59);
    if (second!=CELLMIN)
      systim.wSecond=(WORD)wrap((int)second,0,59);
    SetLocalTime(&systim);
  #else
    /* Linux/Unix (and some DOS compilers) have stime(); on Linux/Unix, you
     * must have "root" permission to call stime()
     */
    time_t sec1970;
    struct tm gtm;

    time(&sec1970);
    gtm=*localtime(&sec1970);
    if (hour!=CELLMIN)
      gtm.tm_hour=wrap((int)hour,0,23);
    if (minute!=CELLMIN)
      gtm.tm_min=wrap((int)minute,0,59);
    if (second!=CELLMIN)
      gtm.tm_sec=wrap((int)second,0,59);
    sec1970=mktime(&gtm);
    stime(&sec1970);
  #endif
}

static void setdate(cell year,cell month,cell day)
{
  int maxday;

  #if defined __WIN32__ || defined _WIN32 || defined WIN32
    SYSTEMTIME systim;

    GetLocalTime(&systim);
    if (year!=CELLMIN)
      systim.wYear=(WORD)wrap((int)year,1970,2099);
    if (month!=CELLMIN)
      systim.wMonth=(WORD)wrap((int)month,1,12);
    maxday=monthdays[systim.wMonth - 1];
    if (systim.wMonth==2 && ((systim.wYear % 4)==0 && ((systim.wYear % 100)!=0 || (systim.wYear % 400)==0)))
      maxday++;
    if (day!=CELLMIN)
      systim.wDay=(WORD)wrap((int)day,1,maxday);
    SetLocalTime(&systim);
  #else
    /* Linux/Unix (and some DOS compilers) have stime(); on Linux/Unix, you
     * must have "root" permission to call stime()
     */
    time_t sec1970;
    struct tm gtm;

    time(&sec1970);
    gtm=*localtime(&sec1970);
    if (year!=CELLMIN)
      gtm.tm_year=year-1900;
    if (month!=CELLMIN)
      gtm.tm_mon=month-1;
    if (day!=CELLMIN)
      gtm.tm_mday=day;
    sec1970=mktime(&gtm);
    stime(&sec1970);
  #endif
}


/* settime(hour, minute, second)
 * Always returns 0
 */
static cell AMX_NATIVE_CALL n_settime(AMX *amx, const cell *params)
{
  (void)amx;
  settime(params[1],params[2],params[3]);
  return 0;
}

/* gettime(&hour, &minute, &second)
 * The return value is the number of seconds since 1 January 1970 (Unix system
 * time).
 */
static cell AMX_NATIVE_CALL n_gettime(AMX *amx, const cell *params)
{
  time_t sec1970;
  struct tm gtm;
  cell *cptr;

  assert(params[0]==(int)(3*sizeof(cell)));

  time(&sec1970);

  /* on DOS/Windows, the timezone is usually not set for the C run-time
   * library; in that case gmtime() and localtime() return the same value
   */
  gtm=*localtime(&sec1970);
  if (amx_GetAddr(amx,params[1],&cptr)==AMX_ERR_NONE)
    *cptr=gtm.tm_hour;
  if (amx_GetAddr(amx,params[2],&cptr)==AMX_ERR_NONE)
    *cptr=gtm.tm_min;
  if (amx_GetAddr(amx,params[3],&cptr)==AMX_ERR_NONE)
    *cptr=gtm.tm_sec;

  /* the time() function returns the number of seconds since January 1 1970
   * in Universal Coordinated Time (the successor to Greenwich Mean Time)
   */
  return (cell)sec1970;
}

/* setdate(year, month, day)
 * Always returns 0
 */
static cell AMX_NATIVE_CALL n_setdate(AMX *amx, const cell *params)
{
  (void)amx;
  setdate(params[1],params[2],params[3]);
  return 0;
}

/* getdate(&year, &month, &day)
 * The return value is the number of days since the start of the year. January
 * 1 is day 1 of the year.
 */
static cell AMX_NATIVE_CALL n_getdate(AMX *amx, const cell *params)
{
  time_t sec1970;
  struct tm gtm;
  cell *cptr;

  assert(params[0]==(int)(3*sizeof(cell)));

  time(&sec1970);

  gtm=*localtime(&sec1970);
  if (amx_GetAddr(amx,params[1],&cptr)==AMX_ERR_NONE)
    *cptr=gtm.tm_year+1900;
  if (amx_GetAddr(amx,params[2],&cptr)==AMX_ERR_NONE)
    *cptr=gtm.tm_mon+1;
  if (amx_GetAddr(amx,params[3],&cptr)==AMX_ERR_NONE)
    *cptr=gtm.tm_mday;

  return gtm.tm_yday+1;
}

/* tickcount(&granularity)
 * Returns the number of milliseconds since start-up. For a 32-bit cell, this
 * count overflows after approximately 24 days of continuous operation.
 */
static cell AMX_NATIVE_CALL n_tickcount(AMX *amx, const cell *params)
{
  cell *cptr;

  assert(params[0]==(int)sizeof(cell));

  INIT_TIMER();
  #if defined __WIN32__ || defined _WIN32 || defined WIN32
    if (amx_GetAddr(amx,params[1],&cptr)==AMX_ERR_NONE)
      *cptr=1000;               /* granularity = 1 ms */
  #else
    if (amx_GetAddr(amx,params[1],&cptr)==AMX_ERR_NONE)
      *cptr=(cell)CLOCKS_PER_SEC;       /* in Unix/Linux, this is often 100 */
  #endif
  return gettimestamp() & 0x7fffffff;
}

/* delay(milliseconds)
 * Pauses for (at least) the requested number of milliseconds.
 */
static cell AMX_NATIVE_CALL n_delay(AMX *amx, const cell *params)
{
  unsigned long stamp;

  (void)amx;
  assert(params[0]==(int)sizeof(cell));

  INIT_TIMER();
  stamp=gettimestamp();
  while (gettimestamp()-stamp < (unsigned long)params[1])
    /* nothing */;
  return 0;
}

/* settimer(milliseconds, bool: singleshot = false)
 * Sets the delay until the @timer() callback is called. The timer may either
 * be single-shot or repetitive.
 */
static cell AMX_NATIVE_CALL n_settimer(AMX *amx, const cell *params)
{
  (void)amx;
  assert(params[0]==(int)(2*sizeof(cell)));
  timestamp=gettimestamp();
  timelimit=params[1];
  timerepeat=(int)(params[2]==0);
  return 0;
}

/* bool: gettimer(&milliseconds, bool: &singleshot = false)
 * Retrieves the timer set with settimer(); returns true if a timer
 * was set up, or false otherwise.
 */
static cell AMX_NATIVE_CALL n_gettimer(AMX *amx, const cell *params)
{
  cell *cptr;

  assert(params[0]==(int)(2*sizeof(cell)));
  if (amx_GetAddr(amx,params[1],&cptr)==AMX_ERR_NONE)
    *cptr=timelimit;
  if (amx_GetAddr(amx,params[1],&cptr)==AMX_ERR_NONE)
    *cptr=timerepeat;
  return timelimit>0;
}

/* settimestamp(seconds1970) sets the date and time from a single parameter: the
 * number of seconds since 1 January 1970.
 */
static cell AMX_NATIVE_CALL n_settimestamp(AMX *amx, const cell *params)
{
  #if defined __WIN32__ || defined _WIN32 || defined WIN32
    int year, month, day, hour, minute, second;

    stamp2datetime(params[1],
                   &year, &month, &day,
                   &hour, &minute, &second);
    setdate(year, month, day);
    settime(hour, minute, second);
  #else
    /* Linux/Unix (and some DOS compilers) have stime(); on Linux/Unix, you
     * must have "root" permission to call stime()
     */
    time_t sec1970=(time_t)params[1];
    stime(&sec1970);
  #endif
  (void)amx;

  return 0;
}

/* cvttimestamp(seconds1970, &year, &month, &day, &hour, &minute, &second)
 */
static cell AMX_NATIVE_CALL n_cvttimestamp(AMX *amx, const cell *params)
{
  int year, month, day, hour, minute, second;

  (void)amx;
  stamp2datetime(params[1],
                 &year, &month, &day,
                 &hour, &minute, &second);
  return 0;
}


#if !defined AMXTIME_NOIDLE
static AMX_IDLE PrevIdle = NULL;
static int idxTimer = -1;

static int AMXAPI amx_TimeIdle(AMX *amx, int AMXAPI Exec(AMX *, cell *, int))
{
  int err=0;

  assert(idxTimer >= 0);

  if (PrevIdle != NULL)
    PrevIdle(amx, Exec);

  if (timelimit>0 && (gettimestamp()-timestamp)>=timelimit) {
    if (timerepeat)
      timestamp+=timelimit;
    else
      timelimit=0;      /* do not repeat single-shot timer */
    err = Exec(amx, NULL, idxTimer);
    while (err == AMX_ERR_SLEEP)
      err = Exec(amx, NULL, AMX_EXEC_CONT);
  } /* if */

  return err;
}
#endif


#if defined __cplusplus
  extern "C"
#endif
const AMX_NATIVE_INFO time_Natives[] = {
  { "gettime",      n_gettime },
  { "settime",      n_settime },
  { "getdate",      n_getdate },
  { "setdate",      n_setdate },
  { "tickcount",    n_tickcount },
  { "settimer",     n_settimer },
  { "gettimer",     n_gettimer },
  { "delay",        n_delay },
  { "settimestamp", n_settimestamp },
  { "cvttimestamp", n_cvttimestamp },
  { NULL, NULL }        /* terminator */
};

int AMXEXPORT AMXAPI amx_TimeInit(AMX *amx)
{
  #if !defined AMXTIME_NOIDLE
    /* see whether there is a @timer() function */
    if (amx_FindPublic(amx,"@timer",&idxTimer) == AMX_ERR_NONE) {
      if (amx_GetUserData(amx, AMX_USERTAG('I','d','l','e'), (void**)&PrevIdle) != AMX_ERR_NONE)
        PrevIdle = NULL;
      amx_SetUserData(amx, AMX_USERTAG('I','d','l','e'), amx_TimeIdle);
    } /* if */
  #endif

  return amx_Register(amx, time_Natives, -1);
}

int AMXEXPORT AMXAPI amx_TimeCleanup(AMX *amx)
{
  (void)amx;
  #if !defined AMXTIME_NOIDLE
    PrevIdle = NULL;
  #endif
  return AMX_ERR_NONE;
}
