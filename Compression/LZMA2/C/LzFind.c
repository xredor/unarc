/* LzFind.c -- Match finder for LZ algorithms
2009-04-22 : Igor Pavlov : Public domain */

#include <string.h>

#include "LzFind.h"
#include "LzHash.h"

#define kEmptyHashValue 0
//#define kMaxValForNormalize ((UInt32)(1<<22)-1)
//#define kNormalizeStepMin (1*mb)
#define kMaxValForNormalize ((UInt32)0x3FFFFFFF)
#define kNormalizeStepMin (64*mb)
#define kMaxHistorySize ((UInt32)3 << 30)

#define kStartMaxLen 3

#define OPTIMIZE_HT        /* store 2 bits of hashed string in every hash entry in order to speed up searching */
#ifdef OPTIMIZE_HT
#define getMatch(x)        ((x)%(1<<30))
#define saveMatch(pos,cur) ((pos) + (UInt32((cur)[0]) << 30))
#else
#define getMatch(x)        (x)
#define saveMatch(pos,cur) (pos)
#endif

static void LzInWindow_Free(CMatchFinder *p, ISzAlloc *alloc)
{
  if (!p->directInput)
  {
    alloc->Free(alloc, p->bufferBase);
    p->bufferBase = 0;
  }
}

/* keepSizeBefore + keepSizeAfter + keepSizeReserv must be < 4G) */

static int LzInWindow_Create(CMatchFinder *p, UInt32 keepSizeReserv, ISzAlloc *alloc)
{
  UInt32 blockSize = p->keepSizeBefore + p->keepSizeAfter + keepSizeReserv;
  if (p->directInput)
  {
    p->blockSize = blockSize;
    return 1;
  }
  if (p->bufferBase == 0 || p->blockSize != blockSize)
  {
    LzInWindow_Free(p, alloc);
    p->blockSize = blockSize;
    p->bufferBase = (Byte *)alloc->Alloc(alloc, (size_t)blockSize);
  }
  return (p->bufferBase != 0);
}

Byte *MatchFinder_GetPointerToCurrentPos(CMatchFinder *p) { return p->buffer; }
Byte MatchFinder_GetIndexByte(CMatchFinder *p, Int32 index) { return p->buffer[index]; }

UInt32 MatchFinder_GetNumAvailableBytes(CMatchFinder *p) { return p->streamPos - p->pos; }

void MatchFinder_ReduceOffsets(CMatchFinder *p, UInt32 subValue)
{
  p->posLimit -= subValue;
  p->pos -= subValue;
  p->streamPos -= subValue;
}

static void MatchFinder_ReadBlock(CMatchFinder *p)
{
  if (p->streamEndWasReached || p->result != SZ_OK)
    return;
  if (p->directInput)
  {
    UInt32 curSize = 0xFFFFFFFF - p->streamPos;
    if (curSize > p->directInputRem)
      curSize = (UInt32)p->directInputRem;
    p->directInputRem -= curSize;
    p->streamPos += curSize;
    if (p->directInputRem == 0)
      p->streamEndWasReached = 1;
    return;
  }
  for (;;)
  {
    Byte *dest = p->buffer + (p->streamPos - p->pos);
    size_t size = (p->bufferBase + p->blockSize - dest);
    if (size == 0)
      return;
    p->result = p->stream->Read(p->stream, dest, &size);
    if (p->result != SZ_OK)
      return;
    if (size == 0)
    {
      p->streamEndWasReached = 1;
      return;
    }
    p->streamPos += (UInt32)size;
    if (p->streamPos - p->pos > p->keepSizeAfter)
      return;
  }
}

void MatchFinder_MoveBlock(CMatchFinder *p)
{
  memmove(p->bufferBase,
    p->buffer - p->keepSizeBefore,
    (size_t)(p->streamPos - p->pos + p->keepSizeBefore));
  p->buffer = p->bufferBase + p->keepSizeBefore;
}

int MatchFinder_NeedMove(CMatchFinder *p)
{
  if (p->directInput)
    return 0;
  /* if (p->streamEndWasReached) return 0; */
  return ((size_t)(p->bufferBase + p->blockSize - p->buffer) <= p->keepSizeAfter);
}

void MatchFinder_ReadIfRequired(CMatchFinder *p)
{
  if (p->streamEndWasReached)
    return;
  if (p->keepSizeAfter >= p->streamPos - p->pos)
    MatchFinder_ReadBlock(p);
}

static void MatchFinder_CheckAndMoveAndRead(CMatchFinder *p)
{
  if (MatchFinder_NeedMove(p))
    MatchFinder_MoveBlock(p);
  MatchFinder_ReadBlock(p);
}

static void MatchFinder_SetDefaultSettings(CMatchFinder *p)
{
  p->cutValue = 32;
  p->btMode = MF_BinaryTree;
  p->numHashBytes = 4;
  p->bigHash = 0;
}

#define kCrcPoly 0xEDB88320

void MatchFinder_Construct(CMatchFinder *p)
{
  UInt32 i;
  p->bufferBase = 0;
  p->directInput = 0;
  p->hash = 0;
  MatchFinder_SetDefaultSettings(p);

  for (i = 0; i < 256; i++)
  {
    UInt32 r = i;
    int j;
    for (j = 0; j < 8; j++)
      r = (r >> 1) ^ (kCrcPoly & ~((r & 1) - 1));
    p->crc[i] = r;
  }
}

static void MatchFinder_FreeThisClassMemory(CMatchFinder *p, ISzAlloc *alloc)
{
  alloc->Free(alloc, p->hash);
  if (p->son)  alloc->Free(alloc, p->son);
  p->hash = 0;
  p->son  = 0;
}

void MatchFinder_Free(CMatchFinder *p, ISzAlloc *alloc)
{
  MatchFinder_FreeThisClassMemory(p, alloc);
  LzInWindow_Free(p, alloc);
}

static CLzRef* AllocRefs(UInt32 num, ISzAlloc *alloc)
{
  size_t sizeInBytes = (size_t)num * sizeof(CLzRef);
  if (sizeInBytes / sizeof(CLzRef) != num)
    return 0;
  return (CLzRef *)alloc->Alloc(alloc, sizeInBytes);
}

int MatchFinder_Create(CMatchFinder *p, UInt32 historySize, UInt32 hashSize,
    UInt32 keepAddBufferBefore, UInt32 matchMaxLen, UInt32 keepAddBufferAfter,
    ISzAlloc *alloc)
{
  UInt32 sizeReserv;
  if (historySize > kMaxHistorySize)
  {
    MatchFinder_Free(p, alloc);
    return 0;
  }
  if (p->btMode == MF_HashTable)
    sizeReserv  =  historySize <= 768*mb           ?  historySize/4  :  historySize/8;
  else
    sizeReserv  =  historySize <= ((UInt32)2 << 30)?  historySize/2  :  historySize/4;
  sizeReserv += (keepAddBufferBefore + matchMaxLen + keepAddBufferAfter) / 2 + (1 << 19);

  p->keepSizeBefore = historySize + keepAddBufferBefore + 1;
  p->keepSizeAfter = matchMaxLen + keepAddBufferAfter;
  /* we need one additional byte, since we use MoveBlock after pos++ and before dictionary using */

  p->matchMaxLen = matchMaxLen;
  hashSize /= sizeof(CLzRef);                                      // convert bytes to hashtable entries
  p->hashMask =  p->btMode==MF_HashTable? hashSize/p->cutValue-1   // every hash slot contains cutValue entries with the same hash value
                                        : hashSize-1;
  p->fixedHashSize = 0;
  if (p->numHashBytes > 2)   p->fixedHashSize += kHash2Size;
  if (p->numHashBytes > 3)   p->fixedHashSize += kHash3Size;
  if (p->numHashBytes > 4)   p->fixedHashSize += kHash4Size;

  {
    UInt32 prevHashSizeSum = p->hashSizeSum;
    UInt32 prevNumSons     = p->numSons;
    p->historySize = historySize;
    p->hashSizeSum = hashSize + p->fixedHashSize;
    p->cyclicBufferSize = historySize + 1;
    p->numSons = p->btMode==MF_BinaryTree ? p->cyclicBufferSize * 2
               : p->btMode==MF_HashChain  ? p->cyclicBufferSize
                                          : 0;
    if (p->hash != 0 && prevHashSizeSum == p->hashSizeSum &&
       (p->son  != 0 || prevNumSons == 0) && prevNumSons == p->numSons)
    {
      if (LzInWindow_Create(p, sizeReserv, alloc))
        return 1;
    }
    else
    {
      MatchFinder_FreeThisClassMemory(p, alloc);

      // Allocate larger block first (either dictionary or hash+son)
      if (historySize + sizeReserv > (p->hashSizeSum + p->numSons) * sizeof(CLzRef))
      {
        if (LzInWindow_Create(p, sizeReserv, alloc))
        {
          p->hash = AllocRefs(p->hashSizeSum, alloc);
          p->son  = p->numSons? AllocRefs(p->numSons, alloc) : 0;
          if (p->hash != 0  &&  (p->numSons==0 || p->son!=0))
            return 1;
        }
      }
      else
      {
        p->son  = p->numSons? AllocRefs(p->numSons, alloc) : 0;
        p->hash = AllocRefs(p->hashSizeSum, alloc);
        if (p->hash != 0  &&  (p->numSons==0 || p->son!=0))
          if (LzInWindow_Create(p, sizeReserv, alloc))
            return 1;
      }
    }
  }

  MatchFinder_Free(p, alloc);
  return 0;
}

static void MatchFinder_SetLimits(CMatchFinder *p)
{
  UInt32 limit = kMaxValForNormalize - p->pos;
  UInt32 limit2 = p->cyclicBufferSize - p->cyclicBufferPos;
  if (limit2 < limit)
    limit = limit2;
  limit2 = p->streamPos - p->pos;
  if (limit2 <= p->keepSizeAfter)
  {
    if (limit2 > 0)
      limit2 = 1;
  }
  else
    limit2 -= p->keepSizeAfter;
  if (limit2 < limit)
    limit = limit2;
  {
    UInt32 lenLimit = p->streamPos - p->pos;
    if (lenLimit > p->matchMaxLen)
      lenLimit = p->matchMaxLen;
    p->lenLimit = lenLimit;
  }
  p->posLimit = p->pos + limit;
}

void MatchFinder_Init(CMatchFinder *p)
{
  UInt32 i;
  for (i = 0; i < p->hashSizeSum; i++)
    p->hash[i] = kEmptyHashValue;
  p->cyclicBufferPos = 0;
  p->buffer = p->bufferBase;
  p->pos = p->streamPos = p->cyclicBufferSize;
  p->result = SZ_OK;
  p->streamEndWasReached = 0;
  MatchFinder_ReadBlock(p);
  MatchFinder_SetLimits(p);
}

static UInt32 MatchFinder_GetSubValue(CMatchFinder *p)
{
  return mymax(p->pos - p->historySize - 1, kNormalizeStepMin);
}

void MatchFinder_Normalize3(UInt32 subValue, CLzRef *items, UInt32 numItems, int btMode)
{
  UInt32 i;
  if (btMode==MF_HashTable)
  {
    for (i = 0; i < numItems; i++)
    {
      UInt32 value = items[i];
      if (getMatch(value) <= subValue)
        value = kEmptyHashValue;
      else
        value -= subValue;
      items[i] = value;
    }
  }
  else
  {
    for (i = 0; i < numItems; i++)
    {
      UInt32 value = items[i];
      if (value <= subValue)
        value = kEmptyHashValue;
      else
        value -= subValue;
      items[i] = value;
    }
  }
}

static void MatchFinder_Normalize(CMatchFinder *p)
{
  UInt32 subValue = MatchFinder_GetSubValue(p);
  MatchFinder_Normalize3(subValue, p->hash, p->hashSizeSum, p->btMode);
  MatchFinder_Normalize3(subValue, p->son,  p->numSons,     p->btMode);
  MatchFinder_ReduceOffsets(p, subValue);
}

static void MatchFinder_CheckLimits(CMatchFinder *p)
{
  if (p->pos == kMaxValForNormalize)
    MatchFinder_Normalize(p);
  if (!p->streamEndWasReached && p->keepSizeAfter == p->streamPos - p->pos)
    MatchFinder_CheckAndMoveAndRead(p);
  if (p->cyclicBufferPos == p->cyclicBufferSize)
    p->cyclicBufferPos = 0;
  MatchFinder_SetLimits(p);
}

static UInt32 * Hc_GetMatchesSpec(UInt32 lenLimit, UInt32 curMatch, UInt32 pos, const Byte *cur, CLzRef *son,
    UInt32 _cyclicBufferPos, UInt32 _cyclicBufferSize, UInt32 cutValue,
    UInt32 *distances, UInt32 maxLen)
{
  son[_cyclicBufferPos] = curMatch;
  for (;;)
  {
    UInt32 delta = pos - curMatch;
    if (cutValue-- == 0 || delta >= _cyclicBufferSize)
      return distances;
    {
      const Byte *pb = cur - delta;
      curMatch = son[_cyclicBufferPos - delta + ((delta > _cyclicBufferPos) ? _cyclicBufferSize : 0)];
      if (pb[maxLen] == cur[maxLen] && *pb == *cur)
      {
        UInt32 len = 0;
        while (++len != lenLimit)
          if (pb[len] != cur[len])
            break;
        if (maxLen < len)
        {
          *distances++ = maxLen = len;
          *distances++ = delta - 1;
          if (len == lenLimit)
            return distances;
        }
      }
    }
  }
}

static UInt32 * Ht_GetMatchesSpec(UInt32 lenLimit, UInt32 *curMatchPtr, UInt32 pos, const Byte *cur, CLzRef *son,
    UInt32 _cyclicBufferPos, UInt32 _cyclicBufferSize, UInt32 cutValue,
    UInt32 *distances, UInt32 maxLen)
{
  //UInt32 *curMatchPtr= &_hash[kFix4HashSize + hashValue*p->cutValue];  // First entry in hash table to check
  UInt32 *lastMatchPtr = curMatchPtr + cutValue;                         // Last entry to check + 1
  UInt32 prevMatch = saveMatch(pos,cur);

  do {
    UInt32 curMatch = *curMatchPtr;   *curMatchPtr++ = prevMatch;   prevMatch = curMatch;
#ifdef OPTIMIZE_HT
    if ((curMatch>>30) != (*cur&3))   continue;   // Check tag stored in 2 higher bits of hash entry
#endif
    curMatch = getMatch(curMatch);
    UInt32 delta = pos - curMatch;
    if (delta >= _cyclicBufferSize)
      break;
    const Byte *pb = cur - delta;
    if (pb[maxLen] == cur[maxLen] && *pb == *cur)
    {
      UInt32 len = 0;
      while (++len != lenLimit)
        if (pb[len] != cur[len])
          break;
      if (maxLen < len)
      {
        *distances++ = maxLen = len;
        *distances++ = delta - 1;
        if (len == lenLimit)
          break;
      }
    }
  } while (curMatchPtr < lastMatchPtr);

  return distances;
}

UInt32 * GetMatchesSpec1(UInt32 lenLimit, UInt32 curMatch, UInt32 pos, const Byte *cur, CLzRef *son,
    UInt32 _cyclicBufferPos, UInt32 _cyclicBufferSize, UInt32 cutValue,
    UInt32 *distances, UInt32 maxLen)
{
  CLzRef *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  CLzRef *ptr1 = son + (_cyclicBufferPos << 1);
  UInt32 len0 = 0, len1 = 0;
  for (;;)
  {
    UInt32 delta = pos - curMatch;
    if (cutValue-- == 0 || delta >= _cyclicBufferSize)
    {
      *ptr0 = *ptr1 = kEmptyHashValue;
      return distances;
    }
    {
      CLzRef *pair = son + ((_cyclicBufferPos - delta + ((delta > _cyclicBufferPos) ? _cyclicBufferSize : 0)) << 1);
      const Byte *pb = cur - delta;
      UInt32 len = (len0 < len1 ? len0 : len1);
      if (pb[len] == cur[len])
      {
        if (++len != lenLimit && pb[len] == cur[len])
          while (++len != lenLimit)
            if (pb[len] != cur[len])
              break;
        if (maxLen < len)
        {
          *distances++ = maxLen = len;
          *distances++ = delta - 1;
          if (len == lenLimit)
          {
            *ptr1 = pair[0];
            *ptr0 = pair[1];
            return distances;
          }
        }
      }
      if (pb[len] < cur[len])
      {
        *ptr1 = curMatch;
        ptr1 = pair + 1;
        curMatch = *ptr1;
        len1 = len;
      }
      else
      {
        *ptr0 = curMatch;
        ptr0 = pair;
        curMatch = *ptr0;
        len0 = len;
      }
    }
  }
}

static void SkipMatchesSpec(UInt32 lenLimit, UInt32 curMatch, UInt32 pos, const Byte *cur, CLzRef *son,
    UInt32 _cyclicBufferPos, UInt32 _cyclicBufferSize, UInt32 cutValue)
{
  CLzRef *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  CLzRef *ptr1 = son + (_cyclicBufferPos << 1);
  UInt32 len0 = 0, len1 = 0;
  for (;;)
  {
    UInt32 delta = pos - curMatch;
    if (cutValue-- == 0 || delta >= _cyclicBufferSize)
    {
      *ptr0 = *ptr1 = kEmptyHashValue;
      return;
    }
    {
      CLzRef *pair = son + ((_cyclicBufferPos - delta + ((delta > _cyclicBufferPos) ? _cyclicBufferSize : 0)) << 1);
      const Byte *pb = cur - delta;
      UInt32 len = (len0 < len1 ? len0 : len1);
      if (pb[len] == cur[len])
      {
        while (++len != lenLimit)
          if (pb[len] != cur[len])
            break;
        {
          if (len == lenLimit)
          {
            *ptr1 = pair[0];
            *ptr0 = pair[1];
            return;
          }
        }
      }
      if (pb[len] < cur[len])
      {
        *ptr1 = curMatch;
        ptr1 = pair + 1;
        curMatch = *ptr1;
        len1 = len;
      }
      else
      {
        *ptr0 = curMatch;
        ptr0 = pair;
        curMatch = *ptr0;
        len0 = len;
      }
    }
  }
}

#define MOVE_POS \
  ++p->cyclicBufferPos; \
  p->buffer++; \
  if (++p->pos == p->posLimit) MatchFinder_CheckLimits(p);

#define MOVE_POS_RET MOVE_POS return offset;

static void MatchFinder_MovePos(CMatchFinder *p) { MOVE_POS; }

#define GET_MATCHES_HEADER2(minLen, ret_op) \
  UInt32 lenLimit; UInt32 hashValue; const Byte *cur; UInt32 curMatch; \
  lenLimit = p->lenLimit; { if (lenLimit < minLen) { MatchFinder_MovePos(p); ret_op; }} \
  cur = p->buffer;

#define GET_MATCHES_HEADER(minLen) GET_MATCHES_HEADER2(minLen, return 0)
#define SKIP_HEADER(minLen)        GET_MATCHES_HEADER2(minLen, continue)

#define MF_PARAMS(p) p->pos, p->buffer, p->son, p->cyclicBufferPos, p->cyclicBufferSize, p->cutValue

#define GET_MATCHES_FOOTER(offset, maxLen) \
  offset = (UInt32)(GetMatchesSpec1(lenLimit, curMatch, MF_PARAMS(p), \
  distances + offset, maxLen) - distances); MOVE_POS_RET;

#define SKIP_FOOTER \
  SkipMatchesSpec(lenLimit, curMatch, MF_PARAMS(p)); MOVE_POS;

static UInt32 Bt2_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances)
{
  UInt32 offset;
  GET_MATCHES_HEADER(2)
  HASH2_CALC;
  curMatch = p->hash[hashValue];
  p->hash[hashValue] = p->pos;
  offset = 0;
  GET_MATCHES_FOOTER(offset, 1)
}

UInt32 Bt3Zip_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances)
{
  UInt32 offset;
  GET_MATCHES_HEADER(3)
  HASH_ZIP_CALC;
  curMatch = p->hash[hashValue];
  p->hash[hashValue] = p->pos;
  offset = 0;
  GET_MATCHES_FOOTER(offset, 2)
}

static UInt32 Bt3_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances)
{
  UInt32 hash2Value, delta2, maxLen, offset;
  GET_MATCHES_HEADER(3)

  HASH3_CALC;

  delta2 = p->pos - p->hash[hash2Value];
  curMatch = p->hash[kFix3HashSize + hashValue];

  p->hash[hash2Value] =
  p->hash[kFix3HashSize + hashValue] = p->pos;


  maxLen = 2;
  offset = 0;
  if (delta2 < p->cyclicBufferSize && *(cur - delta2) == *cur)
  {
    for (; maxLen != lenLimit; maxLen++)
      if (cur[(ptrdiff_t)maxLen - delta2] != cur[maxLen])
        break;
    distances[0] = maxLen;
    distances[1] = delta2 - 1;
    offset = 2;
    if (maxLen == lenLimit)
    {
      SkipMatchesSpec(lenLimit, curMatch, MF_PARAMS(p));
      MOVE_POS_RET;
    }
  }
  GET_MATCHES_FOOTER(offset, maxLen)
}

static UInt32 Bt4_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances)
{
  UInt32 hash2Value, hash3Value, delta2, delta3, maxLen, offset;
  GET_MATCHES_HEADER(4)

  HASH4_CALC;

  delta2 = p->pos - p->hash[                hash2Value];
  delta3 = p->pos - p->hash[kFix3HashSize + hash3Value];
  curMatch = p->hash[kFix4HashSize + hashValue];

  p->hash[                hash2Value] =
  p->hash[kFix3HashSize + hash3Value] =
  p->hash[kFix4HashSize + hashValue] = p->pos;

  maxLen = 1;
  offset = 0;
  if (delta2 < p->cyclicBufferSize && *(cur - delta2) == *cur)
  {
    distances[0] = maxLen = 2;
    distances[1] = delta2 - 1;
    offset = 2;
  }
  if (delta2 != delta3 && delta3 < p->cyclicBufferSize && *(cur - delta3) == *cur)
  {
    maxLen = 3;
    distances[offset + 1] = delta3 - 1;
    offset += 2;
    delta2 = delta3;
  }
  if (offset != 0)
  {
    for (; maxLen != lenLimit; maxLen++)
      if (cur[(ptrdiff_t)maxLen - delta2] != cur[maxLen])
        break;
    distances[offset - 2] = maxLen;
    if (maxLen == lenLimit)
    {
      SkipMatchesSpec(lenLimit, curMatch, MF_PARAMS(p));
      MOVE_POS_RET;
    }
  }
  if (maxLen < 3)
    maxLen = 3;
  GET_MATCHES_FOOTER(offset, maxLen)
}

static UInt32 Hc4_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances)
{
  UInt32 hash2Value, hash3Value, delta2, delta3, maxLen, offset;
  GET_MATCHES_HEADER(4)

  HASH4_CALC;

  delta2 = p->pos - p->hash[                hash2Value];
  delta3 = p->pos - p->hash[kFix3HashSize + hash3Value];
  curMatch = p->hash[kFix4HashSize + hashValue];

  p->hash[                hash2Value] =
  p->hash[kFix3HashSize + hash3Value] =
  p->hash[kFix4HashSize + hashValue] = p->pos;

  maxLen = 1;
  offset = 0;
  if (delta2 < p->cyclicBufferSize && *(cur - delta2) == *cur)
  {
    distances[0] = maxLen = 2;
    distances[1] = delta2 - 1;
    offset = 2;
  }
  if (delta2 != delta3 && delta3 < p->cyclicBufferSize && *(cur - delta3) == *cur)
  {
    maxLen = 3;
    distances[offset + 1] = delta3 - 1;
    offset += 2;
    delta2 = delta3;
  }
  if (offset != 0)
  {
    for (; maxLen != lenLimit; maxLen++)
      if (cur[(ptrdiff_t)maxLen - delta2] != cur[maxLen])
        break;
    distances[offset - 2] = maxLen;
    if (maxLen == lenLimit)
    {
      p->son[p->cyclicBufferPos] = curMatch;
      MOVE_POS_RET;
    }
  }
  if (maxLen < 3)
    maxLen = 3;
  offset = (UInt32)(Hc_GetMatchesSpec(lenLimit, curMatch, MF_PARAMS(p),
    distances + offset, maxLen) - (distances));
  MOVE_POS_RET
}

static UInt32 Ht4_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances)
{
  UInt32 hash2Value, hash3Value, delta2, delta3, maxLen, offset;
  GET_MATCHES_HEADER(4)  curMatch = curMatch;

  HASH4_CALC;

  delta2 = p->pos - p->hash[                hash2Value];
  delta3 = p->pos - p->hash[kFix3HashSize + hash3Value];

  p->hash[                hash2Value] =
  p->hash[kFix3HashSize + hash3Value] = p->pos;

  maxLen = 1;
  offset = 0;
  if (delta2 < p->cyclicBufferSize && *(cur - delta2) == *cur)
  {
    distances[0] = maxLen = 2;
    distances[1] = delta2 - 1;
    offset = 2;
  }
  if (delta2 != delta3 && delta3 < p->cyclicBufferSize && *(cur - delta3) == *cur)
  {
    maxLen = 3;
    distances[offset + 1] = delta3 - 1;
    offset += 2;
    delta2 = delta3;
  }
  if (offset != 0)
  {
    for (; maxLen != lenLimit; maxLen++)
      if (cur[(ptrdiff_t)maxLen - delta2] != cur[maxLen])
        break;
    distances[offset - 2] = maxLen;
    if (maxLen == lenLimit)
    {
      ////p->son[p->cyclicBufferPos] = curMatch;
      MOVE_POS_RET;
    }
  }
  if (maxLen < 3)
    maxLen = 3;
  UInt32 *curMatchPtr = &(p->hash[kFix4HashSize + hashValue*p->cutValue]);   // First entry in hash table to check
  offset = (UInt32)(Ht_GetMatchesSpec(lenLimit, curMatchPtr, MF_PARAMS(p),
    distances + offset, maxLen) - (distances));
  MOVE_POS_RET
}

UInt32 Hc3Zip_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances)
{
  UInt32 offset;
  GET_MATCHES_HEADER(3)
  HASH_ZIP_CALC;
  curMatch = p->hash[hashValue];
  p->hash[hashValue] = p->pos;
  offset = (UInt32)(Hc_GetMatchesSpec(lenLimit, curMatch, MF_PARAMS(p),
    distances, 2) - (distances));
  MOVE_POS_RET
}

static void Bt2_MatchFinder_Skip(CMatchFinder *p, UInt32 num)
{
  do
  {
    SKIP_HEADER(2)
    HASH2_CALC;
    curMatch = p->hash[hashValue];
    p->hash[hashValue] = p->pos;
    SKIP_FOOTER
  }
  while (--num != 0);
}

void Bt3Zip_MatchFinder_Skip(CMatchFinder *p, UInt32 num)
{
  do
  {
    SKIP_HEADER(3)
    HASH_ZIP_CALC;
    curMatch = p->hash[hashValue];
    p->hash[hashValue] = p->pos;
    SKIP_FOOTER
  }
  while (--num != 0);
}

static void Bt3_MatchFinder_Skip(CMatchFinder *p, UInt32 num)
{
  do
  {
    UInt32 hash2Value;
    SKIP_HEADER(3)
    HASH3_CALC;
    curMatch = p->hash[kFix3HashSize + hashValue];
    p->hash[hash2Value] =
    p->hash[kFix3HashSize + hashValue] = p->pos;
    SKIP_FOOTER
  }
  while (--num != 0);
}

static void Bt4_MatchFinder_Skip(CMatchFinder *p, UInt32 num)
{
  do
  {
    UInt32 hash2Value, hash3Value;
    SKIP_HEADER(4)
    HASH4_CALC;
    curMatch = p->hash[kFix4HashSize + hashValue];
    p->hash[                hash2Value] =
    p->hash[kFix3HashSize + hash3Value] = p->pos;
    p->hash[kFix4HashSize + hashValue] = p->pos;
    SKIP_FOOTER
  }
  while (--num != 0);
}

static void Hc4_MatchFinder_Skip(CMatchFinder *p, UInt32 num)
{
  do
  {
    UInt32 hash2Value, hash3Value;
    SKIP_HEADER(4)
    HASH4_CALC;
    curMatch = p->hash[kFix4HashSize + hashValue];
    p->hash[                hash2Value] =
    p->hash[kFix3HashSize + hash3Value] =
    p->hash[kFix4HashSize + hashValue] = p->pos;
    p->son[p->cyclicBufferPos] = curMatch;
    MOVE_POS
  }
  while (--num != 0);
}

static void Ht4_MatchFinder_Skip(CMatchFinder *p, UInt32 num)
{
  do
  {
    UInt32 hash2Value, hash3Value;
    SKIP_HEADER(4)   curMatch = curMatch;
    HASH4_CALC;
    p->hash[                hash2Value] =
    p->hash[kFix3HashSize + hash3Value] = p->pos;
    {
      UInt32 *curMatchPtr  = &(p->hash[kFix4HashSize + hashValue * p->cutValue]);   // First entry in hash table to check
      UInt32 *lastMatchPtr = curMatchPtr + p->cutValue/2;                           // Last entry+1
      UInt32 prevMatch = saveMatch(p->pos,cur);

      do {
        UInt32 curMatch = *curMatchPtr;   *curMatchPtr++ = prevMatch;   prevMatch = curMatch;
      } while (curMatchPtr < lastMatchPtr);
    }
    MOVE_POS
  }
  while (--num != 0);
}

void Hc3Zip_MatchFinder_Skip(CMatchFinder *p, UInt32 num)
{
  do
  {
    SKIP_HEADER(3)
    HASH_ZIP_CALC;
    curMatch = p->hash[hashValue];
    p->hash[hashValue] = p->pos;
    p->son[p->cyclicBufferPos] = curMatch;
    MOVE_POS
  }
  while (--num != 0);
}

void MatchFinder_CreateVTable(CMatchFinder *p, IMatchFinder *vTable)
{
  vTable->Init = (Mf_Init_Func)MatchFinder_Init;
  vTable->GetIndexByte = (Mf_GetIndexByte_Func)MatchFinder_GetIndexByte;
  vTable->GetNumAvailableBytes = (Mf_GetNumAvailableBytes_Func)MatchFinder_GetNumAvailableBytes;
  vTable->GetPointerToCurrentPos = (Mf_GetPointerToCurrentPos_Func)MatchFinder_GetPointerToCurrentPos;
  if (p->btMode==MF_HashChain)
  {
    vTable->GetMatches = (Mf_GetMatches_Func)Hc4_MatchFinder_GetMatches;
    vTable->Skip = (Mf_Skip_Func)Hc4_MatchFinder_Skip;
  }
  else if (p->btMode==MF_HashTable)
  {
    vTable->GetMatches = (Mf_GetMatches_Func)Ht4_MatchFinder_GetMatches;
    vTable->Skip = (Mf_Skip_Func)Ht4_MatchFinder_Skip;
  }
  else if (p->numHashBytes == 2)
  {
    vTable->GetMatches = (Mf_GetMatches_Func)Bt2_MatchFinder_GetMatches;
    vTable->Skip = (Mf_Skip_Func)Bt2_MatchFinder_Skip;
  }
  else if (p->numHashBytes == 3)
  {
    vTable->GetMatches = (Mf_GetMatches_Func)Bt3_MatchFinder_GetMatches;
    vTable->Skip = (Mf_Skip_Func)Bt3_MatchFinder_Skip;
  }
  else
  {
    vTable->GetMatches = (Mf_GetMatches_Func)Bt4_MatchFinder_GetMatches;
    vTable->Skip = (Mf_Skip_Func)Bt4_MatchFinder_Skip;
  }
}
