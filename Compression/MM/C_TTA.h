// Wave sound comression algorithm
#include "../Compression.h"
#include "ttaenc.h"


#ifdef __cplusplus

// Реализация стандартного интерфейса методов сжатия COMPRESSION_METHOD
class TTA_METHOD : public COMPRESSION_METHOD
{
public:
  // Параметры этого метода сжатия
  int level;        // Compression level (1..3, higher means tighter and slower compression)
  int skip_header;  // Skip WAV header detection
  int is_float;     // Floating-point data format
  int num_chan;     // Channels count
  int word_size;    // Size of each encoded value, in bits
  int offset;       // File offset where MM data start (header is copied intact)
  int raw_data ;    // Write raw predictor's output without using entropy encoder

  // Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
  TTA_METHOD();
  // Универсальный метод: возвращаем различные простые характеристики метода сжатия
  virtual int doit (char *what, int param, void *data, CALLBACK_FUNC *callback)
  {
      // Запретить солид-сжатие, если кодируются 2+ байтовые значения или пропускаются байты в начале каждого файла
      if (strequ (what,"nosolid?"))   return word_size!=8 || offset!=0;
      else return COMPRESSION_METHOD::doit (what, param, data, callback);
  }

  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata);
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress   (CALLBACK_FUNC *callback, void *auxdata);

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_TTA)
  virtual void ShowCompressionMethod (char *buf);

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual MemSize GetCompressionMem     (void)         {return 2*mb;}
  virtual MemSize GetDictionary         (void)         {return 0;}
  virtual MemSize GetBlockSize          (void)         {return 0;}
  virtual void    SetCompressionMem     (MemSize mem)  {}
  virtual void    SetDecompressionMem   (MemSize mem)  {}
  virtual void    SetDictionary         (MemSize dict) {}
  virtual void    SetBlockSize          (MemSize bs)   {}
#endif
  virtual MemSize GetDecompressionMem   (void)         {return 1*mb;}
};

// Разборщик строки метода сжатия TTA
COMPRESSION_METHOD* parse_TTA (char** parameters);

#endif  // __cplusplus
