extern "C" {
#include "C_Delta.h"
}


#define DELTA_LIBRARY
#include "Delta.cpp"

/*-------------------------------------------------*/
/* Реализация класса DELTA_METHOD                    */
/*-------------------------------------------------*/

// Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
DELTA_METHOD::DELTA_METHOD()
{
  BlockSize      = 8*mb;
  ExtendedTables = 0;
}

// Функция распаковки
int DELTA_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("delta_decompress");
  if (!f) f = (FARPROC) delta_decompress;

  return ((int (__cdecl *)(MemSize, int, CALLBACK_FUNC*, void*)) f)
                          (BlockSize, ExtendedTables, callback, auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int DELTA_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("delta_compress");
  if (!f) f = (FARPROC) delta_compress;

  return ((int (__cdecl *)(MemSize, int, CALLBACK_FUNC*, void*)) f)
                          (BlockSize, ExtendedTables, callback, auxdata);
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_DELTA)
void DELTA_METHOD::ShowCompressionMethod (char *buf)
{
    DELTA_METHOD defaults; char BlockSizeStr[100]=":";
    showMem (BlockSize, BlockSizeStr+1);
    sprintf (buf, "delta%s%s", BlockSize!=defaults.BlockSize? BlockSizeStr:"", ExtendedTables? ":x":"");
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Конструирует объект типа DELTA_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка в параметрах
COMPRESSION_METHOD* parse_DELTA (char** parameters)
{
  if (strcmp (parameters[0], "delta") == 0) {
    // Если название метода (нулевой параметр) - "delta", то разберём остальные параметры

    DELTA_METHOD *p = new DELTA_METHOD;
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    // Переберём все параметры метода (или выйдем раньше при возникновении ошибки при разборе очередного параметра)
    while (*++parameters && !error)
    {
      char* param = *parameters;
      if (strlen(param)==1) switch (*param) {    // Однобуквенные параметры
        case 'x':  p->ExtendedTables = 1; continue;
      }
      switch (*param) {                    // Параметры, содержащие значения
        case 'b':  p->BlockSize = parseMem (param+1, &error); continue;
      }
      // Сюда мы попадаем, если в параметре не указано его название
      // Если этот параметр удастся разобрать как объём памяти,
      // то присвоим его значение полю BlockSize
      p->BlockSize = parseMem (param, &error);
    }
    if (error)  {delete p; return NULL;}  // Ошибка при парсинге параметров метода
    return p;
  } else
    return NULL;   // Это не метод DELTA
}

static int DELTA_x = AddCompressionMethod (parse_DELTA);   // Зарегистрируем парсер метода DELTA

