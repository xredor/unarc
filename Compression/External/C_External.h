#include "../Compression.h"

int external_compress   (char *packcmd, char *unpackcmd, char *datafile, char *packedfile, CALLBACK_FUNC *callback, void *auxdata);
int external_decompress (char *packcmd, char *unpackcmd, char *datafile, char *packedfile, CALLBACK_FUNC *callback, void *auxdata);

// Добавить в таблицу методов сжатия описанный пользователем в arc.ini внешний упаковщик.
// params содержит описание упаковщика из arc.ini. Возвращает 1, если описание корректно.
int AddExternalCompressor (char *params);

#ifdef __cplusplus

// Реализация стандартного интерфейса методов сжатия COMPRESSION_METHOD
class EXTERNAL_METHOD : public COMPRESSION_METHOD
{
public:
  // Параметры этого метода сжатия
  char    *name;            // Имя метода (pmm, ccm...)
  bool     can_set_mem;     // Доступно изменение требований к памяти?
  MemSize  cmem;            // Объём памяти, используемой для сжатия
  MemSize  dmem;            // Объём памяти, используемой для распаковки
  char    *datafile;        // Наименование файла с неупакованными данными
  char    *packedfile;      // Наименование файла с упакованными данными
  char    *packcmd;         // Команда упаковки данных (datafile -> packedfile)
  char    *unpackcmd;       // Команда распаковки данных (packedfile -> datafile)
  char    *options[MAX_PARAMETERS];             // Доп. параметры метода
  char     option_strings[MAX_METHOD_STRLEN];   // Текстовый буфер для хранения текста параметров
  char    *defaultopt;      // Значения параметров по умолчанию
  int      solid;           // Разрешено делать солид-блоки?

  // Параметры, специфичные для PPMonstr
  int     order;            // Порядок модели (по скольким последним сивмолам предсказывается следующий)
  int     MRMethod;         // Что делать, когда память, выделенная для хранения модели, исчерпана
  int     MinCompression;   // Минимальный процент сжатия. Если выходные данные больше, то вместо них будут записаны оригинальные (несжатые) данные

  EXTERNAL_METHOD() {};
  // Универсальный метод: возвращаем различные простые характеристики метода сжатия
  virtual int doit (char *what, int param, void *data, CALLBACK_FUNC *callback)
  {
      if      (strequ (what,"external?"))  return 1;
      else if (strequ (what,"nosolid?"))   return !solid;
      else return COMPRESSION_METHOD::doit (what, param, data, callback);
  }

  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata);
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress   (CALLBACK_FUNC *callback, void *auxdata);

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_EXTERNAL)
  virtual void ShowCompressionMethod (char *buf);

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual MemSize GetCompressionMem     (void)          {return cmem;}
  virtual MemSize GetDictionary         (void)          {return 0;}
  virtual MemSize GetBlockSize          (void)          {return 0;}
  virtual void    SetCompressionMem     (MemSize _mem);
  virtual void    SetDecompressionMem   (MemSize _mem)  {SetCompressionMem(_mem);}
  virtual void    SetDictionary         (MemSize dict)  {}
  virtual void    SetBlockSize          (MemSize bs)    {}
#endif
  virtual MemSize GetDecompressionMem   (void)          {return dmem;}
};

// Разборщик строки препроцессора EXTERNAL
COMPRESSION_METHOD* parse_EXTERNAL (char** parameters);

#endif  // __cplusplus
