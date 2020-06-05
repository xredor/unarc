extern "C" {
#include "C_TTA.h"
#include "mmdet.h"
}

#define TTA_LIBRARY
#include "entropy.cpp"
#include "filters.cpp"
#include "tta.cpp"


/*-------------------------------------------------*/
/* Реализация класса TTA_METHOD                    */
/*-------------------------------------------------*/

// Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
TTA_METHOD::TTA_METHOD()
{
  level       = 3;
  skip_header = 0;
  is_float    = 0;
  num_chan    = 0;
  word_size   = 0;
  offset      = 0;
  raw_data    = 0;
}

// Функция распаковки
int TTA_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("tta_decompress");
  if (!f) f = (FARPROC) tta_decompress;

  return ((int (__cdecl *)(CALLBACK_FUNC*, void*)) f) (callback, auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int TTA_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
  // Use faster function from DLL if possible
  static FARPROC f = LoadFromDLL ("tta_compress");
  if (!f) f = (FARPROC) tta_compress;

  return ((int (__cdecl *)(int, int, int, int, int, int, int, CALLBACK_FUNC*, void*)) f)
                          (level, skip_header, is_float, num_chan, word_size, offset, raw_data, callback, auxdata);
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_TTA)
void TTA_METHOD::ShowCompressionMethod (char *buf)
{
    TTA_METHOD defaults;  char eStr[100], cStr[100], rStr[100];
    if (num_chan || word_size) {
        sprintf (cStr, ":%d*%d%s", num_chan, word_size, is_float? "f":"");
        if (offset)  sprintf (str_end(cStr), ":o%d", offset);
    } else {
        sprintf (cStr, skip_header? ":s" : "");
    }
    sprintf (eStr, level      !=defaults.level?       ":m%d" : "", level);
    sprintf (rStr, raw_data   !=defaults.raw_data?    ":r%d" : "", raw_data);
    sprintf (buf, "tta%s%s%s", eStr, cStr, rStr);
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Конструирует объект типа TTA_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка в параметрах
COMPRESSION_METHOD* parse_TTA (char** parameters)
{
  if (strcmp (parameters[0], "tta") == 0) {
    // Если название метода (нулевой параметр) - "tta", то разберём остальные параметры

    TTA_METHOD *p = new TTA_METHOD;
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    // Переберём все параметры метода (или выйдем раньше при возникновении ошибки при разборе очередного параметра)
    while (*++parameters && !error)
    {
      char* param = *parameters;
      switch (*param) {                    // Параметры, содержащие значения
        case 'm':  p->level       = parseInt (param+1, &error); continue;
        case 's':  p->skip_header = 1;                          continue;
        case 'f':  p->is_float    = 1;                          continue;
        case 'c':  p->num_chan    = parseInt (param+1, &error); continue;
        case 'w':  p->word_size   = parseInt (param+1, &error); continue;
        case 'o':  p->offset      = parseInt (param+1, &error); continue;
        case 'r':  p->raw_data    = parseInt (param+1, &error); continue;
      }
      // Сюда мы попадаем, если в параметре не указано его название
      // Если этот параметр удастся разобрать как c*w,
      // то используем эти значения для полей num_chan и word_size.
      // Дополнительный символ 'f' означает, что это данные в FP-формате
      int a, b;  char s[MAX_METHOD_STRLEN];
      if (sscanf (param, "%d*%d%s", &a, &b, s)==3  &&  strequ(s,"f"))
          p->is_float = 1, p->num_chan=a, p->word_size=b;
      else if (sscanf (param, "%d*%d", &a, &b)==2)
          p->is_float = 0, p->num_chan=a, p->word_size=b;
      else error=1;
    }
    if (error)  {delete p; return NULL;}  // Ошибка при парсинге параметров метода
    return p;
  } else
    return NULL;   // Это не метод TTA
}

static int TTA_x = AddCompressionMethod (parse_TTA);   // Зарегистрируем парсер метода TTA
