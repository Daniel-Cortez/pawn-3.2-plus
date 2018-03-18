/*  String functions for the Pawn Abstract Machine
 *
 *  Copyright (c) ITB CompuPhase, 2005-2008
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
 *  Version: $Id: amxstring.c 3827 2007-10-16 14:53:31Z thiadmer $
 */

#include <limits.h>
#include <string.h>
#include <assert.h>
#if defined __WIN32__ || defined _WIN32 || defined WIN32 || defined __MSDOS__
  #include <malloc.h>
#endif
#include "amx.h"
#if defined __WIN32__ || defined _WIN32 || defined WIN32 || defined _Windows
  #include <windows.h>
#endif

#define MAX_FORMATSTR   256

#define CHARBITS        (8*sizeof(char))

#if defined _UNICODE
# include <tchar.h>
#elif !defined __T
  typedef char          TCHAR;
# define __T(string)    string
# define _tcscat        strcat
# define _tcschr        strchr
# define _tcscpy        strcpy
# define _tcslen        strlen
#endif
#include "amxcons.h"

#if !defined isdigit
# define isdigit(c)     ((unsigned)((c)-'0')<10u)
#endif
#if !defined sizearray
# define sizearray(a)   (sizeof(a) / sizeof((a)[0]))
#endif

#if defined __clang__
  #pragma clang diagnostic ignored "-Wlogical-op-parentheses"
#endif

#define EXPECT_PARAMS(num) \
  do { \
    if (params[0]!=(num)*sizeof(cell)) \
      return amx_RaiseError(amx,AMX_ERR_PARAMS),0; \
  } while(0)
#define EXPECT_PARAMS_VA(num) \
  do { \
    if (params[0]<(num)*sizeof(cell)) \
      return amx_RaiseError(amx,AMX_ERR_PARAMS),0; \
  } while(0)


/* dest     the destination buffer; the buffer must point to the start of a cell
 * source   the source buffer, this must be aligned to a cell edge
 * len      the number of characters (bytes) to copy
 * offs     the offset in dest, in characters (bytes)
 */
static int amx_StrPack(cell *dest,cell *source,int len,int offs)
{
  int i;

  if ((ucell)*source>UNPACKEDMAX && offs%sizeof(cell)==0) {
    /* source string is already packed and the destination is cell-aligned */
    unsigned char* pdest=(unsigned char*)dest+offs;
    i=(len+sizeof(cell)-1)/sizeof(cell);
    memmove(pdest,source,i*sizeof(cell));
    /* zero-terminate */
    #if BYTE_ORDER==BIG_ENDIAN
      pdest+=len;
      for (i=len; i==len || i%sizeof(cell)!=0; i++)
        *pdest++='\0';
    #else
      i=(len/sizeof(cell))*sizeof(cell);
      pdest+=i;
      len=(len==i) ? sizeof(cell) : sizeof(cell)-(len-i);
      assert(len>0 && len<=sizeof(cell));
      for (i=0; i<len; i++)
        *pdest++='\0';
    #endif
  } else if ((ucell)*source>UNPACKEDMAX) {
    /* source string is packed, destination is not aligned */
    cell mask,c;
    dest+=offs/sizeof(cell);    /* increment whole number of cells */
    offs%=sizeof(cell);         /* get remainder */
    mask=(~(ucell)0) >> (offs*CHARBITS);
    c=*dest & ~mask;
    for (i=0; i<len+offs+1; i+=sizeof(cell)) {
      *dest=c | ((*source >> (offs*CHARBITS)) & mask);
      c=(*source << ((sizeof(cell)-offs)*CHARBITS)) & ~mask;
      dest++;
      source++;
    } /* for */
    /* set the zero byte in the last cell */
    mask=(~(ucell)0) >> (((offs+len)%sizeof(cell))*CHARBITS);
    *(dest-1) &= ~mask;
  } else {
    /* source string is unpacked: pack string, from top-down */
    cell c=0;
    if (offs!=0) {
      /* get the last cell in "dest" and mask of the characters that must be changed */
      cell mask;
      dest+=offs/sizeof(cell);  /* increment whole number of cells */
      offs%=sizeof(cell);       /* get remainder */
      mask=(~(ucell)0) >> (offs*CHARBITS);
      c=(*dest & ~mask) >> ((sizeof(cell)-offs)*CHARBITS);
    } /* if */
    /* for proper alignement, add the offset to both the starting and the ending
     * criterion (so that the number of iterations stays the same)
     */
    assert(offs>=0 && offs<sizeof(cell));
    for (i=offs; i<len+offs; i++) {
      c=(c<<CHARBITS) | (*source++ & 0xff);
      if (i%sizeof(cell)==sizeof(cell)-1) {
        *dest++=c;
        c=0;
      } /* if */
    } /* for */
    if (i%sizeof(cell) != 0)    /* store remaining packed characters */
      *dest=c << (sizeof(cell)-i%sizeof(cell))*CHARBITS;
    else
      *dest=0;                  /* store full cell of zeros */
  } /* if */
  return AMX_ERR_NONE;
}

static int amx_StrUnpack(cell *dest,cell *source,int len)
{
  /* len excludes the terminating '\0' byte */
  if ((ucell)*source>UNPACKEDMAX) {
    /* unpack string, from bottom up (so string can be unpacked in place) */
    cell c;
    int i;
    for (i=len-1; i>=0; i--) {
      c=source[i/sizeof(cell)] >> (sizeof(cell)-i%sizeof(cell)-1)*CHARBITS;
      dest[i]=c & UCHAR_MAX;
    } /* for */
    dest[len]=0;        /* zero-terminate */
  } else {
    /* source string is already unpacked */
    while (len-->0)
      *dest++=*source++;
    *dest=0;
  } /* if */
  return AMX_ERR_NONE;
}

static unsigned char *packedptr(cell *string,int index)
{
  unsigned char *ptr=(unsigned char *)(string+index/sizeof(cell));
  #if BYTE_ORDER==BIG_ENDIAN
    ptr+=index & (sizeof(cell)-1);
  #else
    ptr+=(sizeof(cell)-1) - (index & (sizeof(cell)-1));
  #endif
  return ptr;
}

static cell extractchar(cell *string,int index,int mklower)
{
  cell c;

  if ((ucell)*string>UNPACKEDMAX)
    c=*packedptr(string,index);
  else
    c=string[index];
  if (mklower) {
    #if defined __WIN32__ || defined _WIN32 || defined WIN32
      c=(cell)CharLower((LPTSTR)c);
    #elif defined _Windows
      c=(cell)AnsiLower((LPSTR)c);
    #else
      if ((unsigned int)(c-'A')<26u)
        c+='a'-'A';
    #endif
  } /* if */
  return c;
}

static int verify_addr(AMX *amx,cell addr)
{
  cell *cdest;
  return amx_GetAddr(amx,addr,&cdest);
}

/* strlen(const string[])
 */
static cell AMX_NATIVE_CALL n_strlen(AMX *amx,const cell *params)
{
  cell *cptr;
  int len;

  EXPECT_PARAMS(1);

  if (amx_GetAddr(amx,params[1],&cptr)!=AMX_ERR_NONE) {
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  amx_StrLen(cptr,&len);
  return len;
}

/* strpack(dest[], const source[], maxlength=sizeof dest)
 */
static cell AMX_NATIVE_CALL n_strpack(AMX *amx,const cell *params)
{
  cell *cdest,*csrc;
  int len;

  EXPECT_PARAMS(3);

  if (amx_GetAddr(amx,params[1],&cdest)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  if (amx_GetAddr(amx,params[2],&csrc)!=AMX_ERR_NONE)
    goto err_native;
  if (params[3]<=0 || verify_addr(amx,params[1]+params[3])!=AMX_ERR_NONE)
    goto err_native;

  amx_StrLen(csrc,&len);
  if ((unsigned)len>=params[3]*sizeof(cell))
    len=params[3]*sizeof(cell)-1;
  if (amx_StrPack(cdest,csrc,len,0)!=AMX_ERR_NONE)
    goto err_native;

  return len;
}

/* strunpack(dest[], const source[], maxlength=sizeof dest)
 */
static cell AMX_NATIVE_CALL n_strunpack(AMX *amx,const cell *params)
{
  cell *cdest,*csrc;
  int len;

  EXPECT_PARAMS(3);

  if (amx_GetAddr(amx,params[1],&cdest)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  if (amx_GetAddr(amx,params[2],&csrc)!=AMX_ERR_NONE)
    goto err_native;
  if (params[3]<=0 || verify_addr(amx,params[1]+params[3])!=AMX_ERR_NONE)
    goto err_native;

  amx_StrLen(csrc,&len);
  if (len>=params[3])
    len=params[3]-1;
  if (amx_StrUnpack(cdest,csrc,len)!=AMX_ERR_NONE)
    goto err_native;

  return len;
}

/* strcat(dest[], const source[], maxlength=sizeof dest)
 * packed/unpacked attribute is taken from dest[], or from source[] if dest[]
 * is an empty string.
 */
static cell AMX_NATIVE_CALL n_strcat(AMX *amx,const cell *params)
{
  cell *cdest,*csrc;
  int len,len2,bufsize;
  int packed,err;
  size_t lastaddr;

  EXPECT_PARAMS(3);

  if (amx_GetAddr(amx,params[1],&cdest)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */

  if (amx_GetAddr(amx,params[2],&csrc)!=AMX_ERR_NONE)
    goto err_native;
  if (params[3]<=0 || verify_addr(amx,params[1]+params[3])!=AMX_ERR_NONE)
    goto err_native;
  amx_StrLen(csrc,&len);
  amx_StrLen(cdest,&len2);
  packed=(*cdest==0) ? ((ucell)*csrc>UNPACKEDMAX) : ((ucell)*cdest>UNPACKEDMAX);

  bufsize=params[3];
  if (packed)
    bufsize *= sizeof(cell);
  if (len+len2>=bufsize)
    len=bufsize-len2-1;

  if (packed) {
    err=amx_StrPack(cdest,csrc,len,len2);
  } else {
    /* destination string must either be unpacked, or empty */
    assert((ucell)*cdest<=UNPACKEDMAX || len2==0);
    err=amx_StrUnpack(cdest+len2,csrc,len);
  } /* if */
  if (err!=AMX_ERR_NONE)
    goto err_native;

  return len;
}

/* strcopy(dest[], const source[], maxlength=sizeof dest)
 * packed/unpacked attribute from source[]
 */
static cell AMX_NATIVE_CALL n_strcopy(AMX *amx,const cell *params)
{
  cell *cdest,*csrc;
  int len,bufsize;
  int packed,err;

  EXPECT_PARAMS(3);

  if (amx_GetAddr(amx,params[1],&cdest)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  if (amx_GetAddr(amx,params[2],&csrc)!=AMX_ERR_NONE)
    goto err_native;
  if (params[3]<=0 || verify_addr(amx,params[1]+params[3])!=AMX_ERR_NONE)
    goto err_native;
  amx_StrLen(csrc,&len);
  packed=(ucell)*csrc>UNPACKEDMAX;

  bufsize=params[3];
  if (packed)
    bufsize *= sizeof(cell);
  if (len>=bufsize)
    len=bufsize-1;

  if (packed)
    err=amx_StrPack(cdest,csrc,len,0);
  else
    err=amx_StrUnpack(cdest,csrc,len);
  if (err!=AMX_ERR_NONE)
    goto err_native;

  return len;
}

static int compare(cell *cstr1,cell *cstr2,int ignorecase,int length,int offs1)
{
  int index;
  cell c1=0,c2=0;

  for (index=0; index<length; index++) {
    c1=extractchar(cstr1,index+offs1,ignorecase);
    c2=extractchar(cstr2,index,ignorecase);
    assert(c1!=0 && c2!=0); /* string lengths are already checked, so zero-bytes should not occur */
    if (c1!=c2)
      break;
  } /* for */

  if (c1<c2)
    return -1;
  if (c1>c2)
    return 1;
  return 0;
}

/* strcmp(const string1[], const string2[], bool:ignorecase=false, length=cellmax)
 */
static cell AMX_NATIVE_CALL n_strcmp(AMX *amx,const cell *params)
{
  cell *cstr1,*cstr2;
  int len1,len2,len;
  cell result;

  EXPECT_PARAMS(4);

  if (params[4]==0)
    return 0;

  if (amx_GetAddr(amx,params[1],&cstr1)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  if (amx_GetAddr(amx,params[2],&cstr2)!=AMX_ERR_NONE)
    goto err_native;

  /* get the maximum length to compare */
  amx_StrLen(cstr1,&len1);
  amx_StrLen(cstr2,&len2);
  len=len1;
  if (len>len2)
    len=len2;
  if (len>params[4])
    len=params[4];

  result=compare(cstr1,cstr2,params[3],len,0);
  if (result==0 && len!=params[4]) {
    if (len1>len2)
      return 1;
    if (len1<len2)
      return -1;
  } /* if */
  return result;
}

/* strfind(const string[], const sub[], bool:ignorecase=false, index=0)
 */
static cell AMX_NATIVE_CALL n_strfind(AMX *amx,const cell *params)
{
  cell *cstr,*csub;
  int lenstr,lensub,offs;
  cell c,f;

  EXPECT_PARAMS(4);

  if (amx_GetAddr(amx,params[1],&cstr)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  if (amx_GetAddr(amx,params[2],&csub)!=AMX_ERR_NONE)
    goto err_native;

  /* get the maximum length to compare */
  amx_StrLen(cstr,&lenstr);
  amx_StrLen(csub,&lensub);
  if (lensub==0)
    return -1;

  /* get the start character of the substring, for quicker searching */
  f=extractchar(csub,0,params[3]);
  assert(f!=0);         /* string length is already checked */

  for (offs=(int)params[4]; offs+lensub<=lenstr; offs++) {
    /* find the initial character */
    c=extractchar(csub,0,params[3]);
    assert(c!=0);      /* string length is already checked */
    if (c!=f)
      continue;
    if (compare(cstr,csub,params[3],lensub,offs)==0)
      return offs;
  } /* for */
  return -1;
}

/* strmid(dest[], const source[], start=0, end=cellmax, maxlength=sizeof dest)
 * packed/unpacked attribute is taken from source[]
 */
static cell AMX_NATIVE_CALL n_strmid(AMX *amx,const cell *params)
{
  cell *cdest,*csrc;
  int len,err,bufsize,packed;
  int soffs,doffs;
  size_t lastaddr;
  unsigned char *ptr;
  unsigned char c;
  int start,end;

  EXPECT_PARAMS(5);

  if (amx_GetAddr(amx,params[1],&cdest)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  if (amx_GetAddr(amx,params[2],&csrc)!=AMX_ERR_NONE)
    goto err_native;
  if (params[5]<=0 || verify_addr(amx,params[1]+params[5])!=AMX_ERR_NONE)
    goto err_native;
  amx_StrLen(csrc,&len);
  start=params[3];
  end=params[4];

  /* clamp the start/end parameters */
  if (start<0)
    start=0;
  else if (start>len)
    start=len;
  if (end<start)
    end=start;
  else if (end>len)
    end=len;

  len=end-start;
  packed=((ucell)*csrc>UNPACKEDMAX);
  bufsize=params[5];
  if (packed)
    bufsize *= sizeof(cell);
  if (len>=bufsize)
    len=bufsize-1;

  if (packed) {
    /* first align the source to a cell boundary */
    for (doffs=0,soffs=start; (soffs & (sizeof(cell)-1))!=0 && len>0; soffs++,doffs++,len--) {
      ptr=packedptr(csrc,soffs);
      c=*ptr;
      ptr=packedptr(cdest,doffs);
      *ptr=c;
    } /* for */
    if (len==0) {
      /* nothing left to do, zero-terminate */
      ptr=packedptr(cdest,doffs);
      *ptr='\0';
      err=AMX_ERR_NONE;
    } else {
      err=amx_StrPack(cdest,csrc+soffs/sizeof(cell),len,doffs);
    } /* if */
  } else {
    err=amx_StrUnpack(cdest,csrc+start,len);
  } /* if */
  if (err!=AMX_ERR_NONE)
    goto err_native;

  return len;
}

/* bool: strdel(string[], start, end)
 */
static cell AMX_NATIVE_CALL n_strdel(AMX *amx,const cell *params)
{
  cell *cstr;
  int index,offs,length;
  unsigned char *ptr;
  unsigned char c;

  EXPECT_PARAMS(3);

  /* calculate number of cells needed for (packed) destination */
  if (amx_GetAddr(amx,params[1],&cstr)!=AMX_ERR_NONE) {
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  amx_StrLen(cstr,&length);
  index=(int)params[2];
  offs=(int)params[3]-index;
  if (index>=length || offs<=0)
    return 0;
  if (index+offs>length)
    offs=length-index;

  index--;  /* prepare for increment in the top of the loop */
  if (((ucell)*cstr>UNPACKEDMAX)) {
    do {
      index++;
      ptr=packedptr(cstr,index+offs);
      c=*ptr;
      ptr=packedptr(cstr,index);
      *ptr=c;
    } while (c!='\0');
    if (index==0)
      *cstr=0;
  } else {
    do {
      index++;
      cstr[index]=cstr[index+offs];
    } while (cstr[index]!=0);
  } /* if */

  return 1;
}

/* bool: strins(string[], const substr[], index, maxlength=sizeof string)
 * packed/unpacked attribute is taken from string[], or from substr[]
 * if string[] is an empty string.
 */
static cell AMX_NATIVE_CALL n_strins(AMX *amx,const cell *params)
{
  cell *cstr,*csub;
  int offset,lenstr,lensub,count,count2,bufsize,newlen,packed;

  EXPECT_PARAMS(4);

  /* calculate number of cells needed for (packed) destination */
  if (amx_GetAddr(amx,params[1],&cstr)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  if (amx_GetAddr(amx,params[2],&csub)!=AMX_ERR_NONE)
    goto err_native;
  if (params[4]<=0 || verify_addr(amx,(params[1]+params[4]))!=AMX_ERR_NONE)
    goto err_native;
  amx_StrLen(cstr,&lenstr);
  amx_StrLen(csub,&lensub);
  offset=(int)params[3];
  bufsize=(int)params[4];
  packed=(*cstr==0) ? ((ucell)*csub>UNPACKEDMAX) : ((ucell)*cstr>UNPACKEDMAX);
  if (packed)
    bufsize *= sizeof(cell);
  if (offset>lenstr || offset>=bufsize)
    goto err_native;
  newlen=lenstr+lensub;
  if (newlen>=bufsize)
    newlen=bufsize-1;

  if (packed) {
    unsigned char c;
    /* make room for the new characters */
    *(packedptr(cstr,newlen))=(unsigned char)'\0';
    for (count=newlen-lensub-1,count2=newlen-1; count>=offset; count--,count2--) {
      c=*(packedptr(cstr,count));
      *(packedptr(cstr,count2))=c;
    } /* for */
    /* copy in the new characters */
    for (count=0,count2=offset; count<lensub && count2<newlen; count++,count2++) {
      c=(unsigned char)extractchar(csub,count,0);
      *(packedptr(cstr,count2))=c;
    } /* for */
  } else {
    cell c;
    /* make room for the new characters */
    cstr[newlen]=(cell)'\0';
    for (count=newlen-lensub-1,count2=newlen-1; count>=offset; count--,count2--)
      cstr[count2]=cstr[count];
    /* copy in the new characters */
    for (count=0,count2=offset; count<lensub && count2<newlen; count++,count2++) {
      c=extractchar(csub,count,0);
      cstr[count2]=c;
    } /* for */
  } /* if */

  return 1;
}

/* strval(const string[], index=0)
 */
static cell AMX_NATIVE_CALL n_strval(AMX *amx,const cell *params)
{
  TCHAR str[50],*ptr;
  cell *cstr,result;
  int len,negate,offset;

  EXPECT_PARAMS(2);

  /* get parameters */
  if (amx_GetAddr(amx,params[1],&cstr)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  amx_StrLen(cstr,&len);
  negate=0;
  offset=0;
  if ((unsigned)params[0]>=2*sizeof(cell))
    offset=params[2];
  if (offset<0)
    offset=0;
  else if (offset>=len)
    offset=len-1;

  /* skip a number of cells */
  while (offset>=(int)sizeof(cell)) {
    cstr++;
    offset-=sizeof(cell);
    len-=sizeof(cell);
  } /* while */

  if (len>=(int)sizeof str)
    goto err_native;
  amx_GetString(str,cstr,sizeof(TCHAR)>1,sizeof str);
  assert(offset<(int)sizeof(cell) && offset>=0);
  ptr=str+offset;
  result=0;
  while (*ptr!='\0' && *ptr<=' ')
    ptr++;              /* skip whitespace */
  if (*ptr=='-') {      /* handle sign */
    negate=1;
    ptr++;
  } else if (*ptr=='+') {
    ptr++;
  } /* if */
  while (isdigit(*ptr)) {
    result=result*10 + (*ptr-'0');
    ptr++;
  } /* while */
  if (negate)
    result=-result;
  return result;
}

/* valstr(dest[], value, bool:pack=false) */
static cell AMX_NATIVE_CALL n_valstr(AMX *amx,const cell *params)
{
  TCHAR str[50];
  cell value,temp;
  cell *cstr;
  int len,result,negate;

  EXPECT_PARAMS(3);

  /* find out how many digits are needed */
  negate=0;
  len=1;
  value=params[2];
  if (value<0) {
    negate=1;
    len++;
    value=-value;
  } /* if */
  for (temp=value; temp>=10; temp/=10)
    len++;
  assert(len<=sizearray(str));

  /* put in the string */
  result=len;
  str[len--]='\0';
  while (len>=negate) {
    str[len--]=(char)((value % 10)+'0');
    value/=10;
  } /* while */
  if (negate)
    str[0]='-';
  if (amx_GetAddr(amx,params[1],&cstr)!=AMX_ERR_NONE) {
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  amx_SetString(cstr,str,params[3],sizeof(TCHAR)>1,UNLIMITED);
  return result;
}

/* bool: ispacked(const string[]) */
static cell AMX_NATIVE_CALL n_ispacked(AMX *amx,const cell *params)
{
  cell *cstr;
  EXPECT_PARAMS(1);
  if (amx_GetAddr(amx,params[1],&cstr)!=AMX_ERR_NONE) {
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  return *cstr>=UNPACKEDMAX;
}


/* single character decode and encode */
#define BITMASK 0x3f
#define DEC(c)  (((c) - ' ') & BITMASK)
#define ENC(c)  (char)(((c) & BITMASK) == 0 ? 0x60 : ((c) & BITMASK) + ' ')

static int uudecode(unsigned char *target, char *source)
{
  int len, retval;

  len = DEC(*source++);
  retval = len;
  while (len > 0) {
    if (len-- > 0)
      *target++ = (unsigned char)(( DEC(source[0]) << 2 ) | ( DEC(source[1]) >> 4 ));
    if (len-- > 0)
      *target++ = (unsigned char)(( DEC(source[1]) << 4 ) | ( DEC(source[2]) >> 2 ));
    if (len-- > 0)
      *target++ = (unsigned char)(( DEC(source[2]) << 6 ) | DEC(source[3]) );
    source += 4;
  } /* while */
  return retval;
}

static int uuencode(char *target, unsigned char *source, int length)
{
  int split[4]={0,0,0,0};

  if (length > BITMASK)
    return 0;                           /* can encode up to 64 bytes */

  *target++ = ENC(length);
  while (length > 0) {
    split[0] = source[0] >> 2;          /* split first byte to char. 0 & 1 */
    split[1] = source[0] << 4;
    if (length > 1) {
      split[1] |= source[1] >> 4;       /* split 2nd byte to char. 1 & 2 */
      split[2] = source[1] << 2;
      if (length > 2) {
        split[2] |= source[2] >> 6;     /* split 3th byte to char. 2 & 3 */
        split[3] = source[2];
      } /* if */
    } /* if */

    *target++ = ENC(split[0]);
    *target++ = ENC(split[1]);
    if (length > 1)
      *target++ = ENC(split[2]);
    if (length > 2)
      *target++ = ENC(split[3]);
    source += 3;
    length -= 3;
  } /* while */

  *target = '\0';                       /* end string */
  return 1;
}

/* uudecode(dest[], const source[], maxlength=sizeof dest)
 * Returns the number of bytes (not cells) decoded; if the dest buffer is
 * too small, not all bytes are stored.
 * Always creates a (packed) array (not a string; the array is not
 * zero-terminated).
 * A buffer may be decoded "in-place"; the destination size is always smaller
 * than the source size.
 * Endian issues (for multi-byte values in the data stream) are not handled.
 */
static cell AMX_NATIVE_CALL n_uudecode(AMX *amx,const cell *params)
{
  cell *cdest,*cstr;
  unsigned char dst[BITMASK+2];
  char src[BITMASK+BITMASK/3+2];
  int len;
  size_t size;

  EXPECT_PARAMS(3);

  if (amx_GetAddr(amx,params[1],&cdest)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  if (params[3]<=0 || verify_addr(amx,params[1]+params[3])!=AMX_ERR_NONE)
    goto err_native;
  if (amx_GetAddr(amx,params[2],&cstr)!=AMX_ERR_NONE)
    goto err_native;
  /* get the source */
  amx_GetString(src,cstr,0,sizeof src);
  /* decode */
  len=uudecode(dst,src);
  /* store */
  size=len;
  if (size>params[3]*sizeof(cell))
    size=params[3]*sizeof(cell);
  memcpy(cdest,dst,size);
  return len;
}

/* uuencode(dest[], const source[], numbytes, maxlength=sizeof dest)
 * Returns the number of characters encoded, excluding the zero string
 * terminator; if the dest buffer is too small, not all bytes are stored.
 * Always creates a packed string. This string has a newline character at the
 * end. A buffer may be encoded "in-place" if the destination is large enough.
 * Endian issues (for multi-byte values in the data stream) are not handled.
 */
static cell AMX_NATIVE_CALL n_uuencode(AMX *amx,const cell *params)
{
  cell *cdest,*cstr;
  unsigned char src[BITMASK+2];
  char dst[BITMASK+BITMASK/3+2];
  cell numbytes;

  EXPECT_PARAMS(4);

  if (amx_GetAddr(amx,params[1],&cdest)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  if (amx_GetAddr(amx,params[2],&cstr)!=AMX_ERR_NONE)
    goto err_native;
  if (params[4]<=0 || verify_addr(amx,params[1]+params[4])!=AMX_ERR_NONE)
    goto err_native;
  numbytes=params[3];
  if (numbytes>params[4]*sizeof(cell))
    numbytes=params[4]*sizeof(cell);
  /* get the source */
  amx_GetString((char *)src,cstr,0,sizeof src);
  /* encode (and check for errors) */
  if (uuencode(dst,src,params[3])) {
    *cstr=0;
    return 0;
  } /* if */
  /* always add a \n */
  assert(strlen(dst)+1<sizeof dst);
  strcat(dst,"\n");
  /* store */
  amx_SetString(cdest,dst,1,0,params[4]);
  return (((params[3]+2)/3) << 2)+2;
}

/* memcpy(dest[], const source[], index=0, numbytes, maxlength=sizeof dest)
 * This function can align byte strings in cell arrays, or concatenate two
 * byte strings in two arrays. The parameter "index" is a byte offset; "numbytes"
 * is the number of bytes to copy. Parameter "maxlength", however, is in cells.
 * This function allows copying in-place, for aligning memory buffers.
 * Endian issues (for multi-byte values in the data stream) are not handled.
 */
static cell AMX_NATIVE_CALL n_memcpy(AMX *amx,const cell *params)
{
  cell *cdest,*csrc;
  unsigned char *pdest,*psrc;

  EXPECT_PARAMS(5);

  if (amx_GetAddr(amx,params[1],&cdest)!=AMX_ERR_NONE) {
err_native:
    amx_RaiseError(amx,AMX_ERR_NATIVE);
    return 0;
  } /* if */
  if (amx_GetAddr(amx,params[2],&csrc)!=AMX_ERR_NONE)
    goto err_native;
  if (params[5]<=0 || verify_addr(amx,params[1]+params[5])!=AMX_ERR_NONE)
    goto err_native;
  if (params[3]<0 || params[4]<0 || (params[3]+params[4])>params[5]*(int)sizeof(cell))
    return 0;
  pdest=(unsigned char*)cdest+params[3];
  psrc=(unsigned char*)csrc;
  memmove(pdest,psrc,params[4]);
  return 1;
}

#if !defined AMX_NOSTRFMT
  static int str_putstr(void *dest,const TCHAR *str)
  {
    if (_tcslen((TCHAR*)dest)+_tcslen(str)<MAX_FORMATSTR)
      _tcscat((TCHAR*)dest,str);
    return 0;
  }

  static int str_putchar(void *dest,TCHAR ch)
  {
    int len=_tcslen((TCHAR*)dest);
    if (len<MAX_FORMATSTR-1) {
      ((TCHAR*)dest)[len]=ch;
      ((TCHAR*)dest)[len+1]='\0';
    } /* if */
    return 0;
  }
#endif

/* strformat(dest[], size=sizeof dest, bool:pack=false, const format[], {Fixed,Float,_}:...)
 */
static cell AMX_NATIVE_CALL n_strformat(AMX *amx,const cell *params)
{
  #if defined AMX_NOSTRFMT
    (void)amx;
    (void)params;
    return 0;
  #else
    cell *cdest,*cstr;
    AMX_FMTINFO info;
    TCHAR output[MAX_FORMATSTR];

    EXPECT_PARAMS_VA(4);

    memset(&info,0,sizeof info);
    info.params=params+5;
    info.numparams=(int)(params[0]/sizeof(cell))-4;
    info.skip=0;
    info.length=MAX_FORMATSTR;  /* max. length of the string */
    info.f_putstr=str_putstr;
    info.f_putchar=str_putchar;
    info.user=output;
    output[0] = __T('\0');

    if (amx_GetAddr(amx,params[1],&cdest)!=AMX_ERR_NONE) {
err_native:
      amx_RaiseError(amx,AMX_ERR_NATIVE);
      return 0;
    } /* if */
    if (params[2]<=0 || verify_addr(amx,params[1]+params[2])!=AMX_ERR_NONE)
      goto err_native;
    if (amx_GetAddr(amx,params[4],&cstr)!=AMX_ERR_NONE)
      goto err_native;
    amx_printstring(amx,cstr,&info);

    /* store the output string */
    amx_SetString(cdest,(char*)output,(int)params[3],sizeof(TCHAR)>1,(int)params[2]);
    return 1;
  #endif
}


#if defined __cplusplus
  extern "C"
#endif
const AMX_NATIVE_INFO string_Natives[] = {
  { "ispacked",  n_ispacked },
  { "memcpy",    n_memcpy },
  { "strcat",    n_strcat },
  { "strcmp",    n_strcmp },
  { "strcopy",   n_strcopy },
  { "strdel",    n_strdel },
  { "strfind",   n_strfind },
  { "strformat", n_strformat },
  { "strins",    n_strins },
  { "strlen",    n_strlen },
  { "strmid",    n_strmid },
  { "strpack",   n_strpack },
  { "strunpack", n_strunpack },
  { "strval",    n_strval },
  { "uudecode",  n_uudecode },
  { "uuencode",  n_uuencode },
  { "valstr",    n_valstr },
  { NULL, NULL }        /* terminator */
};

int AMXEXPORT AMXAPI amx_StringInit(AMX *amx)
{
  return amx_Register(amx, string_Natives, -1);
}

int AMXEXPORT AMXAPI amx_StringCleanup(AMX *amx)
{
  (void)amx;
  return AMX_ERR_NONE;
}
