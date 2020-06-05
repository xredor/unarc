// BinTreeMain.h

#include "../../../../Common/Defs.h"
#include "../../../../Common/CRC.h"
// #include "../../../../Common/Alloc.h"

#include "BinTree.h"

// #include <xmmintrin.h>
// It's for prefetch
// But prefetch doesn't give big gain in K8.

namespace BT_NAMESPACE {

#ifdef HASH_ARRAY_2
  static const UInt32 kHash2Size = 1 << 10;
  #define kNumHashDirectBytes 0
  #ifdef HASH_ARRAY_3
    static const UInt32 kNumHashBytes = 4;
    static const UInt32 kHash3Size = 1 << 16;
  #else
    static const UInt32 kNumHashBytes = 3;
  #endif
  static const UInt32 kHashSize = 0;
  static const UInt32 kMinMatchCheck = kNumHashBytes;
  static const UInt32 kStartMaxLen = 1;
#else
  #ifdef HASH_ZIP
    #define kNumHashDirectBytes 0
    static const UInt32 kNumHashBytes = 3;
    static const UInt32 kHashSize = 1 << 16;
    static const UInt32 kMinMatchCheck = kNumHashBytes;
    static const UInt32 kStartMaxLen = 1;
  #else
    #define kNumHashDirectBytes 2
    static const UInt32 kNumHashBytes = 2;
    static const UInt32 kHashSize = 1 << (8 * kNumHashBytes);
    static const UInt32 kMinMatchCheck = kNumHashBytes + 1;
    static const UInt32 kStartMaxLen = 1;
  #endif
#endif

#ifdef HASH_ARRAY_2
#ifdef HASH_ARRAY_3
static const UInt32 kHash3Offset = kHash2Size;
#endif
#endif

#ifdef HASH_TABLE
static const bool isHASH_TABLE = TRUE;
#else
static const bool isHASH_TABLE = FALSE;
#endif

static const UInt32 kFixHashSize = 0
    #ifdef HASH_ARRAY_2
    + kHash2Size
    #ifdef HASH_ARRAY_3
    + kHash3Size
    #endif
    #endif
    ;

CMatchFinder::CMatchFinder():
  _hash(0), _son(0)
{
}

void CMatchFinder::FreeThisClassMemory()
{
  BigFree(_hash);
  _hash = 0;
  BigFree(_son);
  _son = 0;
}

void CMatchFinder::FreeMemory()
{
  FreeThisClassMemory();
  CLZInWindow::Free();
}

CMatchFinder::~CMatchFinder()
{
  FreeMemory();
}

inline static UInt32 CalcHashSize (UInt32 dictsize, UInt32 hashSize)
{
  if (hashSize)  return hashSize/sizeof(CIndex);  // hashSize was set explicitly

  #ifdef HASH_TABLE
    return roundup_to_power_of (dictsize/2, 2) / sizeof(CIndex);
  //return roundup_to_power_of (min(dictsize,1<<28)*4, 2) / sizeof(CIndex);
  #else
    #ifndef HASH_ARRAY_2
    return kHashSize + dictsize*0;
    #else
    UInt32 hs = rounddown_to_power_of (dictsize/3*4, 2);
    return hs<=64*kb?  64*kb :
           hs<=32*mb?  hs/2  :
          #ifndef HASH_ARRAY_3
                       16*mb ;
          #else
                       hs/4  ;
          #endif
    #endif
  #endif
}

// Size of sliding area in input buffer
uint ReservedAreaSize (uint dict)
{
  if (compress_all_at_once)    return 4096;  //+4096 is just for case
//  else if (dict>=kMaxValForNormalize-64*mb)    return 64*mb;
  else                         return dict / (isHASH_TABLE?4:2);
}


STDMETHODIMP CMatchFinder::Create(UInt32 historySize, UInt32 hashSize, UInt32 keepAddBufferBefore,
    UInt32 matchMaxLen, UInt32 keepAddBufferAfter)
{
  if (historySize > kMaxValForNormalize - 256)
  {
    if (historySize-1 <= kMaxValForNormalize) {  // historySize~=kMaxValForNormalize -> just make hs a bit smaller than kMax
      historySize = kMaxValForNormalize - 256;
    } else {
      FreeMemory();
      return E_INVALIDARG;
    }
  }

  #ifdef _HASH_CHAIN
    SetNumPasses (8 + (matchMaxLen >> 2));
  #else
    SetNumPasses (16 + (matchMaxLen >> 1));
  #endif
  {
    _matchMaxLen = matchMaxLen;
    UInt32 newCyclicBufferSize = historySize + 1;
    if (_hash != 0 && newCyclicBufferSize == _cyclicBufferSize)
      goto lz;
    FreeThisClassMemory();
    _cyclicBufferSize = newCyclicBufferSize; // don't change it

    UInt32 hs = CalcHashSize (historySize, hashSize);
    _hashMask = hs-1;
    _hashSizeSum = hs + kFixHashSize;

  #ifdef HASH_TABLE
    _hash = (CIndex *)BigAlloc(_hashSizeSum * sizeof(CIndex));
    if (_hash == 0)
      goto err;
  #else
    UInt32 numItems = _cyclicBufferSize
    #ifndef _HASH_CHAIN
     * 2
    #endif
    ;
    _son  = (CIndex *)BigAlloc(numItems     * sizeof(CIndex));
    _hash = (CIndex *)BigAlloc(_hashSizeSum * sizeof(CIndex));
    if (_hash == 0 || _son==0)
      goto err;
  #endif
  }
lz:
  {
    UInt32 sizeReserv = ReservedAreaSize(historySize + keepAddBufferBefore +
        matchMaxLen + keepAddBufferAfter) + 256;
    if (CLZInWindow::Create(historySize + keepAddBufferBefore,
        matchMaxLen + keepAddBufferAfter, sizeReserv))
    {
        return S_OK;
    }
  }
err:
  FreeMemory();
  return E_OUTOFMEMORY;
}

static const UInt32 kEmptyHashValue = 0;

STDMETHODIMP CMatchFinder::SetStream(ISequentialInStream *stream)
{
  CLZInWindow::SetStream(stream);
  return S_OK;
}

STDMETHODIMP CMatchFinder::Init()
{
  RINOK(CLZInWindow::Init());
  for(UInt32 i = 0; i < _hashSizeSum; i++)
    _hash[i] = kEmptyHashValue;
  _cyclicBufferPos = 0;
  ReduceOffsets(-1);
  return S_OK;
}

STDMETHODIMP_(void) CMatchFinder::ReleaseStream()
{
  // ReleaseStream();
}

#ifdef HASH_ARRAY_2
#ifdef HASH_ARRAY_3

#define HASH_CALC { \
  UInt32 temp = CCRC::Table[cur[0]] ^ cur[1]; \
  hash2Value = temp & (kHash2Size - 1); \
  hash3Value = (temp ^ (UInt32(cur[2]) << 8)) & (kHash3Size - 1); \
  hashValue = ((temp ^ (UInt32(cur[2]) << 8) ^ (CCRC::Table[cur[3]] << 5)) << hashShift) & _hashMask; }

#else // no HASH_ARRAY_3
#define HASH_CALC { \
  UInt32 temp = CCRC::Table[cur[0]] ^ cur[1]; \
  hash2Value = temp & (kHash2Size - 1); \
  hashValue = (temp ^ (UInt32(cur[2]) << 8)) & _hashMask; }
#endif // HASH_ARRAY_3
#else // no HASH_ARRAY_2
#ifdef HASH_ZIP
inline UInt32 Hash(const Byte *pointer)
{
  return ((UInt32(pointer[0]) << 8) ^ CCRC::Table[pointer[1]] ^ pointer[2]) & (kHashSize - 1);
}
#else // no HASH_ZIP
inline UInt32 Hash(const Byte *pointer)
{
  return pointer[0] ^ (UInt32(pointer[1]) << 8);
}
#endif // HASH_ZIP
#endif // HASH_ARRAY_2

#ifdef HASH_TABLE
#define getMatch(x) ((x)%(1<<30))
#else
#define getMatch(x) (x)
#endif


STDMETHODIMP CMatchFinder::GetMatches(UInt32 *distances)
{
  UInt32 lenLimit;
  if (_pos + _matchMaxLen <= _streamPos)
    lenLimit = _matchMaxLen;
  else
  {
    lenLimit = _streamPos - _pos;
    if(lenLimit < kMinMatchCheck)
    {
      distances[0] = 0;
      return MovePos();
    }
  }

  int offset = 1;

  UInt32 matchMinPos = (_pos > _cyclicBufferSize) ? (_pos - _cyclicBufferSize) : 0;
  const Byte *cur = _buffer + _pos;

  UInt32 maxLen = kStartMaxLen; // to avoid items for len < hashSize;

  #ifdef HASH_ARRAY_2
  UInt32 hash2Value;
  #ifdef HASH_ARRAY_3
  UInt32 hash3Value,  hashShift = isHASH_TABLE? _hashShift : 0;
  #endif
  UInt32 hashValue;
  HASH_CALC;
  #else
  UInt32 hashValue = Hash(cur);
  #endif

  #ifdef HASH_ARRAY_2
  UInt32 curMatch2 = _hash[hash2Value];
  #ifdef HASH_ARRAY_3
  UInt32 curMatch3 = _hash[kHash3Offset + hash3Value];
  #endif
  _hash[hash2Value] = _pos;
  if(curMatch2 > matchMinPos)
    if (_buffer[curMatch2] == cur[0])
    {
      distances[offset++] = maxLen = 2;
      distances[offset++] = _pos - curMatch2 - 1;
    }

  #ifdef HASH_ARRAY_3
  _hash[kHash3Offset + hash3Value] = _pos;
  if(curMatch3 > matchMinPos)
    if (_buffer[curMatch3] == cur[0])
    {
      if (curMatch3 == curMatch2)
        offset -= 2;
      distances[offset++] = maxLen = 3;
      distances[offset++] = _pos - curMatch3 - 1;
      curMatch2 = curMatch3;
    }
  #endif
  if (offset != 1 && curMatch2 == getMatch(_hash[kFixHashSize + hashValue]))  // curMatch2 == curMatch
  {
    offset -= 2;
    maxLen = kStartMaxLen;
  }
  #endif


#ifdef HASH_TABLE
  UInt32 *curMatchPtr  = &_hash[kFixHashSize + hashValue];   // First entry in hash table to check
  UInt32 *lastMatchPtr = curMatchPtr + _cutValue;            // Last entry to check + 1
  UInt32 prevMatch = _pos + (UInt32(cur[0]) << 30);

  do {
    UInt32 curMatch = *curMatchPtr;   *curMatchPtr++ = prevMatch;   prevMatch = curMatch;
    if (getMatch(curMatch) <= matchMinPos)  break;
    if ((curMatch>>30) != (cur[0]&3))   continue;   // Check tag stored in 2 higher bits of hash entry
    curMatch %= 1<<30;
    UInt32 delta = _pos - curMatch;
    const Byte *pb = _buffer + curMatch;
    UInt32 len = kNumHashDirectBytes;
    if (pb[maxLen] == cur[maxLen])
    {
      while (pb[len] == cur[len]  &&  ++len != lenLimit);
      if (len > maxLen)
      {
        distances[offset++] = maxLen = len;
        distances[offset++] = delta - 1;
        if (len >= lenLimit)  break;
      }
    }
  } while (curMatchPtr < lastMatchPtr);

#else
  UInt32 curMatch = _hash[kFixHashSize + hashValue];
  _hash[kFixHashSize + hashValue] = _pos;

  CIndex *son = _son;

  #ifdef _HASH_CHAIN
  son[_cyclicBufferPos] = curMatch;
  #else
  CIndex *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  CIndex *ptr1 = son + (_cyclicBufferPos << 1);

  UInt32 len0, len1;
  len0 = len1 = kNumHashDirectBytes;
  #endif

  #if kNumHashDirectBytes != 0
  if(curMatch > matchMinPos)
  {
    if (_buffer[curMatch + kNumHashDirectBytes] != cur[kNumHashDirectBytes])
    {
      distances[offset++] = maxLen = kNumHashDirectBytes;
      distances[offset++] = _pos - curMatch - 1;
    }
  }
  #endif
  UInt32 count = _cutValue;
  while(true)
  {
    if(curMatch <= matchMinPos || count-- == 0)
    {
      #ifndef _HASH_CHAIN
      *ptr0 = *ptr1 = kEmptyHashValue;
      #endif
      break;
    }
    UInt32 delta = _pos - curMatch;
    UInt32 cyclicPos = (delta <= _cyclicBufferPos) ?
        (_cyclicBufferPos - delta):
        (_cyclicBufferPos - delta + _cyclicBufferSize);
    CIndex *pair = son +
    #ifdef _HASH_CHAIN
      cyclicPos;
    #else
      (cyclicPos << 1);
    #endif

    // _mm_prefetch((const char *)pair, _MM_HINT_T0);

    const Byte *pb = _buffer + curMatch;
    UInt32 len =
    #ifdef _HASH_CHAIN
    kNumHashDirectBytes;
    if (pb[maxLen] == cur[maxLen])
    #else
    MyMin(len0, len1);
    #endif
    if (pb[len] == cur[len])
    {
      while(++len != lenLimit)
        if (pb[len] != cur[len])
          break;
      if (maxLen < len)
      {
        distances[offset++] = maxLen = len;
        distances[offset++] = delta - 1;
        if (len == lenLimit)
        {
          #ifndef _HASH_CHAIN
          *ptr1 = pair[0];
          *ptr0 = pair[1];
          #endif
          break;
        }
      }
    }
    #ifdef _HASH_CHAIN
    curMatch = *pair;
    #else
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
    #endif
  }
#endif  // HASH_TABLE

  distances[0] = offset - 1;
  if (++_cyclicBufferPos == _cyclicBufferSize)
    _cyclicBufferPos = 0;
  RINOK(CLZInWindow::MovePos());
  if (_pos == kMaxValForNormalize)
    Normalize();
  return S_OK;
}

STDMETHODIMP CMatchFinder::Skip(UInt32 num)
{
  do
  {
  #ifdef _HASH_CHAIN
  if (_streamPos - _pos < kNumHashBytes)
  {
    RINOK(MovePos());
    continue;
  }
  #else
  UInt32 lenLimit;
  if (_pos + _matchMaxLen <= _streamPos)
    lenLimit = _matchMaxLen;
  else
  {
    lenLimit = _streamPos - _pos;
    if(lenLimit < kMinMatchCheck)
    {
      RINOK(MovePos());
      continue;
    }
  }
  UInt32 matchMinPos = (_pos > _cyclicBufferSize) ? (_pos - _cyclicBufferSize) : 0;
  #endif
  const Byte *cur = _buffer + _pos;

  #ifdef HASH_ARRAY_2
  UInt32 hash2Value;
  #ifdef HASH_ARRAY_3
  UInt32 hash3Value,  hashShift = isHASH_TABLE? _hashShift : 0;
  UInt32 hashValue;
  HASH_CALC;
  _hash[kHash3Offset + hash3Value] = _pos;
  #else
  UInt32 hashValue;
  HASH_CALC;
  #endif
  _hash[hash2Value] = _pos;
  #else
  UInt32 hashValue = Hash(cur);
  #endif

  #ifdef HASH_TABLE

  UInt32 *curMatchPtr  = &_hash[kFixHashSize + hashValue];   // First entry in hash table to check
  UInt32 *lastMatchPtr = curMatchPtr + _cutValue/2;          // Last entry+1
  UInt32 prevMatch = _pos + (UInt32(cur[0]) << 30);

  do {
    UInt32 curMatch = *curMatchPtr;   *curMatchPtr++ = prevMatch;   prevMatch = curMatch;
  } while (curMatchPtr < lastMatchPtr);

  #elif defined(_HASH_CHAIN)

  UInt32 curMatch = _hash[kFixHashSize + hashValue];
  _hash[kFixHashSize + hashValue] = _pos;
  _son[_cyclicBufferPos] = curMatch;

  #else

  UInt32 curMatch = _hash[kFixHashSize + hashValue];
  _hash[kFixHashSize + hashValue] = _pos;

  CIndex *son = _son;
  CIndex *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  CIndex *ptr1 = son + (_cyclicBufferPos << 1);

  UInt32 len0, len1;
  len0 = len1 = kNumHashDirectBytes;
  UInt32 count = _cutValue;
  while(true)
  {
    if(curMatch <= matchMinPos || count-- == 0)
    {
      *ptr0 = *ptr1 = kEmptyHashValue;
      break;
    }

    UInt32 delta = _pos - curMatch;
    UInt32 cyclicPos = (delta <= _cyclicBufferPos) ?
      (_cyclicBufferPos - delta):
      (_cyclicBufferPos - delta + _cyclicBufferSize);
    CIndex *pair = son + (cyclicPos << 1);

    // _mm_prefetch((const char *)pair, _MM_HINT_T0);

    const Byte *pb = _buffer + curMatch;
    UInt32 len = MyMin(len0, len1);

    if (pb[len] == cur[len])
    {
      while(++len != lenLimit)
        if (pb[len] != cur[len])
          break;
      if (len == lenLimit)
      {
        *ptr1 = pair[0];
        *ptr0 = pair[1];
        break;
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
  #endif
  if (++_cyclicBufferPos == _cyclicBufferSize)
    _cyclicBufferPos = 0;
  RINOK(CLZInWindow::MovePos());
  if (_pos == kMaxValForNormalize)
    Normalize();
  }
  while(--num != 0);
  return S_OK;
}

void CMatchFinder::Normalize()
{
  // Сдвинуть назад виртуальные позиции. Минимум на 128 мб (актуально для словаря в 1гб, поскольку kMaxValForNormalize=1гб)
  UInt32 subValue = mymax (_pos - _cyclicBufferSize, 128<<20);
  {
    CIndex *items = _hash;
    UInt32 numItems = _hashSizeSum;
    for (UInt32 i = 0; i < numItems; i++)
    {
      UInt32 value = items[i];
      if (getMatch(value) <= subValue)
        value = kEmptyHashValue;
      else
        value -= subValue;
      items[i] = value;
    }
  }

  if (_son)
  {
    CIndex *items = _son;
    UInt32 numItems = _cyclicBufferSize
      #ifndef _HASH_CHAIN
       * 2
      #endif
      ;
    for (UInt32 i = 0; i < numItems; i++)
    {
      UInt32 value = items[i];
      if (value <= subValue)
        value = kEmptyHashValue;
      else
        value -= subValue;
      items[i] = value;
    }
  }
  ReduceOffsets(subValue);
}

HRESULT CMatchFinder::MovePos()
{
  if (++_cyclicBufferPos == _cyclicBufferSize)
    _cyclicBufferPos = 0;
  RINOK(CLZInWindow::MovePos());
  if (_pos == kMaxValForNormalize)
    Normalize();
  return S_OK;
}

STDMETHODIMP_(Byte) CMatchFinder::GetIndexByte(Int32 index)
  { return CLZInWindow::GetIndexByte(index); }

STDMETHODIMP_(UInt32) CMatchFinder::GetMatchLen(Int32 index,
    UInt32 back, UInt32 limit)
  { return CLZInWindow::GetMatchLen(index, back, limit); }

STDMETHODIMP_(UInt32) CMatchFinder::GetNumAvailableBytes()
  { return CLZInWindow::GetNumAvailableBytes(); }

STDMETHODIMP_(const Byte *) CMatchFinder::GetPointerToCurrentPos()
  { return CLZInWindow::GetPointerToCurrentPos(); }

STDMETHODIMP_(Int32) CMatchFinder::NeedChangeBufferPos(UInt32 numCheckBytes)
  { return CLZInWindow::NeedMove(numCheckBytes) ? 1: 0; }

STDMETHODIMP_(void) CMatchFinder::ChangeBufferPos()
  { CLZInWindow::MoveBlock();}

#undef HASH_CALC
#undef kNumHashDirectBytes
#undef getMatch

}
