#define TORNADO_LIBRARY
#include "Tornado.cpp"
extern "C" {
#include "C_Tornado.h"
}

/*-------------------------------------------------*/
/* Реализация класса TORNADO_METHOD                */
/*-------------------------------------------------*/

// Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
TORNADO_METHOD::TORNADO_METHOD()
{
  m = std_Tornado_method [default_Tornado_method];
}

// Функция распаковки
int TORNADO_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
  return tor_decompress (callback, auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int TORNADO_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
  return tor_compress (m, callback, auxdata);
}

// Установить размер словаря и откорректировать размер хеша
void TORNADO_METHOD::SetDictionary (MemSize dict)
{
  if (dict>0) {
    if (dict < m.buffer)
      // При уменьшении словаря: уменьшить размер хэша, если он слишком велик для такого маленького блока
      m.hashsize  =  sizeof(PtrVal)  *  mymin (m.hashsize/sizeof(PtrVal), roundup_to_power_of(dict,2));
    else
      // При увеличении словаря: пропорционально увеличить размер хеша
      if (m.hashsize > 1*mb)
      {
        if (m.hashsize<8*mb && m.hashsize<m.buffer/2)   m.hashsize = m.buffer/2;  // Во-первых, увеличим размер хеша, если он был подогнан под кеш Core2
        uint h  =  mymin (uint64(dict) / (m.buffer/64) * (m.hashsize/64),  2*gb);  // Идеальный размер нового хеша
        m.hashsize = mymin (round_to_nearest_power_of(h / m.hash_row_width, 2) * m.hash_row_width,  2*gb);  // Округлим размер хеша с учётом row_width
      }
    m.buffer = dict;
  }
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_TORNADO)
void TORNADO_METHOD::ShowCompressionMethod (char *buf)
{
    struct PackMethod defaults = std_Tornado_method[m.number];  char NumStr[100], BufferStr[100], HashSizeStr[100], TempHashSizeStr[100], RowStr[100], EncStr[100], ParserStr[100], StepStr[100], TableStr[100], TempAuxHashSizeStr[100], AuxHashSizeStr[100], AuxRowStr[100];
    showMem (m.buffer,       BufferStr);
    showMem (m.hashsize,     TempHashSizeStr);
    showMem (m.auxhash_size, TempAuxHashSizeStr);
    sprintf (NumStr,         m.number            != default_Tornado_method?     ":%d"    : "", m.number);
    sprintf (HashSizeStr,    m.hashsize          != defaults.hashsize?          ":h%s"   : "", TempHashSizeStr);
    sprintf (RowStr,         m.hash_row_width    != defaults.hash_row_width?    ":l%d"   : "", m.hash_row_width);
    sprintf (EncStr,         m.encoding_method   != defaults.encoding_method?   ":c%d"   : "", m.encoding_method);
    sprintf (ParserStr,      m.match_parser      != defaults.match_parser?      ":p%d"   : "", m.match_parser);
    sprintf (StepStr,        m.update_step       != defaults.update_step?       ":u%d"   : "", m.update_step);
    sprintf (TableStr,       m.find_tables       != defaults.find_tables?       ":t%d"   : "", m.find_tables);
    sprintf (AuxHashSizeStr, m.auxhash_size      != defaults.auxhash_size?      ":ah%s"  : "", TempAuxHashSizeStr);
    sprintf (AuxRowStr,      m.auxhash_row_width != defaults.auxhash_row_width? ":al%d"  : "", m.auxhash_row_width);
    sprintf (buf, "tor%s:%s%s%s%s%s%s%s%s%s", NumStr, BufferStr, HashSizeStr, RowStr, EncStr, ParserStr, StepStr, TableStr, AuxHashSizeStr, AuxRowStr);
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Конструирует объект типа TORNADO_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка в параметрах
COMPRESSION_METHOD* parse_TORNADO (char** parameters)
{
  if (strcmp (parameters[0], "tor") == 0) {
    // Если название метода (нулевой параметр) - "tor", то разберём остальные параметры

    TORNADO_METHOD *p = new TORNADO_METHOD;
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    // Переберём все параметры метода (или выйдем раньше при возникновении ошибки при разборе очередного параметра)
    while (*++parameters && !error)
    {
      char* param = *parameters;
      switch (*param) {                    // Параметры, содержащие значения
        case 'b': p->m.buffer          = parseMem (param+1, &error); continue;
        case 'h': p->m.hashsize        = parseMem (param+1, &error); continue;
        case 'l': p->m.hash_row_width  = parseInt (param+1, &error); continue;
        case 'c': p->m.encoding_method = parseInt (param+1, &error); continue;
        case 'p': p->m.match_parser    = parseInt (param+1, &error); continue;
        case 'u': p->m.update_step     = parseInt (param+1, &error); continue;
        case 't': p->m.find_tables     = parseInt (param+1, &error); continue;
        case 'a': switch (param[1]) {      // Параметры ah/al
                    case 'h': p->m.auxhash_size       = parseMem (param+2, &error); continue;
                    case 'l': p->m.auxhash_row_width  = parseInt (param+2, &error); continue;
                  }
      }
      // Сюда мы попадаем, если в параметре не указано его название
      // Если этот параметр удастся разобрать как целое число (т.е. в нём - только цифры),
      // то будем считать, что это номер пресета, иначе попробуем разобрать его как buffer
      int n = parseInt (param, &error);
      if (!error)  p->m = std_Tornado_method[n];
      else         error=0, p->m.buffer = parseMem (param, &error);
    }

    if (error)  {delete p; return NULL;}  // Ошибка при парсинге параметров метода
    return p;
  } else
    return NULL;   // Это не метод TORNADO
}

static int TORNADO_x = AddCompressionMethod (parse_TORNADO);   // Зарегистрируем парсер метода TORNADO
