extern "C" {
#include "C_MM.h"
}
#define MM_LIBRARY
#include "mm.cpp"


/*-------------------------------------------------*/
/* Реализация класса MM_METHOD                     */
/*-------------------------------------------------*/

// Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
MM_METHOD::MM_METHOD()
{
    mode        = 9;
    skip_header = 0;
    is_float    = 0;
    num_chan    = 0;
    word_size   = 0;
    offset      = 0;
    reorder     = 0;
}

// Функция распаковки
int MM_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
    return mm_decompress (callback, auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int MM_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
    return mm_compress (mode, skip_header, is_float, num_chan, word_size, offset, reorder, callback, auxdata);
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_MM)
void MM_METHOD::ShowCompressionMethod (char *buf)
{
    MM_METHOD defaults;
    char dStr[100], cStr[100], rStr[100];
    if (num_chan || word_size) {
        sprintf (cStr, ":%d*%d%s", num_chan, word_size, is_float? "f":"");
        if (offset)  sprintf (str_end(cStr), ":o%d", offset);
    } else {
        sprintf (cStr, skip_header? ":s" : "");
    }
    sprintf (rStr, reorder? ":r%d" : "", reorder);
    sprintf (dStr, mode!=defaults.mode? ":d%d" : "", mode);
    sprintf (buf, "mm%s%s%s", dStr, cStr, rStr);
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Конструирует объект типа MM_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка в параметрах
COMPRESSION_METHOD* parse_MM (char** parameters)
{
  if (strcmp (parameters[0], "mm") == 0) {
    // Если название метода (нулевой параметр) - "mm", то разберём остальные параметры

    MM_METHOD *p = new MM_METHOD;
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    // Переберём все параметры метода (или выйдем раньше при возникновении ошибки при разборе очередного параметра)
    while (*++parameters && !error)
    {
      char* param = *parameters;
      switch (*param) {                    // Параметры, содержащие значения
        case 's':  p->skip_header = 1;                          continue;
        case 'f':  p->is_float    = 1;                          continue;
        case 'd':  p->mode        = parseInt (param+1, &error); continue;
        case 'c':  p->num_chan    = parseInt (param+1, &error); continue;
        case 'w':  p->word_size   = parseInt (param+1, &error); continue;
        case 'o':  p->offset      = parseInt (param+1, &error); continue;
        case 'r':  p->reorder     = parseInt (param+1, &error); continue;
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
    return NULL;   // Это не метод MM
}

static int MM_x = AddCompressionMethod (parse_MM);   // Зарегистрируем парсер метода MM

