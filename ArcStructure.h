#pragma once

// Обработка архивов, созданных FreeArc:
//   чтение и декодирование Footer блока и блоков оглавления
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "Environment.h"
#include "Compression/Compression.h"

#define aSIGNATURE make4byte(65,114,67,1)  /* Сигнатура архивов FreeArc: ArC */
#define MAX_FOOTER_DESCRIPTOR_SIZE 4096    /* Максимальный размер дескриптора блока архива */

/******************************************************************************
** Класс, реализующий массивы, знающие свой размер :) *************************
******************************************************************************/
#ifdef __cplusplus
template <typename T> class ARRAY
{
public:
  int size;                         // Кол-во элементов в массиве
  T  *data;                         // Данные, хранящиеся в массиве
  bool autodelete;                  // Автоматически удалять data при удалении самого массива

  void setsize (int _size)          {size = _size; data = size? new T[size] : NULL; autodelete=TRUE;}
  void resize (int _size)           {if(autodelete) delete data; setsize(_size);}   // Изменить длину уже существующего массива
  void set (int _size, void* ptr)   {resize(0); size=_size, data=(T*)ptr, autodelete=FALSE;}  // Использовать в качестве содержимого массива указанный кусок в памяти
  ARRAY (int _size=0)               {setsize (_size);}       // Создать массив с длиной _size
  ~ARRAY()                          {resize(0);}
  T& operator[] (int i)             {return data[i];}
  T& operator() (int i)             {return data[i];}
};

#endif  // __cplusplus


/******************************************************************************
** Синонимы для простых типов, используемых в программе ***********************
******************************************************************************/
typedef time_t   XFILETIME;        // дата/время файла
typedef int      BOOL;             // булевский тип
typedef uint32   CRC;              // CRC файла
typedef char*    COMPRESSOR;       // метод сжатия
typedef int      BLOCKTYPE;        // тип архивного блока:
enum {DESCR_BLOCK=0, HEADER_BLOCK, DATA_BLOCK, DIR_BLOCK, FOOTER_BLOCK, RECOVERY_BLOCK};

struct BLOCK                       // информация о блоке архива
{
  BLOCKTYPE  type;
  COMPRESSOR compressor;
  FILESIZE   pos;
  FILESIZE   origsize;
  FILESIZE   compsize;
  CRC        crc;
};

struct BLOCK_DESCRIPTOR : BLOCK {};// дескриптор блока архива


/******************************************************************************
** Чтение потока данных *******************************************************
******************************************************************************/
class MEMORY_BUFFER
{
public:
    char *buf;         // адрес начала буфера, необходим для освобождения памяти
    char *bufend;      // адрес после буфера, используется для проверки выхода за его пределы
    char *p;           // текущий указатель чтения

    MEMORY_BUFFER () {buf = NULL;}
    ~MEMORY_BUFFER() {free (buf);}

    // Использовать буфер для чтения данных из файла `file` с позиции `pos` длины `len`
    MEMORY_BUFFER& open (MYFILE &file, FILESIZE pos, FILESIZE size)
    {
      free (buf);                      // Освободим предыдущий использованный буфер
      buf = (char*) malloc (size+8);   // Мы выделяем 8 лишних байт, чтобы можно было быстро декодировать целые числа, не опасаясь выйти за границу буфера
      CHECK (buf, (s,"ERROR: can't alloc %lu memory bytes", (unsigned long)(size+8)));
      file.seek (pos);
      file.read (buf, size);
      p=buf, bufend=p+size;
      return *this;
    }

    // Прочитать данные из файла в буфер, распаковать их и проверить их CRC
    MEMORY_BUFFER& openCompressedCheckCRC (COMPRESSOR compressor, FILESIZE origsize, MYFILE &file, FILESIZE pos, FILESIZE compsize, CRC right_crc)
    {
      open (file, pos, compsize);
      char *origbuf = (char*) malloc (origsize+8);  // Лишние 8 байт для запаса при выполнении readInteger
      int result = DecompressMem (compressor, buf, compsize, origbuf, origsize);
      CHECK (result!=FREEARC_ERRCODE_INVALID_COMPRESSOR, (s,"ERROR: unsupported compression method \"%s\"", compressor));
      CHECK (result==origsize, (s,"ERROR: archive structure corrupted (decompression of control block failed)"));
      free(buf), p=buf=origbuf, bufend=buf+origsize;
      CRC crc = CalcCRC (buf, origsize);
      CHECK (crc==right_crc, (s,"ERROR: archive structure corrupted (control block failed CRC check)"))
      return *this;
    }

    // Прочитать данные из файла в буфер и проверить соответствие их CRC значению, записанному в последних байтах этих данных
    MEMORY_BUFFER& openWithCRCAtEnd (MYFILE &file, FILESIZE pos, FILESIZE size)
    {
      open (file, pos, size);
      bufend -= sizeof(CRC);
      CRC right_crc = *(CRC*)bufend;
      CRC crc = CalcCRC (buf, size-sizeof(CRC));
      CHECK (crc==right_crc, (s,"ERROR: archive structure corrupted (descriptor failed CRC check)"))
      return *this;
    }


    // Достигнут конец буфера?
    bool eof ()         {return p>=bufend;}
    // Продвинуть указатель чтения на n байтов вперёд и проверить, что мы не вышли за конец буфера :)
    void skip (int n)   {p+=n; CHECK(p<=bufend, (s,"ERROR: archive structure corrupted (bad data)"));}

    // Прочитать целое число в формате с переменной длиной
    uint64 readInteger()
    {
      uint32 x = *(uint32*)p;
           if ((x&  1)==  0)  {skip(1); return (x & ((1u<< 8)-1))>>1;}
      else if ((x&  3)==  1)  {skip(2); return (x & ((1u<<16)-1))>>2;}
      else if ((x&  7)==  3)  {skip(3); return (x & ((1u<<24)-1))>>3;}
      else if ((x& 15)==  7)  {skip(4); return (x               )>>4;}
      uint64 y = *(uint64*)p;
           if ((x& 31)== 15)  {skip(5); return (y & ((uint64(1)<<40)-1))>>5;}
      else if ((x& 63)== 31)  {skip(6); return (y & ((uint64(1)<<48)-1))>>6;}
      else if ((x&127)== 63)  {skip(7); return (y & ((uint64(1)<<56)-1))>>7;}
      else if ((x&255)==127)  {skip(8); return (y                      )>>8;}
      else                    {skip(1); uint64 y = *(uint64*)p; skip(8); return y;}
    }

    template <typename T> MEMORY_BUFFER &read (T *x)   {*x = readInteger();                       return *this;}
    template <typename T> MEMORY_BUFFER &read1(T *x)   {*x = *(uint8 *)p & ((1u<< 8)-1); skip(1); return *this;}
    template <typename T> MEMORY_BUFFER &read2(T *x)   {*x = *(uint16*)p & ((1u<<16)-1); skip(2); return *this;}
    template <typename T> MEMORY_BUFFER &read4(T *x)   {*x = *(uint32*)p               ; skip(4); return *this;}

    // Прочитать `n` значений и создать из них структурированный массив
    template <typename T> MEMORY_BUFFER &read (int n, ARRAY<T> *array)
    {
      array->resize(n);
      iterate (n, read( &((*array)[i]) ));
      return *this;
    }
    // Аналогично предыдущему, но читаются однобайтовые значения
    template <typename T> MEMORY_BUFFER &read1 (int n, ARRAY<T> *array)
    {
      array->resize(n);
      iterate (n, read1( &((*array)[i]) ));
      return *this;
    }
    // Аналогично предыдущему, но читаются четырёхбайтовые значения
    template <typename T> MEMORY_BUFFER &read4 (int n, ARRAY<T> *array)
    {
      array->resize(n);
      iterate (n, read4( &((*array)[i]) ));
      return *this;
    }

    // Прочитать из буфера кол-во элементов в массиве и затем его содержимое
    template <typename T> MEMORY_BUFFER &read( ARRAY<T> *array)
    {
      int n; read (&n);        // прочитать количество элементов в массиве
      return read (n, array);  // перейти к чтению элементов массива
    }

    MEMORY_BUFFER &read (char *x)     // Прочитать символ
    {
      *x = *(char*)p;
      skip(1);
      return *this;
    }

    MEMORY_BUFFER &read (char* *x)    // Прочитать строку
    {
      char *end = (char*) memchr( p, '\0', (uint8*)bufend - (uint8*)p);
      CHECK(end, (s,"ERROR: archive structure corrupted (bad string)"));
      *x = (char*)p;         // Прочитанная строка будет указывать непосредственно в буфер
      p = end+1;
      return *this;
    }

    MEMORY_BUFFER &read (BLOCK_DESCRIPTOR *x)    // Прочитать дескриптор блока архива
    {
      read (&x->type);
      read (&x->compressor);
      read (&x->pos);
      read (&x->origsize);
      read (&x->compsize);
      read4(&x->crc);
      return *this;
    }
};


/*****************************************************************************************************
** Локальный дескриптор блока архива, т.е. находящийся в архиве непосредственно после самого блока ***
*****************************************************************************************************/

struct LOCAL_BLOCK_DESCRIPTOR : BLOCK
{
  MEMORY_BUFFER buffer;  // Буфер, используемый для чтения дескриптора. Прочитанное значение compressor будет указывать на строку в этом буфере

  // Прочитать из архива локальный дескриптор блока
  LOCAL_BLOCK_DESCRIPTOR (MYFILE &arcfile, FILESIZE descr_pos)
  {
    FILESIZE descr_size  =  mymin (arcfile.size()-descr_pos, MAX_FOOTER_DESCRIPTOR_SIZE);
    buffer.openWithCRCAtEnd (arcfile, descr_pos, descr_size);
    uint32 sign;
    buffer.read4 (&sign );
    buffer.read  (&type );
    buffer.read  (&compressor );
    buffer.read  (&origsize );
    buffer.read  (&compsize );
    buffer.read4 (&crc );
    CHECK (sign==aSIGNATURE && origsize>0 && compsize>0 && compsize<=descr_pos, (s,"ERROR: archive structure corrupted (strange descriptor)"));
    pos = descr_pos-compsize;
    //printf("%4.4s %d %s %u %u %08x\n", &sign, type, compressor, origsize, compsize, crc);
  }
};

// Локальный дескриптор FOOTER BLOCK
struct FOOTER_BLOCK_LOCAL_DESCRIPTOR : LOCAL_BLOCK_DESCRIPTOR
{
  // Прочитать локальный дескриптор блока и выполнить дополнительные проверки, имеющие смысл только для FOOTER BLOCK
  FOOTER_BLOCK_LOCAL_DESCRIPTOR (MYFILE &arcfile, FILESIZE descr_pos)  :  LOCAL_BLOCK_DESCRIPTOR (arcfile, descr_pos)
  {
    CHECK (type==FOOTER_BLOCK, (s,"ERROR: archive structure corrupted (footer block not found)"));
  }
};

// Найти в архивном файле дескриптор FOOTER BLOCK и возвратить его позицию
FILESIZE FindFooterDescriptor (MYFILE &arcfile)
{
  char buf[MAX_FOOTER_DESCRIPTOR_SIZE];
  FILESIZE arcsize = arcfile.size();
  FILESIZE size = mymin (arcsize, MAX_FOOTER_DESCRIPTOR_SIZE);  // мы будем искать сигнатуру в последних size байтах архива
  arcfile.seek (arcsize-size);
  arcfile.read (buf, size);
  for (char *ptr=buf+size-sizeof(uint32); ; ptr--) {
    if (*(uint32*)ptr == aSIGNATURE)    return (arcsize-size)+(ptr-buf);   // Позиция в файле сигнатуры, с которой начинается дескриптор FOOTER BLOCK
    CHECK (ptr>buf, (s,"ERROR: this is not FreeArc archive or this archive is corrupt"));   // Сигнатура не найдена в последних MAX_FOOTER_DESCRIPTOR_SIZE байтах архива
  }
}


/******************************************************************************
** Информация о структуре архива (т.е. всех служебных блоках) *****************
******************************************************************************/
class ARCHIVE
{
private:
  MEMORY_BUFFER buffer;  // Буфер, хранящий содержимое FOOTER BLOCK. Уничтожается только при закрытии архива, поскольку мы используем ссылки на хранящиеся в нём данные
public:
  MYFILE arcfile;        // Файл архива. Открывается при создании ARCHIVE и закрывается при его уничтожении
  ARRAY <BLOCK_DESCRIPTOR> control_blocks_descriptors;   // Дескрипторы служебных блоков архива, читаемые из FOOTER BLOCK
  int                      arcLocked;  // Признак того, что архив закрыт от изменений
  ARRAY <char>             arcComment; // Комментарий к архиву. Может содержать нулевые символы
  FILESIZE                 SFXSize;    // Размер SFX-модуля перед архивом

  ARCHIVE () {}
  ARCHIVE (FILENAME arcname) : arcfile (arcname, READ_MODE) {}   // Открывает файл архива
  void read_structure();               // Считывает описания служебных блоков
};

// Считывает из FOOTER BLOCK описания служебных блоков
void ARCHIVE::read_structure()
{
  FILESIZE pos = FindFooterDescriptor (arcfile);            // Найти в архиве дескриптор FOOTER BLOCK
  FOOTER_BLOCK_LOCAL_DESCRIPTOR arcFooter (arcfile, pos);   // Прочитать этот дескриптор и расшифровать его
  buffer.openCompressedCheckCRC (arcFooter.compressor, arcFooter.origsize, arcfile, arcFooter.pos, arcFooter.compsize, arcFooter.crc); // Прочитать в буфер содержимое FOOTER BLOCK
  buffer.read (&control_blocks_descriptors);                // Декодировать из буфера дескрипторы служебных блоков архива
  iterate_array (i, control_blocks_descriptors) {
    control_blocks_descriptors[i].pos  =  arcFooter.pos - control_blocks_descriptors[i].pos; // Заменим относительные адреса блоков (хранящиеся как смещение относительно начала ЭТОГО блока) на абсолютные
    //printf("%d %d\n", control_blocks_descriptors[i].pos, control_blocks_descriptors[i].compsize);
  }
  SFXSize = control_blocks_descriptors[0].pos;   // всё, что находится перед первым блоком архива, можно смело считать SFX-модулем :)
  buffer.read1 (&arcLocked);                     // 1 байт: 1 - архив заблокирован от дальнейших изменений, 0 - нет
  int cmtlen;  buffer.read (&cmtlen);            // Комментарий старого образца - в UCS4
  arcComment.set (cmtlen, buffer.p);
  for (int i=0; i<cmtlen; i++)  arcComment[i] = buffer.p[i*4];
  buffer.skip (cmtlen*4);
  char *rr_settings; if (!buffer.eof())  buffer.read (&rr_settings);
  if (!buffer.eof()) {
    buffer.read (&cmtlen);                       // Комментарий кодируется как массив символов с явно заданной длиной
    if (cmtlen>0)  arcComment.set (cmtlen, buffer.p);
  }
  //printf("%d %d %*.*s\n", arcLocked, arcComment.size, arcComment.size, arcComment.size, &arcComment[0]);
}


/******************************************************************************
** Блок каталога **************************************************************
******************************************************************************/
class DIRECTORY_BLOCK
{
public:
  MYFILE &arcfile;                     // Файл архива, которому принадлежит сей славный блок каталога
private:
  MEMORY_BUFFER buffer;                // Буфер, хранящий весь каталог в бинарном виде. Раскодированные имена файлов ссылаются на этот буфер, поэтому он не удаляется до завершения работы с каталогом

  int               dirs_in_block;     // Количество каталогов, записанных в этом DIRECTORY BLOCK
  ARRAY <FILENAME>  dirs;              // Имена каталогов
  ARRAY <int>       dir_numbers;       // Номер каталога для каждого из файлов
public:
  FILENAME  dirname (int i)  {return dirs[dir_numbers[i]];}  // Имя каталога для i-го файла
  FILENAME  fullname(int i, char buffer[]);                  // Полное имя i-го файла
  int               total_files;       // Количество файлов, описанных в этом блоке каталога
  ARRAY <FILENAME>  name;              // Имена файлов (без имени каталога)
  ARRAY <FILESIZE>  size;              // Размеры файлов
  ARRAY <XFILETIME> time;              // Время модификации файлов
  ARRAY <BOOL>      isdir;             // Булевские флаги "это каталог?"
  ARRAY <CRC>       crc;               // CRC файлов

  int                       num_of_blocks;  // Кол-во блоков данных
  ARRAY <int>               num_of_files;   // Кол-во файлов в каждом блоке данных, которое после чтения заголовка заменяется на номер первого файла В СЛЕДУЮЩЕМ блоке для block_start()/block_end()
  ARRAY <BLOCK_DESCRIPTOR>  data_block;     // Описания блоков данных (компрессор, позиция в архиве, длина)

  int block_start (int block_num)  {return block_num>0? num_of_files[block_num-1] : 0;}  // Номер первого файла в блоке данных block_num
  int block_end   (int block_num)  {return num_of_files[block_num];}                     // Номер первого файла в следующем блоке данных (т.е. последнего в этом + 1)

  DIRECTORY_BLOCK (ARCHIVE &arc, BLOCK &block_info);   // Читает из архива содержимое блока каталога и декодирует его так, чтобы обеспечить быстрый доступ к описанию любого файла и любого блока данных
};

DIRECTORY_BLOCK::DIRECTORY_BLOCK (ARCHIVE &arc, BLOCK &block_info) : arcfile (arc.arcfile)
{
  // Прочитам в буфер содержимое каталога, распакуем его и проверим CRC
  CHECK (block_info.type == DIR_BLOCK, (s,"INTERNAL ERROR: must be dir block"));
  buffer.openCompressedCheckCRC (block_info.compressor, block_info.origsize, arcfile, block_info.pos, block_info.compsize, block_info.crc);

  // Прочитать общее кол-во solid-блоков и информацию о каждом из них - кол-во файлов, компрессор,
  // смещение начала solid-блока относительно блока каталога, и упакованный размер
  buffer.read  (&num_of_blocks);    buffer.read  (num_of_blocks, &num_of_files);
  ARRAY <COMPRESSOR> compressors;   buffer.read  (num_of_blocks, &compressors);
  ARRAY <FILESIZE>   offsets;       buffer.read  (num_of_blocks, &offsets);
  ARRAY <FILESIZE>   compsizes;     buffer.read  (num_of_blocks, &compsizes);

  // Реконструируем data_block[] по прочитанным данным
  data_block.setsize (num_of_blocks);
  iterate_array (i, data_block)
  {
    data_block[i].type       = DATA_BLOCK;
    data_block[i].compressor = compressors[i];
    data_block[i].pos        = block_info.pos - offsets[i];    // Вычислим абсолютный адрес блока в архиве исходя из его смещения относительно блока каталога
    data_block[i].origsize   = 0;               // А оно кому надо?
    data_block[i].compsize   = compsizes[i];
    data_block[i].crc        = 0;               // CRC блоков данных не хранится - это ни к чему
    //printf("datablock %s %d %d\n", data_block[i].compressor, data_block[i].pos, data_block[i].compsize);
  }

  // Посчитаем общее кол-во файлов в этом каталоге и изменим num_of_files[block_num] так, чтобы этот массив можно было использовать для определения файлов, принадлежащих блоку данных block_num
  total_files=0;  iterate (num_of_blocks, (total_files += num_of_files[i], num_of_files[i] = total_files));

  // Прочитаем имена каталогов и приведём символы-разделители каталогов к принятым на данной платформе
  buffer.read  (&dirs);
  iterate_array (i, dirs)    replace (dirs[i], UNSUPPORTED_PATH_DELIMITERS, PATH_DELIMITER);

  // Прочитаем информацию об отдельных файлах
  buffer.read  (total_files, &name);
  buffer.read  (total_files, &dir_numbers);
  buffer.read  (total_files, &size);
  buffer.read4 (total_files, &time);
  buffer.read1 (total_files, &isdir);
  buffer.read4 (total_files, &crc);

  //iterate( total_files, printf("%s %s %d %d\n", dirname(i), name[i], size[i], isdir[i]));
  //printf("%d files\n", total_files);
}

// Полное имя i-го файла
FILENAME DIRECTORY_BLOCK::fullname (int i, char buffer[])
{
  strcpy (buffer, dirname(i));
  if (buffer[0] != '\0')  strcat (buffer, STR_PATH_DELIMITER);
  strcat (buffer, name[i]);
  return buffer;
}

