#pragma once

#include <stdio.h>

class CUI : public BASEUI
{
private:
  char outdir[MY_FILENAME_MAX*4];  //unicode: utf-8 encoding
  uint64 total_files, total_bytes, total_packed;
public:
  void DisplayHeader (char* header);
  bool ProgressFile  (bool isdir, const char *operation, FILENAME filename, uint64 filesize);
  void EndProgress   ();
  bool AllowProcessing (char cmd, int silent, FILENAME arcname, char* comment, int cmtsize, FILENAME outdir);
  FILENAME GetOutDir();
  char AskOverwrite (FILENAME filename, uint64 size, time_t modified);

  void ListHeader (COMMAND &);
  void ListFooter (COMMAND &);
  void ListFiles (DIRECTORY_BLOCK *, COMMAND &);
};

void CUI::DisplayHeader (char* header)
{
  printf ("%s", header);
}

bool CUI::ProgressFile (bool isdir, const char *operation, FILENAME filename, uint64 filesize)
{
  printf (isdir?  "%s %s" STR_PATH_DELIMITER "\n"  :  "%s %s (%llu bytes)\n",
          operation, filename, filesize);
  return TRUE;
}

void CUI::EndProgress()
{
  printf ("All OK");
}

FILENAME CUI::GetOutDir()
{
  return outdir;
}

bool CUI::AllowProcessing (char cmd, int silent, FILENAME arcname, char* comment, int cmtsize, FILENAME _outdir)
{
  strcpy (outdir, _outdir);
  printf (". %s archive: %s\n",                       // Выведем имя обрабатываемого архива
    cmd=='l'||cmd=='v'? "Listing" : cmd=='t' ? "Testing" : "Extracting", drop_dirname(arcname));
  if (cmtsize>0)                                      // Выведем архивный комментарий
#ifdef FREEARC_WIN
{
    // Convert comment from UTF-8 to OEM encoding before printing
    char *oemname = (char*) malloc(cmtsize+1);
    strncpy (oemname, comment, cmtsize);
    oemname[cmtsize] = 0;
    utf8_to_oem (oemname, oemname);
    printf ("%s\n", oemname);
    free (oemname);
}
#else
    printf("%*.*s\n", cmtsize, cmtsize, comment);
#endif

#ifdef FREEARC_SFX
  // В SFX необходимо запросить согласие пользователя перед началом распаковки
  if (!silent)
  {
    char answer[256];
    printf ("Continue extraction (y/n)? ");
    fgets (answer, 256, stdin);
    if (! (strequ(answer,"y") || strequ(answer,"Y")))
    {
      printf ("Extraction aborted!\n");
      return FALSE;
    }
    printf("\n");
  }
#endif
  return TRUE;
}

char CUI::AskOverwrite (FILENAME filename, uint64 size, time_t modified)
{
  char help[] = "Valid answers: Y - yes, N - no, A - overwrite all, S - skip all, Q - quit\n";
  again: printf ("Overwrite %s (y/n/a/s/q) ? ", filename);
  char answer[256];  fgets(answer, 256, stdin);  *answer = tolower(*answer);
  if (strlen(answer)!=1 || !strchr("ynasq", *answer))  {printf (help);  goto again;}
  if (*answer=='q') {printf ("Extraction aborted\n");  exit(1);}
  return *answer;
}


/******************************************************************************
** Реализация команды получения листинга архива *******************************
******************************************************************************/
void CUI::ListHeader (COMMAND &command)
{
  if (command.cmd=='l')
      printf ("Date/time                  Size Filename\n"
              "----------------------------------------\n");
  else
      printf ("Date/time              Attr            Size          Packed      CRC Filename\n"
              "-----------------------------------------------------------------------------\n");
  total_files=total_bytes=total_packed=0;
}

void CUI::ListFooter (COMMAND &command)
{
  if (command.cmd=='l')
      printf ("----------------------------------------\n");
  else
      printf ("-----------------------------------------------------------------------------\n");
  printf ("%.0lf files, %.0lf bytes, %.0lf compressed", double(total_files), double(total_bytes), double(total_packed));
}

void CUI::ListFiles (DIRECTORY_BLOCK *dirblock, COMMAND &command)
{
  int  b=0;                // current_data_block
  bool Encrypted = FALSE;  // текущий солид-блок зашифрован?
  uint64 packed=0;
  iterate_var (i, dirblock->total_files) {
    // Увеличим номер солид-блока если мы вышли за последний принадлежащий ему файл
    if (i >= dirblock->block_end(b))
      b++;
    // Если это первый файл в солид-блоке - соберём block-related информацию
    if (i == dirblock->block_start(b))
    { // Запишем на первый файл в блоке весь его упакованный размер
      packed = dirblock->data_block[b].compsize;
      // Запомним информацию о солид-блоке для использования её со всеми файлами из этого солид-блока
      char *c = dirblock->data_block[b].compressor;
      Encrypted = strstr (c, "+aes-")!=NULL || strstr (c, "+serpent-")!=NULL || strstr (c, "+blowfish-")!=NULL || strstr (c, "+twofish-")!=NULL;
    }


    if (command.accept_file (dirblock, i)) { //   Если этот файл требуется обработать
      unsigned long long filesize = dirblock->size[i];
      char timestr[100];  FormatDateTime (timestr, 100, dirblock->time[i]);

      if (command.cmd=='l')
          printf (dirblock->isdir[i]? "%s       -dir-" : "%s %11.0lf", timestr, double(filesize));
      else
          printf ("%s %s %15.0lf %15.0lf %08x", timestr, dirblock->isdir[i]? ".D.....":".......", double(filesize), double(packed), dirblock->crc[i]);
      printf ("%c", Encrypted? '*':' ');

      // Print filename using console encoding
      static char filename[MY_FILENAME_MAX*4];
      dirblock->fullname (i, filename);
      static MYFILE file;  file.setname (filename);
      printf ("%s\n", file.displayname());

      total_files++;
      total_bytes  += filesize;
      total_packed += packed;    packed = 0;
    }
  }
}

