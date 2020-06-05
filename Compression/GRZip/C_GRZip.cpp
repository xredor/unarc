/*-------------------------------------------------*/
/* GRZipII/libGRZip compressor          libGRZip.c */
/* libGRZip Compression(Decompression) Functions   */
/*-------------------------------------------------*/

/*--
  This file is a part of GRZipII and/or libGRZip, a program
  and library for lossless, block-sorting data compression.

  Copyright (C) 2002-2004 Grebnov Ilya. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  Grebnov Ilya, Ivanovo, Russian Federation.
  Ilya.Grebnov@magicssoft.ru, http://magicssoft.ru/

  This program is based on (at least) the work of:
  Juergen Abel, Jon L. Bentley, Edgar Binder,
  Charles Bloom, Mike Burrows, Andrey Cadach,
  Damien Debin, Sebastian Deorowicz, Peter Fenwick,
  George Plechanov, Michael Schindler, Robert Sedgewick,
  Julian Seward, David Wheeler, Vadim Yoockin.

  Normal compression mode:
    Compression     memory use : [7-9]*BlockLen  + 1Mb
    Decompression   memory use : 5*BlockLen      + 1Mb
  Fast compression mode:
    Compression     memory use : 5*BlockLen      + 1Mb
    Decompression   memory use : 5.125*BlockLen  + 1Mb

  For more information on these sources, see the manual.
--*/

#include <math.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "C_GRZip.h"
}
#include "../MultiThreading.h"
#include "libGRZip.h"
#include "LZP.c"
#include "BWT.c"
#include "ST4.c"
#include "MTF_Ari.c"
#include "WFC_Ari.c"
#include "Rec_Flt.c"

const sint32 RESERVED = 0;  // неиспользуемые байты в заголовке заполняются этим значением

#ifndef FREEARC_DECOMPRESS_ONLY

sint32 GRZip_StoreBlock(uint8 * Input ,sint32 Size,
                        uint8 * Output,sint32 Mode)
{
  *(sint32 *)(Output+4)=-1;
  *(sint32 *)(Output+8)=DisableAllButLZP(Mode);
  *(sint32 *)(Output+12)=0;
  *(sint32 *)(Output+16)=Size;
  memcpy(Output+28,Input,Size);
  *(sint32 *)(Output+20)=RESERVED;
  *(sint32 *)(Output+24)=RESERVED;
  return (Size+28);
}

sint32 GRZip_CompressBlock(uint8 * Input ,sint32 Size,
                           uint8 * Output,sint32 Mode)
{
  sint32 SSize=Size;

  *(sint32 *)Output=Size;

  if ((Size<32)||(Size>GRZ_MaxBlockSize))
    return(GRZip_StoreBlock(Input,Size,Output,0));

  if (Size<1024) Mode|=GRZ_Compression_ST4;

  if ((Size>1024)&&((Mode&GRZ_Disable_DeltaFlt)==0))
  {
    sint32 RecMode=GRZip_Rec_Test(Input,Size);
    if (RecMode)
    {
      sint32 NewSize;
      uint8 * Buffer=(uint8 *)BigAlloc(Size+LZP_MaxMatchLen);
      if (Buffer==NULL) return(GRZip_StoreBlock(Input,Size,Output,0));
      GRZip_Rec_Encode(Input,Size,Buffer,RecMode); Mode+=GRZ_Disable_DeltaFlt;
      if ((RecMode&1)==1)
      {
        sint32 PartSize=(Size>>1);
        sint32 Result=GRZip_CompressBlock(Buffer,PartSize,Output+28,Mode);
        if (Result<0) {BigFree(Buffer);return(GRZip_StoreBlock(Input,Size,Output,0));}
        NewSize=Result;
        Result=GRZip_CompressBlock(Buffer+PartSize,Size-PartSize,Output+28+NewSize,Mode);
        if (Result<0) {BigFree(Buffer);return(GRZip_StoreBlock(Input,Size,Output,0));}
        NewSize+=Result;
      }
      if ((RecMode&1)==0)
      {
        sint32 PartSize=(Size>>2);
        sint32 Result=GRZip_CompressBlock(Buffer,PartSize,Output+28,Mode);
        if (Result<0) {BigFree(Buffer);return(GRZip_StoreBlock(Input,Size,Output,0));}
        NewSize=Result;
        Result=GRZip_CompressBlock(Buffer+PartSize,PartSize,Output+28+NewSize,Mode);
        if (Result<0) {BigFree(Buffer);return(GRZip_StoreBlock(Input,Size,Output,0));}
        NewSize+=Result;
        Result=GRZip_CompressBlock(Buffer+2*PartSize,PartSize,Output+28+NewSize,Mode);
        if (Result<0) {BigFree(Buffer);return(GRZip_StoreBlock(Input,Size,Output,0));}
        NewSize+=Result;
        Result=GRZip_CompressBlock(Buffer+3*PartSize,Size-3*PartSize,Output+28+NewSize,Mode);
        if (Result<0) {BigFree(Buffer);return(GRZip_StoreBlock(Input,Size,Output,0));}
        NewSize+=Result;
      }
      BigFree(Buffer);

      if (NewSize>=Size) return(GRZip_StoreBlock(Input,Size,Output,0));

      *(sint32 *)(Output+4)=-2;
      *(sint32 *)(Output+8)=RecMode;
      *(sint32 *)(Output+16)=NewSize;
      *(sint32 *)(Output+20)=RESERVED;
      *(sint32 *)(Output+24)=RESERVED;

      return (NewSize+28);
    }
  }

  uint8 * LZPBuffer=(uint8 *)BigAlloc(Size+LZP_MaxMatchLen);
  if (LZPBuffer==NULL) return(GRZip_StoreBlock(Input,Size,Output,0));

  if (LZP_Enabled(Mode))
  {
    sint32 Result=GRZip_LZP_Encode(Input,Size,LZPBuffer,Get_LZP_MinMatchLen(Mode),Get_LZP_HT_Size(Mode));
    if (Result==GRZ_NOT_ENOUGH_MEMORY)
    {
      BigFree(LZPBuffer);
      return(GRZip_StoreBlock(Input,Size,Output,0));
    };
    if (Result==GRZ_NOT_COMPRESSIBLE)
    {
      Mode=Disable_LZP(Mode);
      memcpy(LZPBuffer,Input,Size);
      *(sint32 *)(Output+8)=Size;
    }
    else
     { *(sint32 *)(Output+8)=Result,Size=Result;}
  }
  else
  {
    memcpy(LZPBuffer,Input,Size);
    *(sint32 *)(Output+8)=Size;
  }
  sint32 Result;

  for (Result=0;Result<8;Result++) LZPBuffer[Result+Size]=0;
  Size=(Size+7)&(~7);

  if (Mode&GRZ_Compression_ST4)
    Result=GRZip_ST4_Encode(LZPBuffer,Size,LZPBuffer);
  else
    Result=GRZip_BWT_Encode(LZPBuffer,Size,LZPBuffer,Mode&GRZ_BWTSorting_Fast);

  if (Result==GRZ_NOT_ENOUGH_MEMORY)
  {
    if (LZP_Enabled(Mode))
    {
      sint32 Result=GRZip_LZP_Encode(Input,SSize,LZPBuffer,Get_LZP_MinMatchLen(Mode),Get_LZP_HT_Size(Mode));
      if (Result==GRZ_NOT_ENOUGH_MEMORY)
      {
        BigFree(LZPBuffer);
        return(GRZip_StoreBlock(Input,SSize,Output,0));
      };
      Result=GRZip_StoreBlock(LZPBuffer,Result,Output,Mode);
      BigFree(LZPBuffer);
      return (Result);
    }
    BigFree(LZPBuffer);
    return(GRZip_StoreBlock(Input,SSize,Output,0));
  };

  *(sint32 *)(Output+12)=Result;

  if (Mode&GRZ_Compression_MTF)
    Result=GRZip_MTF_Ari_Encode(LZPBuffer,Size,Output+28);
  else
    Result=GRZip_WFC_Ari_Encode(LZPBuffer,Size,Output+28);

  if ((Result==GRZ_NOT_ENOUGH_MEMORY)||(Result==GRZ_NOT_COMPRESSIBLE))
  {
    if (LZP_Enabled(Mode))
    {
      sint32 Result=GRZip_LZP_Encode(Input,SSize,LZPBuffer,Get_LZP_MinMatchLen(Mode),Get_LZP_HT_Size(Mode));
      if (Result==GRZ_NOT_ENOUGH_MEMORY)
      {
        BigFree(LZPBuffer);
        return(GRZip_StoreBlock(Input,SSize,Output,0));
      };
      Result=GRZip_StoreBlock(LZPBuffer,Result,Output,Mode);
      BigFree(LZPBuffer);
      return (Result);
    }
    BigFree(LZPBuffer);
    return(GRZip_StoreBlock(Input,SSize,Output,0));
  };

  *(sint32 *)(Output+4)=Mode;
  *(sint32 *)(Output+16)=Result;
  *(sint32 *)(Output+20)=RESERVED;
  *(sint32 *)(Output+24)=RESERVED;

  BigFree(LZPBuffer);
  return (Result+28);
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

sint32 GRZip_CheckBlockSign(uint8 * Input,sint32 Size)
{
  if (Size<28) return (GRZ_UNEXPECTED_EOF);
  if ((*(sint32 *)(Input+24))!=RESERVED)
    return (GRZ_CRC_ERROR);
  return (GRZ_NO_ERROR);
}

sint32 GRZip_DecompressBlock(uint8 * Input,sint32 Size,uint8 * Output)
{
  if (Size<28) return (GRZ_UNEXPECTED_EOF);
  if ((*(sint32 *)(Input+24))!=RESERVED)
    return (GRZ_CRC_ERROR);
  if ((*(sint32 *)(Input+16))+28>Size) return (GRZ_UNEXPECTED_EOF);
  if ((*(sint32 *)(Input+20))!=RESERVED)
    return (GRZ_CRC_ERROR);
  sint32 Mode=*(sint32 *)(Input+4);
  sint32 Result=*(sint32 *)(Input+16);
  if (Mode==-1)
  {
    Mode=*(sint32 *)(Input+8);
    if (Mode==0)
    {
      memcpy(Output,Input+28,Result);
      return (Result);
    }
    Result=GRZip_LZP_Decode(Input+28,Result,Output,Get_LZP_MinMatchLen(Mode),Get_LZP_HT_Size(Mode));
    return (Result);
  }

  if (Mode==-2)
  {
    sint32 RecMode=*(sint32 *)(Input+8);
              Size=*(sint32 *)(Input);

    uint8 * Buffer=(uint8 *)BigAlloc(Size+LZP_MaxMatchLen);
    if (Buffer==NULL) return(GRZ_NOT_ENOUGH_MEMORY);

    uint8 * Tmp=(Input+28);
    sint32  OutputPos=0;

    if ((RecMode&1)==1)
    {
      Result=GRZip_DecompressBlock(Tmp,(*(sint32 *)(Tmp+16))+28,Buffer+OutputPos);
      if (Result<0) {BigFree(Buffer);return Result;};
      OutputPos+=Result;
      Tmp=Tmp+(*(sint32 *)(Tmp+16))+28;
      Result=GRZip_DecompressBlock(Tmp,(*(sint32 *)(Tmp+16))+28,Buffer+OutputPos);
      if (Result<0) {BigFree(Buffer);return Result;};
    }
    if ((RecMode&1)==0)
    {
      Result=GRZip_DecompressBlock(Tmp,(*(sint32 *)(Tmp+16))+28,Buffer+OutputPos);
      if (Result<0) {BigFree(Buffer);return Result;};
      OutputPos+=Result;
      Tmp=Tmp+(*(sint32 *)(Tmp+16))+28;
      Result=GRZip_DecompressBlock(Tmp,(*(sint32 *)(Tmp+16))+28,Buffer+OutputPos);
      if (Result<0) {BigFree(Buffer);return Result;};
      OutputPos+=Result;
      Tmp=Tmp+(*(sint32 *)(Tmp+16))+28;
      Result=GRZip_DecompressBlock(Tmp,(*(sint32 *)(Tmp+16))+28,Buffer+OutputPos);
      if (Result<0) {BigFree(Buffer);return Result;};
      OutputPos+=Result;
      Tmp=Tmp+(*(sint32 *)(Tmp+16))+28;
      Result=GRZip_DecompressBlock(Tmp,(*(sint32 *)(Tmp+16))+28,Buffer+OutputPos);
      if (Result<0) {BigFree(Buffer);return Result;};
    }
    GRZip_Rec_Decode(Buffer,Size,Output,RecMode);
    BigFree(Buffer);
    return (Size);
  }

  uint8 * LZPBuffer=(uint8 *)BigAlloc(*(sint32 *)(Input+8)+LZP_MaxMatchLen);
  if (LZPBuffer==NULL) return(GRZ_NOT_ENOUGH_MEMORY);

  sint32 TSize;

  if (Mode&GRZ_Compression_MTF)
    TSize=GRZip_MTF_Ari_Decode(Input+28,LZPBuffer);
  else
    TSize=GRZip_WFC_Ari_Decode(Input+28,(*(sint32 *)(Input+8)),LZPBuffer);

  if (Result==GRZ_NOT_ENOUGH_MEMORY)
  {
    BigFree(LZPBuffer);
    return(GRZ_NOT_ENOUGH_MEMORY);
  };

  Result=*(sint32 *)(Input+12);

  if (Mode&GRZ_Compression_ST4)
    Result=GRZip_ST4_Decode(LZPBuffer,TSize,Result);
  else
    Result=GRZip_BWT_Decode(LZPBuffer,TSize,Result);

  if (Result==GRZ_NOT_ENOUGH_MEMORY)
  {
    BigFree(LZPBuffer);
    return(GRZ_NOT_ENOUGH_MEMORY);
  };

  TSize=*(sint32 *)(Input+8);

  if (LZP_Enabled(Mode))
  {
    sint32 Result=GRZip_LZP_Decode(LZPBuffer,TSize,Output,Get_LZP_MinMatchLen(Mode),Get_LZP_HT_Size(Mode));
    if (Result==GRZ_NOT_ENOUGH_MEMORY)
    {
      BigFree(LZPBuffer);
      return(GRZ_NOT_ENOUGH_MEMORY);
    };
  }
  else
    memcpy(Output,LZPBuffer,TSize);

  BigFree(LZPBuffer);
  return (*(sint32 *)Input);
}

#ifndef FREEARC_DECOMPRESS_ONLY

#define ABS_MaxByte      256
#define ABS_MinBlockSize 24*1024

sint32 GRZip_GetAdaptiveBlockSize(uint8 * Input,sint32 Size)
{
  sint32  TotFreq[ABS_MaxByte];
  sint32     Freq[ABS_MaxByte];

  if (Size<=ABS_MinBlockSize) return Size;

  memset(TotFreq,0,ABS_MaxByte*sizeof(sint32));

  uint8 * SInput=Input;
  uint8 * InputEnd=Input+ABS_MinBlockSize;
  while  (Input<InputEnd) TotFreq[*Input++]++;

  sint32 Pos=ABS_MinBlockSize,BlockSize=ABS_MinBlockSize/2;

  while (Pos+BlockSize<Size)
  {
    memset(Freq,0,ABS_MaxByte*sizeof(sint32));

    sint32 i=0,Sum=BlockSize+(Pos>>1);

    uint8 * Ptr=SInput+Pos;
    uint8 * PtrEnd=Ptr+BlockSize;
    while (Ptr<PtrEnd) Freq[*Ptr++]++;

    double AvgSize=0,RealSize=0;
    for (i=0;i<ABS_MaxByte;i++)
      if (Freq[i])
      {
        sint32 Fr=Freq[i];
        RealSize-=Fr*log10((double)Fr/BlockSize);
        AvgSize-=Fr*log10((double)(Fr+(TotFreq[i]>>1))/Sum);
      }

    if (AvgSize>1.25*RealSize)
       if (BlockSize<256)
         return Pos;
       else
         {BlockSize>>=1;continue;}

    for (i=0;i<ABS_MaxByte;i++) TotFreq[i]+=Freq[i];
    Pos+=BlockSize;
  }
  return Size;
}

#undef ABS_MaxByte
#undef ABS_MinBlockSize


/*-------------------------------------------------*/
/* Multithreaded grzip_compress/grzip_decompress   */
/*-------------------------------------------------*/
struct GRZipMTCompressor;

// Single GRZip compression thread
struct GRZipCompressionThread : WorkerThread
{
    GRZipMTCompressor* compressor;
    int init();
    int process();
    int done();
};

// Multi-threaded GRZip compressor
struct GRZipMTCompressor : MTCompressor<GRZipCompressionThread>
{
    sint32  Mode;
    int     BlockSize;
    int     AdaptiveBlockSize;       // использовать переменный размер блока

    GRZipMTCompressor (int Method,
                       int BlockSize,
                       int EnableLZP,
                       int MinMatchLen,
                       int HashSizeLog,
                       int AlternativeBWTSort,
                       int AdaptiveBlockSize,
                       int DeltaFilter,
                       CALLBACK_FUNC *callback,
                       void *auxdata)
    {
        switch (Method)
        {
            case 1:  Mode = GRZ_Compression_BWT + GRZ_Compression_WFC; break;
            case 2:  Mode = GRZ_Compression_BWT + GRZ_Compression_MTF; break;
            case 3:  Mode = GRZ_Compression_ST4 + GRZ_Compression_WFC; break;
            case 4:  Mode = GRZ_Compression_ST4 + GRZ_Compression_MTF; break;
            default: SetErrCode (FREEARC_ERRCODE_INVALID_COMPRESSOR);        ////
        }
        Mode += EnableLZP? Encode_LZP_HT_Size(HashSizeLog)+Encode_LZP_MinMatchLen(MinMatchLen) : GRZ_Disable_LZP;
        Mode += AlternativeBWTSort? GRZ_BWTSorting_Strong : GRZ_BWTSorting_Fast;
        Mode += DeltaFilter? GRZ_Enable_DeltaFlt : GRZ_Disable_DeltaFlt;
        this->AdaptiveBlockSize = AdaptiveBlockSize;
        this->BlockSize = mymin (BlockSize, GRZ_MaxBlockSize);
        this->callback  = callback;
        this->auxdata   = auxdata;
    }

    int main_cycle()
    {
        GRZipCompressionThread *job = FreeJobs.Get();   // Acquire first compression job
        char* RemainderPos; int RemainderSize=0;        // остаток данных с предыдущего раза - адрес и количество
        while ( (job->InSize = callback ("read", job->InBuf + RemainderSize, BlockSize - RemainderSize, auxdata)) >= 0 )
        {
          if ((job->InSize+=RemainderSize)==0)     return 0;  // Данных больше нет
          if (errcode < 0)                         return 0;  // Error in other thread
          RemainderSize=0;
          if (AdaptiveBlockSize)
          {  // Пошукаем статистику прочитанных данных - может, нет смысла сжимать их общим блоком
             sint32 NewSize = GRZip_GetAdaptiveBlockSize ((uint8*) job->InBuf, job->InSize);
             // Принято решение сжать только первые NewSize байт. Остальное оставим на следующий раз
             RemainderPos=job->InBuf+NewSize; RemainderSize=job->InSize-NewSize; job->InSize=NewSize;
          }
          WriterJobs.Put(job);
          job->StartOperation.Signal();
          job = FreeJobs.Get();                     // Acquire next compression job
          // Перенесём необработанный остаток данных в начало буфера
          if (RemainderSize>0)   memmove(job->InBuf, RemainderPos, RemainderSize);
        }
        return job->InSize;
    }
};

int GRZipCompressionThread::init()                   // Alloc resources
{
    compressor = (GRZipMTCompressor*) task;
    InBuf   = (char*) BigAlloc (compressor->BlockSize + LZP_MaxMatchLen);
    OutBuf  = (char*) BigAlloc (compressor->BlockSize + LZP_MaxMatchLen);
    return (InBuf && OutBuf? 0 : FREEARC_ERRCODE_NOT_ENOUGH_MEMORY);
}

int GRZipCompressionThread::process()                // Perform one compression operation
{
    int res = GRZip_CompressBlock ((uint8*)InBuf, InSize, (uint8*)OutBuf, compressor->Mode);
    return (res == GRZ_NOT_ENOUGH_MEMORY? FREEARC_ERRCODE_NOT_ENOUGH_MEMORY :
            res <  0?                     FREEARC_ERRCODE_GENERAL :
                                          res);
}

int GRZipCompressionThread::done()                   // Free resources
{
    BigFree(OutBuf);  OutBuf = NULL;
    BigFree(InBuf);   InBuf  = NULL;
    return 0;
}


int __cdecl grzip_compress (int Method,
                    int BlockSize,
                    int EnableLZP,
                    int MinMatchLen,
                    int HashSizeLog,
                    int AlternativeBWTSort,
                    int AdaptiveBlockSize,
                    int DeltaFilter,
                    CALLBACK_FUNC *callback,
                    void *auxdata)
{
  GRZipMTCompressor grz (Method,
                         BlockSize,
                         EnableLZP,
                         MinMatchLen,
                         HashSizeLog,
                         AlternativeBWTSort,
                         AdaptiveBlockSize,
                         DeltaFilter,
                         callback,
                         auxdata);
  return grz.run();
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)


// Single GRZip decompression thread
struct GRZipDecompressionThread : WorkerThread
{
    int process()
    {
        int res = GRZip_DecompressBlock ((uint8*)InBuf, InSize+28, (uint8*)OutBuf);
        return (res == GRZ_NOT_ENOUGH_MEMORY? FREEARC_ERRCODE_NOT_ENOUGH_MEMORY :
                res <  0?                     FREEARC_ERRCODE_GENERAL :
                                              res);
    }
    int after_write()                //// done() too
    {
        BigFree(OutBuf);  OutBuf = NULL;
        BigFree(InBuf);   InBuf = NULL;
        return 0;
    }
};

// Multi-threaded GRZip decompressor
struct GRZipMTDecompressor : MTCompressor<GRZipDecompressionThread>
{
    GRZipMTDecompressor (CALLBACK_FUNC *callback, void *auxdata)
    {
        this->callback  = callback;
        this->auxdata   = auxdata;
    }

    int main_cycle()
    {
        uint8 BlockSign[28];
        while (1)
        {
            sint32 NumRead=callback("read",BlockSign,28,auxdata);
            if (NumRead==0)                                          return FREEARC_OK;    // Конец данных
            if (NumRead!=28)                                         return NumRead<0? NumRead:FREEARC_ERRCODE_BAD_COMPRESSED_DATA;
            if (GRZip_CheckBlockSign(BlockSign,28)!=GRZ_NO_ERROR)    return FREEARC_ERRCODE_BAD_COMPRESSED_DATA;

            GRZipDecompressionThread *job = FreeJobs.Get();            // Acquire next compression job
            job->InBuf = (char*) BigAlloc (*(sint32*)(BlockSign+16) + LZP_MaxMatchLen);
            if (job->InBuf==NULL)                                    return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;
            memcpy(job->InBuf,BlockSign,28);
            job->OutBuf = (char*) BigAlloc (*(sint32*)job->InBuf + LZP_MaxMatchLen);
            if (job->OutBuf==NULL)                                   return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;
            job->InSize = callback("read",job->InBuf+28,*(sint32 *)(job->InBuf+16),auxdata);
            if (job->InSize != *(sint32 *)(job->InBuf+16))           return job->InSize<0? job->InSize : FREEARC_ERRCODE_BAD_COMPRESSED_DATA;
            WriterJobs.Put(job);
            job->StartOperation.Signal();
        }
    }
};


int __cdecl grzip_decompress (CALLBACK_FUNC *callback, void *auxdata)
{
  GRZipMTDecompressor grz (callback, auxdata);
  return grz.run();
}


/*-------------------------------------------------*/
/* Реализация класса GRZIP_METHOD                  */
/*-------------------------------------------------*/
// Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
GRZIP_METHOD::GRZIP_METHOD()
{
  Method              = 1;
  BlockSize           = 8*mb;
  EnableLZP           = 1;
  MinMatchLen         = 32;
  HashSizeLog         = 15;
  AlternativeBWTSort  = 0;
  AdaptiveBlockSize   = 0;
  DeltaFilter         = 0;
}

// Функция распаковки
int GRZIP_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("grzip_decompress");
  if (!f) f = (FARPROC) grzip_decompress;

  return ((int (__cdecl *)(CALLBACK_FUNC*, void*)) f) (callback, auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int GRZIP_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("grzip_compress");
  if (!f) f = (FARPROC) grzip_compress;

  return ((int (__cdecl *)(int, int, int, int, int, int, int, int, CALLBACK_FUNC*, void*)) f)
                        (Method,
                         BlockSize,
                         EnableLZP,
                         MinMatchLen,
                         HashSizeLog,
                         AlternativeBWTSort,
                         AdaptiveBlockSize,
                         DeltaFilter,
                         callback,
                         auxdata);
}

// Установить размер блока и уменьшить размер хэша, если он слишком велик для такого маленького блока
void GRZIP_METHOD::SetBlockSize (MemSize bs)
{
  if (bs>0) {
    BlockSize   = mymin (bs, GRZ_MaxBlockSize);
    HashSizeLog = mymin (HashSizeLog, 1+lb(BlockSize-1));
  }
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_GRZIP)
void GRZIP_METHOD::ShowCompressionMethod (char *buf)
{
  char LZP_Str[100], BlockSizeStr[100];
  sprintf (LZP_Str, "l%d:h%d", MinMatchLen, HashSizeLog);
  showMem (BlockSize, BlockSizeStr);
  sprintf (buf, "grzip:%s:m%d:%s%s%s%s", BlockSizeStr,
                                         Method,
                                         EnableLZP?          LZP_Str : "l",
                                         AlternativeBWTSort? ":s" : "",
                                         AdaptiveBlockSize?  ":a" : "",
                                         DeltaFilter?        ":d" : "");
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Конструирует объект типа GRZIP_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка при задании параметров
COMPRESSION_METHOD* parse_GRZIP (char** parameters)
{
  if (strcmp (parameters[0], "grzip") == 0) {
    // Если название метода (нулевой параметр) - "grzip", то разберём остальные параметры

    GRZIP_METHOD *p = new GRZIP_METHOD;
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    while (!error && *++parameters)  // Переберём все параметры метода
    {
      char *param = *parameters;
      if (strlen(param)==1) switch (*param) {    // Однобуквенные параметры
        case 's':  p->AlternativeBWTSort  = 1; continue;
        case 'a':  p->AdaptiveBlockSize   = 1; continue;
        case 'l':  p->EnableLZP           = 0; continue;
        case 'd':  p->DeltaFilter         = 1; continue;
        case 'p':  p->AdaptiveBlockSize=0; p->EnableLZP=0; p->DeltaFilter=1; continue;
      }
      else switch (*param) {                    // Параметры, содержащие значения
        case 'm':  p->Method      = parseInt (param+1, &error); continue;
        case 'b':  p->BlockSize   = parseMem (param+1, &error); continue;
        case 'l':  p->MinMatchLen = parseInt (param+1, &error); continue;
        case 'h':  p->HashSizeLog = parseInt (param+1, &error); continue;
      }
      // Сюда мы попадаем, если в параметре не указано его название
      // Если этот параметр удастся разобрать как целое число (т.е. в нём - только цифры),
      // то присвоим его значение полю MinMatchLen, иначе попробуем разобрать его как BlockSize
      int n = parseInt (param, &error);
      if (!error) p->MinMatchLen = n;
      else        error=0, p->BlockSize = parseMem (param, &error);
    }
    if (error)  {delete p; return NULL;}  // Ошибка при парсинге параметров метода
    return p;
  } else
    return NULL;   // Это не метод grzip
}

static int GRZIP_x = AddCompressionMethod (parse_GRZIP);   // Зарегистрируем парсер метода GRZIP

/*-------------------------------------------------*/
/* End                                  libGRZip.c */
/*-------------------------------------------------*/
