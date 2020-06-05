#ifndef FREEARC_COMPRESSION_H
#define FREEARC_COMPRESSION_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <time.h>

#include "Common.h"


#ifdef __cplusplus
extern "C" {
#endif

//Коды ошибок
#define FREEARC_OK                               0     /* ALL RIGHT */
#define FREEARC_ERRCODE_GENERAL                  (-1)  /* Some error when (de)compressing */
#define FREEARC_ERRCODE_INVALID_COMPRESSOR       (-2)  /* Invalid compression method or parameters */
#define FREEARC_ERRCODE_ONLY_DECOMPRESS          (-3)  /* Program builded with FREEARC_DECOMPRESS_ONLY, so don't try to use compress */
#define FREEARC_ERRCODE_OUTBLOCK_TOO_SMALL       (-4)  /* Output block size in (de)compressMem is not enough for all output data */
#define FREEARC_ERRCODE_NOT_ENOUGH_MEMORY        (-5)  /* Can't allocate memory needed for (de)compression */
#define FREEARC_ERRCODE_READ                     (-6)  /* Error when reading data */
#define FREEARC_ERRCODE_BAD_COMPRESSED_DATA      (-7)  /* Data can't be decompressed */
#define FREEARC_ERRCODE_NOT_IMPLEMENTED          (-8)  /* Requested feature isn't supported */
#define FREEARC_ERRCODE_NO_MORE_DATA_REQUIRED    (-9)  /* Required part of data was already decompressed */
#define FREEARC_ERRCODE_OPERATION_TERMINATED    (-10)  /* Operation terminated by user */
#define FREEARC_ERRCODE_WRITE                   (-11)  /* Error when writing data */


// Константы для удобной записи объёмов памяти
#define b_ (1u)
#define kb (1024*b_)
#define mb (1024*kb)
#define gb (1024*mb)

// Количество байт, которые должны читаться/записываться за один раз во всех упаковщиках
#define BUFFER_SIZE (64*kb)

// Количество байт, которые должны читаться/записываться за один раз в быстрых методах и при распаковке асимметричных алгоритмов
#define LARGE_BUFFER_SIZE (256*kb)

// Количество байт, которые должны читаться/записываться за один раз в очень быстрых методах (storing, tornado и тому подобное)
// Этот объём минимизирует потери на disk seek operations - при условии, что одновременно не происходит в/в в другом потоке ;)
#define HUGE_BUFFER_SIZE (8*mb)

// Дополнительные определения для удобства создания парсеров строк методов сжатия
#define COMPRESSION_METHODS_DELIMITER            '+'   /* Разделитель алгоритмов сжатия в строковом описании компрессора */
#define COMPRESSION_METHOD_PARAMETERS_DELIMITER  ':'   /* Разделитель параметров в строковом описании метода сжатия */
#define MAX_COMPRESSION_METHODS    1000        /* Должно быть не меньше числа методов сжатия, регистрируемых с помощью AddCompressionMethod */
#define MAX_PARAMETERS             200         /* Должно быть не меньше максимального кол-ва параметров (разделённых двоеточиями), которое может иметь метод сжатия */
#define MAX_METHOD_STRLEN          2048        /* Максимальная длина строки, описывающей метод сжатия */
#define MAX_METHODS_IN_COMPRESSOR  100         /* Максимальное число методов в одном компрессоре */
#define MAX_EXTERNAL_COMPRESSOR_SECTION_LENGTH 2048  /* Максимальная длина секции [External compressor] */


// ****************************************************************************************************************************
// ХЕЛПЕРЫ ЧТЕНИЯ/ЗАПИСИ ДАННЫХ В МЕТОДАХ СЖАТИЯ ******************************************************************************
// ****************************************************************************************************************************

// Тип функции для обратных вызовов
typedef int CALLBACK_FUNC (const char *what, void *data, int size, void *auxdata);

// Макросы для чтения/записи в(ы)ходных потоков с проверкой, что передано ровно столько данных, сколько было запрошено
#define checked_read(ptr,size)         if ((x = callback("read" ,ptr,size,auxdata)) != size) {x>=0 && (x=FREEARC_ERRCODE_READ);  goto finished;}
#define checked_write(ptr,size)        if ((x = callback("write",ptr,size,auxdata)) != size) {x>=0 && (x=FREEARC_ERRCODE_WRITE); goto finished;}
// Макрос для чтения входных потоков с проверкой на ошибки и конец входных данных
#define checked_eof_read(ptr,size)     if ((x = callback("read", ptr,size,auxdata)) != size) {x>0  && (x=FREEARC_ERRCODE_READ);  goto finished;}

// Auxiliary code to read/write data blocks and 4-byte headers
#define INIT() callback ("init", NULL, 0, auxdata)
#define DONE() callback ("done", NULL, 0, auxdata)

#define MALLOC(type, ptr, size)                                            \
{                                                                          \
    (ptr) = (type*) malloc ((size) * sizeof(type));                        \
    if ((ptr) == NULL) {                                                   \
        errcode = FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;                       \
        goto finished;                                                     \
    }                                                                      \
}

#define BIGALLOC(type, ptr, size)                                          \
{                                                                          \
    (ptr) = (type*) BigAlloc ((size) * sizeof(type));                      \
    if ((ptr) == NULL) {                                                   \
        errcode = FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;                       \
        goto finished;                                                     \
    }                                                                      \
}

#define READ(buf, size)                                                    \
{                                                                          \
    void *localBuf = (buf);                                                \
    int localSize  = (size);                                               \
    if (localSize  &&  (errcode=callback("read",localBuf,localSize,auxdata)) != localSize) { \
        if (errcode>=0) errcode=FREEARC_ERRCODE_READ;                      \
        goto finished;                                                     \
    }                                                                      \
}

#define READ_LEN(len, buf, size)                                           \
{                                                                          \
    if ((errcode=(len)=callback("read",buf,size,auxdata)) < 0) {           \
        goto finished;                                                     \
    }                                                                      \
}

#define READ_LEN_OR_EOF(len, buf, size)                                    \
{                                                                          \
    if ((errcode=(len)=callback("read",buf,size,auxdata)) <= 0) {          \
        goto finished;                                                     \
    }                                                                      \
}

#define WRITE(buf, size)                                                   \
{                                                                          \
    void *localBuf = (buf);                                                \
    int localSize  = (size);                                               \
    /* "write" callback on success guarantees to write all the data and may return 0 */ \
    if (localSize && (errcode=callback("write",localBuf,localSize,auxdata))<0)  \
        goto finished;                                                     \
}

#define READ4(var)                                                         \
{                                                                          \
    unsigned char localHeader[4];                                          \
    READ (localHeader, 4);                                                 \
    (var) = value32 (localHeader);                                         \
}

#define READ4_OR_EOF(var)                                                  \
{                                                                          \
    int localHeaderSize;                                                   \
    unsigned char localHeader[4];                                          \
    READ_LEN_OR_EOF (localHeaderSize, localHeader, 4);                     \
    if (localHeaderSize!=4)  {errcode=FREEARC_ERRCODE_READ; goto finished;}\
    (var) = value32 (localHeader);                                         \
}

#define WRITE4(value)                                                      \
{                                                                          \
    unsigned char localHeader[4];                                          \
    setvalue32 (localHeader, value);                                       \
    WRITE (localHeader, 4);                                                \
}

#define QUASIWRITE(size)                                                   \
{                                                                          \
    int64 localSize = (size);                                              \
    callback ("quasiwrite", &localSize, size, auxdata);                    \
}

#define ReturnErrorCode(x)                                                 \
{                                                                          \
    errcode = (x);                                                         \
    goto finished;                                                         \
}                                                                          \


// Buffered data output
#ifndef FREEARC_STANDALONE_TORNADO
#define FOPEN()   Buffer fbuffer(BUFFER_SIZE)
#define FWRITE(buf, size)                                                  \
{                                                                          \
    void *flocalBuf = (buf);                                               \
    int flocalSize = (size);                                               \
    int rem = fbuffer.remainingSpace();                                    \
    if (flocalSize>=4096) {                                                \
        FFLUSH();                                                          \
        WRITE(flocalBuf, flocalSize);                                      \
    } else if (flocalSize < rem) {                                         \
        fbuffer.put (flocalBuf, flocalSize);                               \
    } else {                                                               \
        fbuffer.put (flocalBuf, rem);                                      \
        FFLUSH();                                                          \
        fbuffer.put ((byte*)flocalBuf+rem, flocalSize-rem);                \
    }                                                                      \
}
#define FFLUSH()  { WRITE (fbuffer.buf, fbuffer.len());  fbuffer.empty(); }
#define FCLOSE()  { FFLUSH();  fbuffer.free(); }
#endif // !FREEARC_STANDALONE_TORNADO


// ****************************************************************************************************************************
// УТИЛИТЫ ********************************************************************************************************************
// ****************************************************************************************************************************

// Алгоритм сжатия/шифрования, представленный в виде строки
typedef char *CMETHOD;

// Последовательность алгоритмов сжатия/шифрования, представленная в виде "exe+rep+lzma+aes"
typedef char *COMPRESSOR;

// Запросить сервис what метода сжатия method
int CompressionService (char *method, char *what, DEFAULT(int param,0), DEFAULT(void *data,NULL), DEFAULT(CALLBACK_FUNC *callback,NULL));

// Проверить, что данный компрессор включает алгоритм шифрования
int compressorIsEncrypted (COMPRESSOR c);
// Вычислить, сколько памяти нужно для распаковки данных, сжатых этим компрессором
MemSize compressorGetDecompressionMem (COMPRESSOR c);

// Get/set number of threads used for (de)compression
int  __cdecl GetCompressionThreads (void);
void __cdecl SetCompressionThreads (int threads);

// Load (accelerated) function from facompress.dll
FARPROC LoadFromDLL (char *funcname);

// Used in 4x4 only: read entire input buffer before compression begins, allocate output buffer large enough to hold entire compressed output
extern int compress_all_at_once;

// This function should cleanup Compression Library
void compressionLib_cleanup (void);


// ****************************************************************************************************************************
// СЕРВИСЫ СЖАТИЯ И РАСПАКОВКИ ДАННЫХ *****************************************************************************************
// ****************************************************************************************************************************

// Распаковать данные, упакованные заданным методом
int Decompress (char *method, CALLBACK_FUNC *callback, void *auxdata);
// Распаковать данные, сжатые цепочкой методов
int MultiDecompress (char *method, CALLBACK_FUNC *callback, void *auxdata);
// Прочитать из входного потока обозначение метода сжатия и распаковать данные этим методом
int DecompressWithHeader (CALLBACK_FUNC *callback, void *auxdata);
// Распаковать данные в памяти, записав в выходной буфер не более outputSize байт.
// Возвращает код ошибки или количество байт, записанных в выходной буфер
int DecompressMem (char *method, void *input, int inputSize, void *output, int outputSize);
int DecompressMemWithHeader     (void *input, int inputSize, void *output, int outputSize);

#ifndef FREEARC_DECOMPRESS_ONLY
// Упаковать данные заданным методом
int Compress   (char *method, CALLBACK_FUNC *callback, void *auxdata);
// Записать в выходной поток обозначение метода сжатия и упаковать данные этим методом
int CompressWithHeader (char *method, CALLBACK_FUNC *callback, void *auxdata);
// Упаковать данные в памяти, записав в выходной буфер не более outputSize байт.
// Возвращает код ошибки или количество байт, записанных в выходной буфер
int CompressMem           (char *method, void *input, int inputSize, void *output, int outputSize);
int CompressMemWithHeader (char *method, void *input, int inputSize, void *output, int outputSize);
// Вывести в out_method каноническое представление метода сжатия in_method (выполнить ParseCompressionMethod + ShowCompressionMethod)
int CanonizeCompressionMethod (char *in_method, char *out_method);
// Информация о памяти, необходимой для упаковки/распаковки, размере словаря и размере блока.
MemSize GetCompressionMem   (char *method);
MemSize GetDictionary       (char *method);
MemSize GetBlockSize        (char *method);
// Возвратить в out_method новый метод сжатия, настроенный на использование
// соответствующего количества памяти/словаря/размера блока
int SetCompressionMem   (char *in_method, MemSize mem,  char *out_method);
int SetDecompressionMem (char *in_method, MemSize mem,  char *out_method);
int SetDictionary       (char *in_method, MemSize dict, char *out_method);
int SetBlockSize        (char *in_method, MemSize bs,   char *out_method);
// Возвратить в out_method новый метод сжатия, уменьшив, если необходимо,
// используемую алгоритмом память / его словарь / размер блока
int LimitCompressionMem   (char *in_method, MemSize mem,  char *out_method);
int LimitDecompressionMem (char *in_method, MemSize mem,  char *out_method);
int LimitDictionary       (char *in_method, MemSize dict, char *out_method);
int LimitBlockSize        (char *in_method, MemSize bs,   char *out_method);
#endif
MemSize GetDecompressionMem (char *method);

// Функция "(рас)паковки", копирующая данные один в один
int copy_data   (CALLBACK_FUNC *callback, void *auxdata);


// ****************************************************************************************************************************
// КЛАСС, РЕАЛИЗУЮЩИЙ ИНТЕРФЕЙС К МЕТОДУ СЖАТИЯ *******************************************************************************
// ****************************************************************************************************************************

#ifdef __cplusplus

// Абстрактный интерфейс к произвольному методу сжатия
class COMPRESSION_METHOD
{
public:
  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata) = 0;
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress   (CALLBACK_FUNC *callback, void *auxdata) = 0;

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к ParseCompressionMethod)
  virtual void ShowCompressionMethod (char *buf) = 0;

  // Информация о памяти, необходимой для упаковки/распаковки,
  // размере словаря (то есть насколько далеко заглядывает алгоритм в поиске похожих данных - для lz/bs схем),
  // и размере блока (то есть сколько максимум данных имеет смысл помещать в один солид-блок - для bs схем и lzp)
  virtual MemSize GetCompressionMem   (void)         = 0;
  virtual MemSize GetDictionary       (void)         = 0;
  virtual MemSize GetBlockSize        (void)         = 0;
  // Настроить метод сжатия на использование заданного кол-ва памяти, словаря или размера блока
  virtual void    SetCompressionMem   (MemSize mem)  = 0;
  virtual void    SetDecompressionMem (MemSize mem)  = 0;
  virtual void    SetDictionary       (MemSize dict) = 0;
  virtual void    SetBlockSize        (MemSize bs)   = 0;
  // Ограничить используемую при упаковке/распаковке память, или словарь / размер блока
  void LimitCompressionMem   (MemSize mem)  {if (GetCompressionMem()   > mem)   SetCompressionMem(mem);}
  void LimitDecompressionMem (MemSize mem)  {if (GetDecompressionMem() > mem)   SetDecompressionMem(mem);}
  void LimitDictionary       (MemSize dict) {if (GetDictionary()       > dict)  SetDictionary(dict);}
  void LimitBlockSize        (MemSize bs)   {if (GetBlockSize()        > bs)    SetBlockSize(bs);}
#endif
  virtual MemSize GetDecompressionMem (void)         = 0;

  // Универсальный метод. Параметры:
  //   what: "compress", "decompress", "setCompressionMem", "limitDictionary"...
  //   data: данные для операции в формате, зависящем от конкретной выполняемой операции
  //   param&result: простой числовой параметр, что достаточно для многих информационных операций
  // Неиспользуемые параметры устанавливаются в NULL/0. result<0 - код ошибки
  virtual int doit (char *what, int param, void *data, CALLBACK_FUNC *callback);

  double addtime;  // Дополнительное время, потраченное на сжатие (во внешних программах, дополнительных threads и т.д.)
  COMPRESSION_METHOD() {addtime=0;}
  virtual ~COMPRESSION_METHOD() {}
//  Debugging code:  char buf[100]; ShowCompressionMethod(buf); printf("%s : %u => %u\n", buf, GetCompressionMem(), mem);
};


// ****************************************************************************************************************************
// ФАБРИКА COMPRESSION_METHOD *************************************************************************************************
// ****************************************************************************************************************************

// Сконструировать объект класса - наследника COMPRESSION_METHOD,
// реализующий метод сжатия, заданный в виде строки `method`
COMPRESSION_METHOD *ParseCompressionMethod (char* method);

typedef COMPRESSION_METHOD* (*CM_PARSER) (char** parameters);
typedef COMPRESSION_METHOD* (*CM_PARSER2) (char** parameters, void *data);
int AddCompressionMethod         (CM_PARSER parser);  // Добавить парсер нового метода в список поддерживаемых методов сжатия
int AddExternalCompressionMethod (CM_PARSER2 parser2, void *data);  // Добавить парсер внешнего метода сжатия с дополнительным параметром, который должен быть передан этому парсеру
#endif  // __cplusplus
void ClearExternalCompressorsTable (void);                          // Очистить таблицу внешних упаковщиков
#ifdef __cplusplus


// ****************************************************************************************************************************
// МЕТОД "СЖАТИЯ" STORING *****************************************************************************************************
// ****************************************************************************************************************************

// Реализация метода "сжатия" STORING
class STORING_METHOD : public COMPRESSION_METHOD
{
public:
  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata);
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress   (CALLBACK_FUNC *callback, void *auxdata);

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия (функция, обратная к parse_STORING)
  virtual void ShowCompressionMethod (char *buf);

  // Получить/установить объём памяти, используемой при упаковке/распаковке, размер словаря или размер блока
  virtual MemSize GetCompressionMem   (void)    {return BUFFER_SIZE;}
  virtual MemSize GetDictionary       (void)    {return 0;}
  virtual MemSize GetBlockSize        (void)    {return 0;}
  virtual void    SetCompressionMem   (MemSize) {}
  virtual void    SetDecompressionMem (MemSize) {}
  virtual void    SetDictionary       (MemSize) {}
  virtual void    SetBlockSize        (MemSize) {}
#endif
  virtual MemSize GetDecompressionMem (void)    {return BUFFER_SIZE;}
};

// Разборщик строки метода сжатия STORING
COMPRESSION_METHOD* parse_STORING (char** parameters);

#endif  // __cplusplus



// ****************************************************************************************************************************
// ENCRYPTION ROUTINES *****************************************************************************************************
// ****************************************************************************************************************************

// Generates key based on password and salt using given number of hashing iterations
void Pbkdf2Hmac (const BYTE *pwd, int pwdSize, const BYTE *salt, int saltSize,
                 int numIterations, BYTE *key, int keySize);

int fortuna_size (void);


#ifdef __cplusplus
}       // extern "C"
#endif

#endif  // FREEARC_COMPRESSION_H
