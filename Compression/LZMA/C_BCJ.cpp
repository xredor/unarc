extern "C" {
#include "C_BCJ.h"
}

#include "7zip/Compress/Branch/BranchX86.c"
#include "7zip/Compress/Branch/BranchCoder.cpp"
#include "7zip/Compress/Branch/x86.cpp"

template <class CBCJ_x86_Coder>
int bcj_x86_de_compress( CALLBACK_FUNC *callback, void *auxdata)
{
  CBCJ_x86_Coder c; c.Init();                    // энкодер/декодер для BCJ-X86 препроцессора
  BYTE* Buf = (BYTE*)malloc(LARGE_BUFFER_SIZE);  // место для данных
  if (Buf==NULL)   return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;
  int RemainderSize=0;                           // остаток данных с предыдущего раза
  int x, InSize;                                 // количество прочитанных байт
  while ( (InSize = x = callback ("read", Buf+RemainderSize, LARGE_BUFFER_SIZE-RemainderSize, auxdata)) >= 0 )
  {
    if ((InSize+=RemainderSize)==0)            goto Ok;  // Данных больше нет
    int OutSize = InSize<=5? InSize : c.Filter(Buf, InSize);  // Меньше 5 байт этот фильтр не воспринимает :)
    if( (x=callback("write",Buf,OutSize,auxdata)) != OutSize )      goto Error;
    RemainderSize = InSize-OutSize;
    // Перенесём необработанный остаток данных в начало буфера
    if (RemainderSize>0)                memmove(Buf,Buf+OutSize,RemainderSize);
  }
Error: delete Buf; return x;            // произошла ошибка при чтении/записи
Ok:    delete Buf; return FREEARC_OK;   // всё в порядке
}


/*-------------------------------------------------*/
/* Реализация класса BCJ_X86_METHOD                */
/*-------------------------------------------------*/
// Функция распаковки
int BCJ_X86_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
  return bcj_x86_de_compress<CBCJ_x86_Decoder> (callback, auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int BCJ_X86_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
  return bcj_x86_de_compress<CBCJ_x86_Encoder> (callback, auxdata);
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия (функция, обратная к parse_BCJ_X86)
void BCJ_X86_METHOD::ShowCompressionMethod (char *buf)
{
  sprintf (buf, "exe");
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Конструирует объект типа BCJ_X86_METHOD или возвращает NULL, если это другой метод сжатия
COMPRESSION_METHOD* parse_BCJ_X86 (char** parameters)
{
  if (strcmp (parameters[0], "exe") == 0
      &&  parameters[1]==NULL )
    // Если название метода - "exe" и параметров у него нет, то это наш метод
    return new BCJ_X86_METHOD;
  else
    return NULL;   // Это не метод bcj_x86
}

static int BCJ_X86_x = AddCompressionMethod (parse_BCJ_X86);   // Зарегистрируем парсер метода BCJ_X86
