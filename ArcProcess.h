#pragma once

/******************************************************************************
** Процесс выполнения команды *************************************************
******************************************************************************/
class PROCESS
{
public:
  COMMAND *cmd;            // Выполняемая команда
  BASEUI  *UI;
  ARCHIVE arcinfo;

  // Переменные, отражающие состояние процесса чтения входных данных
  MYFILE *infile;          // Файл архива, из которого идёт чтение
  FILESIZE bytes_left;     // Кол-во байт, которое осталось прочитать до исчерпания упакованных данных этого солид-блока

  // Переменные, отражающие состояние процесса записи распакованных данных
  DIRECTORY_BLOCK *dir;     // Каталог, которому принадлежат распаковываемые файлы
  int curfile;              //   Номер в каталоге текущего распаковываемого файла
  BOOL included;            //   Текущий файл включён в обработку или мы просто пропускаем его?
  int extractUntil;         //   Номер последнего файла, который нужно извлечь из этого солид-блока
  MYFILE outfile;           // Файл, извлекаемый из архива
  char fullname[MY_FILENAME_MAX*4]; // Полное имя распаковываемого сейчас файла
  FILESIZE bytes_to_write;  // Сколько байт в текущем файле осталось записать
  FILESIZE writtenBytes;    // Сколько байт всего было распаковано в текущем архиве
  FILESIZE archive_pos;     // Текущая позиция в архиве
  CRC crc;                  // CRC данных, записанных в файл
  enum PASS {FIRST_PASS, SECOND_PASS};  // Первый/второй проход по солид-блоку (первый - распаковка каталогов и пустых файлов, второй - всех остальных)

  // Методы
  void outfile_open (PASS pass);                         // Открыть очередной выходной файл и напечатать сообщение о его распаковке
  void outfile_write (void *buf, int size);              // Записать данные в выходной файл
  void outfile_close();                                  // Закрыть выходной файл
  int  DecompressCallback (const char *what, void *buf, int size);  // Callback-функция чтения/записи для распаковщика

  // Распаковать или протестировать файлы из солид-блока с номером block_num каталога dirblock
  void ExtractFiles (DIRECTORY_BLOCK *dirblock, int block_num);

  // Получить информацию об архиве
  PROCESS (COMMAND* _cmd, BASEUI* _UI, uint64 &total_files, uint64 &origsize, uint64 &compsize);

  // Читает структуру архива и вызывает в зависимости от выполняемой команды
  // ListFiles для каждого блока каталога или ExtractFiles для каждого солид-блока
  PROCESS(COMMAND* _cmd, BASEUI* _UI);

  // Процедура экстренного выхода
  void quit(int errcode);
} *CurrentProcess;


/*************************************************************************************************
** Нижеследующие процедуры предоставляют абстрактные средства работы с текущим выходным файлом, **
** скрывающие такие детали, как различия команд e/x/t, различие между каталогами и файлами,     **
** и то, что часть файлов может быть исключена из обработки                                     **
*************************************************************************************************/

// Открыть очередной выходной файл и напечатать сообщение о его распаковке
void PROCESS::outfile_open (PASS pass)
{
  crc = INIT_CRC;
  bytes_to_write = dir->size[curfile];
  if (pass==SECOND_PASS && bytes_to_write==0)
    return;  // Directories and empty files were extracted in first pass
  included = cmd->accept_file (dir, curfile);
  char *xname = cmd->cmd=='e'? dir->name[curfile]
                             : dir->fullname (curfile, fullname);
  outfile.setname (xname);

  if (included && cmd->cmd!='t')
    if (dir->isdir[curfile])
      {if (cmd->cmd!='e')  BuildPathTo (outfile.filename), create_dir (outfile.filename);}
    else
      {if (outfile.exists())
       {
         if (cmd->no)  included = FALSE;
         else if (!cmd->yes)
         {
           char answer = UI->AskOverwrite (outfile.displayname(), dir->size[curfile], dir->time[curfile]);
           switch (answer)
           {
             case 'y': break;
             case 'n': included = FALSE;  break;
             case 'a': cmd->yes = TRUE;   break;
             case 's': cmd->no  = TRUE;   included = FALSE;  break;
             case 'q': quit(FREEARC_ERRCODE_OPERATION_TERMINATED);
           }
         }
       }
       if (included)  outfile.open (WRITE_MODE);}

  if (pass==FIRST_PASS || dir->size[curfile]>0)   // Не писать повторно о распаковке каталогов/пустых файлов
    if (!(dir->isdir[curfile] && cmd->cmd!='x'))  // Не сообщать о тестировании каталогов ;)
      if (!UI->ProgressFile (dir->isdir[curfile], included? (cmd->cmd=='t'? "Testing":"Extracting"):"Skipping", MYFILE(xname).displayname(), bytes_to_write))
        quit(FREEARC_ERRCODE_OPERATION_TERMINATED);
}

// Записать данные в выходной файл
void PROCESS::outfile_write (void *buf, int size)
{
  crc = UpdateCRC (buf, size, crc);
  if (included && cmd->cmd!='t' && size)
    outfile.write(buf,size);
  if (!UI->ProgressWrite (writtenBytes += size))  quit(FREEARC_ERRCODE_OPERATION_TERMINATED);
}

// Закрыть выходной файл
void PROCESS::outfile_close()
{
  if (included)
  {
    CHECK ((crc^INIT_CRC) == dir->crc[curfile], (s,"ERROR: file %s failed CRC check", outfile.utf8name));
    if (cmd->cmd!='t' && !dir->isdir[curfile])
      outfile.close();
      outfile.SetFileDateTime (dir->time[curfile]);
  }
  included = FALSE;
}


/******************************************************************************
** Реализация команд распаковки и тестирования архивов ************************
******************************************************************************/

// Callback-функция чтения/записи для распаковщика
int PROCESS::DecompressCallback (const char *what, void *buf, int size)
{
  if (strequ (what, "read")) {
    int read_bytes = mymin (bytes_left, size);
    if (read_bytes==0)  return 0;
    if (!UI->ProgressRead (archive_pos))  quit(FREEARC_ERRCODE_OPERATION_TERMINATED);
    int len = infile->tryRead (buf, read_bytes);
    if (len>0)  bytes_left -= len,  archive_pos += len;
    return len;

  } else if (strequ (what, "write")) {
    int origsize = size;
    if (curfile > extractUntil)  return FREEARC_ERRCODE_NO_MORE_DATA_REQUIRED;   // Нам попался тупой распаковщик, не способный завершить распаковку по требованию :(
    while (size>0) {
      int n = mymin (bytes_to_write, size);   // Записываем сколько осталось до конца файла или
      outfile_write (buf,n);                  // сколько осталось данных в буфере - смотря что меньше
      bytes_to_write -= n;
      if (bytes_to_write==0) {                // Если файл записан до конца - перейдём к следующему
        outfile_close();
        if (++curfile > extractUntil)  return FREEARC_ERRCODE_NO_MORE_DATA_REQUIRED;   // Если все файлы, которые мы должны распаковать из этого блока, уже извлечены, то попросить распаковщик завершить распаковку
        outfile_open(SECOND_PASS);
      }
      buf=(uint8*)buf+n; size-=n;
    }
    return origsize;     // Сигнализировать успешную запись и попросить продолжить распаковку

  } else return FREEARC_ERRCODE_NOT_IMPLEMENTED;
}

// Add "tempfile" to compressors chain if required
char *AddTempfile (char *compressor)
{
  char *c = (compressor = strdup_msg(compressor));
  int  compressor_len = strlen(compressor);
  char *buffering = "tempfile";
  char PLUS[] = {COMPRESSION_METHODS_DELIMITER, '\0'};

  // Разобьём компрессор на отдельные алгоритмы и посчитаем расход памяти
  CMETHOD  cm[MAX_METHODS_IN_COMPRESSOR];
  uint64 memi[MAX_METHODS_IN_COMPRESSOR];
  int N = split (compressor, COMPRESSION_METHODS_DELIMITER, cm, MAX_METHODS_IN_COMPRESSOR);
  uint64 mem = 0;
  for (int i=0; i<N; i++)
    mem += memi[i] = GetDecompressionMem(cm[i]);

  // Maximum memory allowed to use
  uint64 maxmem = mymin (GetPhysicalMemory()/4*3, GetMaxMemToAlloc());

  // If memreqs are too large - add "tempfile" between methods
  if (mem > maxmem)
  {
    compressor = (char*) malloc_msg (compressor_len + (strlen(buffering)+strlen(PLUS))*(N-1) + 1);

    strcpy(compressor, cm[0]);
    mem=memi[0];

    for (int i=1; i<N; i++)
    {
      // If total memreqs of methods after last tempfile >maxmem - add one more tempfile occurence
      if (mem>0 && mem+memi[i]>maxmem)
      {
        strcat (compressor, PLUS);
        strcat (compressor, buffering);
        mem = 0;
      }
      strcat (compressor, PLUS);
      strcat (compressor, cm[i]);
      mem += memi[i];
    }
    free(c);  // we can't free c earlier since its space used by cm[i]
    return compressor;
  }

  free(c);
  return NULL;
}

int global_callback (const char *what, void *buf, int size, void *auxdata)
{
  return ((PROCESS*)auxdata) -> DecompressCallback (what, buf, size);
}

// Распаковать или протестировать файлы из солид-блока с номером block_num каталога dirblock
void PROCESS::ExtractFiles (DIRECTORY_BLOCK *dirblock, int block_num)
{
  dir = dirblock;
  BLOCK& data_block (dirblock->data_block [block_num]);
  extractUntil = -1;                        // В эту переменную будет записан номер последнего файла в солид-блоке, который нужно обработать
  // Переберём все файлы в этом блоке
  for (curfile = dirblock->block_start(block_num); curfile < dirblock->block_end(block_num); curfile++) {
    if (cmd->accept_file (dirblock, curfile))           // Если этот файл требуется обработать
    {
      if (dir->size[curfile]==0) {   // то если это каталог или пустой файл - сделаем это сразу
        outfile_open (FIRST_PASS);
        outfile_close(); }
      else
        extractUntil = curfile;      // а иначе - запомним, что нужно распаковать блок как минимум до этого файла
    }
  }
  if (extractUntil >= 0) {                       // Если в этом блоке нашлось что распаковывать - значит, распакуем! :)
    infile = &dirblock->arcfile;                 //   Архивный файл
    infile->seek (archive_pos = data_block.pos); //   Начало данных солид-блока в архиве
    bytes_left = data_block.compsize;            //   Размер упакованных данных в солид-блоке
    curfile = dirblock->block_start (block_num); // Номер первого файла в этом солид-блоке
    outfile_open (SECOND_PASS);                  // Откроем первый выходной файл
    char *compressor = AddTempfile (data_block.compressor);  // Добавим "tempfile" между компрессорами если не хватает памяти для распаковки
    int result = MultiDecompress (compressor? compressor : data_block.compressor, global_callback, this);
    CHECK (result!=FREEARC_ERRCODE_INVALID_COMPRESSOR, (s,"ERROR: unsupported compression method %s", data_block.compressor));
    CHECK (result>=0 || result==FREEARC_ERRCODE_NO_MORE_DATA_REQUIRED, (s,"ERROR: archive data corrupted (decompression fails)"));
    free (compressor);
    outfile_close();                             // Закроем последний выходной файл
  }
}


/******************************************************************************
** Получить информацию об архиве **********************************************
******************************************************************************/

PROCESS::PROCESS (COMMAND* _cmd, BASEUI* _UI, uint64 &total_files, uint64 &origsize, uint64 &compsize) : cmd(_cmd), UI(_UI)
{
  CurrentProcess = this;
  SetCompressionThreads (GetProcessorsCount());

  arcinfo.arcfile.open (cmd->arcname, READ_MODE);                     // Откроем файл архива
  arcinfo.read_structure();                                           // Прочитаем структуру архива
  total_files = origsize = compsize = 0;

  iterate_array (i, arcinfo.control_blocks_descriptors) {             // Переберём все служебные блоки в архиве...
    BLOCK& block_descriptor = arcinfo.control_blocks_descriptors[i];
    if (block_descriptor.type == DIR_BLOCK) {                         // ... и отберём из них блоки каталога
      DIRECTORY_BLOCK dirblock (arcinfo, block_descriptor);           // Прочитаем блок каталога
      iterate_var (i, dirblock.total_files)                           // Переберём все файлы в каталоге
      	origsize += dirblock.size[i];
      iterate_array (i, dirblock.data_block)                          // Переберём все солид-блоки в каталоге
      	compsize += dirblock.data_block[i].compsize;
      total_files += dirblock.total_files;
    }
  }
}


/******************************************************************************
** Головная процедура выполнения команды над архивом **************************
******************************************************************************/

// Читает структуру архива и вызывает в зависимости от выполняемой команды
// ListFiles для каждого блока каталога или ExtractFiles для каждого солид-блока
PROCESS::PROCESS (COMMAND* _cmd, BASEUI* _UI) : cmd(_cmd), UI(_UI)
{
  CurrentProcess = this;
  cmd->Prepare();

  arcinfo.arcfile.open (cmd->arcname, READ_MODE);                     // Откроем файл архива
  arcinfo.read_structure();                                           // Прочитаем структуру архива
  // Выведем заголовок операции на экран и запросим у пользователя разрешение на распаковку SFX
  if (!UI->AllowProcessing (cmd->cmd, cmd->silent, MYFILE(cmd->arcname).displayname(), &arcinfo.arcComment[0], arcinfo.arcComment.size, cmd->outpath.utf8name)) {
    cmd->ok = FALSE;  return;
  }
  if (cmd->cmd!='t')  outfile.SetBaseDir (UI->GetOutDir());

  writtenBytes = 0;
  if (cmd->list_cmd())     UI->ListHeader (*cmd);
  else                     UI->BeginProgress (arcinfo.arcfile.size());
  iterate_array (i, arcinfo.control_blocks_descriptors) {             // Переберём все служебные блоки в архиве...
    BLOCK& block_descriptor = arcinfo.control_blocks_descriptors[i];
    if (block_descriptor.type == DIR_BLOCK) {                         // ... и отберём из них блоки каталога
      DIRECTORY_BLOCK dirblock (arcinfo, block_descriptor);           // Прочитаем блок каталога
      if (cmd->list_cmd())                                            // Если это команда получения листинга
        UI->ListFiles (&dirblock, *cmd);                              //   то выполним её
      else
        iterate_array (i, dirblock.data_block)                        //   иначе - переберём все солид-блоки в каталоге
          ExtractFiles (&dirblock, i);                                //     и для каждого из них выполним процедуру тестирования/распаковки
    }
  }
  if (cmd->list_cmd())  UI->ListFooter (*cmd);
  else                  UI->EndProgress (cmd);
}


// Процедура экстренного выхода
void PROCESS::quit(int errcode)
{
  cmd->ok = FALSE;
  if (outfile.isopen())  outfile.close(), outfile.remove();
  arcinfo.arcfile.tryClose();
  compressionLib_cleanup();
  UI->Abort (cmd, errcode);
}


