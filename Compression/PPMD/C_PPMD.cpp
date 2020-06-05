#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

extern "C" {
#include "C_PPMD.h"
}
#include "PPMdType.h"

/*-------------------------------------------------*/
/* Реализация ppmd_compress                        */
/*-------------------------------------------------*/
#ifndef FREEARC_DECOMPRESS_ONLY

namespace PPMD_compression {

#include "Model.cpp"

extern "C" {
int ppmd_compress (int order, MemSize mem, int MRMethod, CALLBACK_FUNC *callback, void *auxdata)
{
  if ( !StartSubAllocator(mem) ) {
    return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;
  }
  _PPMD_FILE* fpIn  = new _PPMD_FILE (callback, auxdata);
  _PPMD_FILE* fpOut = new _PPMD_FILE (callback, auxdata);
  EncodeFile (fpOut, fpIn, order, MR_METHOD(MRMethod));
  fpOut->flush();
  int ErrCode = FREEARC_OK;
  if (_PPMD_ERROR_CODE(fpIn) <0)  ErrCode = _PPMD_ERROR_CODE (fpIn);
  if (_PPMD_ERROR_CODE(fpOut)<0)  ErrCode = _PPMD_ERROR_CODE (fpOut);
  delete fpOut;
  delete fpIn;
  StopSubAllocator();
  return ErrCode;
}
} // extern "C"

void _STDCALL PrintInfo (_PPMD_FILE* DecodedFile, _PPMD_FILE* EncodedFile)
{
}

} // namespace PPMD_compression
#undef _PPMD_H_

#endif // FREEARC_DECOMPRESS_ONLY


/*-------------------------------------------------*/
/* Реализация ppmd_decompress                      */
/*-------------------------------------------------*/

namespace PPMD_decompression {

#include "Model.cpp"

extern "C" {
int ppmd_decompress (int order, MemSize mem, int MRMethod, CALLBACK_FUNC *callback, void *auxdata)
{
  if ( !StartSubAllocator(mem) ) {
    return FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;
  }
  _PPMD_FILE* fpIn  = new _PPMD_FILE (callback, auxdata);
  _PPMD_FILE* fpOut = new _PPMD_FILE (callback, auxdata);
  DecodeFile (fpOut, fpIn, order, MR_METHOD(MRMethod));
  fpOut->flush();
  int ErrCode = FREEARC_OK;
  if (_PPMD_ERROR_CODE(fpIn) <0)  ErrCode = _PPMD_ERROR_CODE (fpIn);
  if (_PPMD_ERROR_CODE(fpOut)<0)  ErrCode = _PPMD_ERROR_CODE (fpOut);
  delete fpOut;
  delete fpIn;
  StopSubAllocator();
  return ErrCode;
}
} // extern "C"

void _STDCALL PrintInfo(_PPMD_FILE* DecodedFile,_PPMD_FILE* EncodedFile)
{
}

} // namespace PPMD_decompression


/*-------------------------------------------------*/
/* Реализация класса PPMD_METHOD                  */
/*-------------------------------------------------*/

// Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
PPMD_METHOD::PPMD_METHOD()
{
  order    = 10;
  mem      = 48*mb;
  MRMethod = 0;
}

// Функция распаковки
int PPMD_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("ppmd_decompress");
  if (!f) f = (FARPROC) ppmd_decompress;

  return ((int (*)(int, MemSize, int, CALLBACK_FUNC*, void*)) f) (order, mem, MRMethod, callback, auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int PPMD_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("ppmd_compress");
  if (!f) f = (FARPROC) ppmd_compress;

  return ((int (*)(int, MemSize, int, CALLBACK_FUNC*, void*)) f) (order, mem, MRMethod, callback, auxdata);
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_PPMD)
void PPMD_METHOD::ShowCompressionMethod (char *buf)
{
  char MemStr[100];
  showMem (mem, MemStr);
  sprintf (buf, "ppmd:%d:%s%s", order, MemStr, MRMethod==2? ":r2": (MRMethod==1? ":r":""));
}

// Изменить потребность в памяти, заодно оттюнинговав order
void PPMD_METHOD::SetCompressionMem (MemSize _mem)
{
  if (_mem==0)  return;
  order  +=  int (log(double(_mem)/mem) / log(double(2)) * 4);
  mem = _mem;
}


#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Конструирует объект типа PPMD_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка в параметрах
COMPRESSION_METHOD* parse_PPMD (char** parameters)
{
  if (strcmp (parameters[0], "ppmd") == 0) {
    // Если название метода (нулевой параметр) - "ppmd", то разберём остальные параметры

    PPMD_METHOD *p = new PPMD_METHOD;
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    // Переберём все параметры метода (или выйдем раньше при возникновении ошибки при разборе очередного параметра)
    while (*++parameters && !error)
    {
      char *param = *parameters;
      if (start_with (param, "mem")) {
        param+=2;  // Обработать "mem..." как "m..."
      }
      if (strlen(param)==1) switch (*param) {    // Однобуквенные параметры
        case 'r':  p->MRMethod = 1; continue;
      }
      else switch (*param) {                    // Параметры, содержащие значения
        case 'm':  p->mem      = parseMem (param+1, &error); continue;
        case 'o':  p->order    = parseInt (param+1, &error); continue;
        case 'r':  p->MRMethod = parseInt (param+1, &error); continue;
      }
      // Сюда мы попадаем, если в параметре не указано его название
      // Если этот параметр удастся разобрать как целое число (т.е. в нём - только цифры),
      // то присвоим его значение полю order, иначе попробуем разобрать его как mem
      int n = parseInt (param, &error);
      if (!error) p->order = n;
      else        error=0, p->mem = parseMem (param, &error);
    }
    if (error)  {delete p; return NULL;}  // Ошибка при парсинге параметров метода
    return p;
  } else
    return NULL;   // Это не метод ppmd
}

static int PPMD_x = AddCompressionMethod (parse_PPMD);   // Зарегистрируем парсер метода PPMD
