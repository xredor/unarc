// to do: отбор файлов по именам ("name" или "dir/name"),
//        дешифрование данных/заголовка
//        добавление ".arc", listfiles/-ap/-kb

// Обработка сбоев при распаковке архива
#undef  ON_CHECK_FAIL
#define ON_CHECK_FAIL()   UnarcQuit()
void UnarcQuit();

// Доступ к структуре архива, парсингу командной строки и выполнению операций над архивом
#include "ArcStructure.h"
#include "ArcCommand.h"
#include "ArcProcess.h"

// Экстренный выход из программы в случае ошибки
void UnarcQuit()
{
  CurrentProcess->quit(FREEARC_ERRCODE_GENERAL);
}

#include "CUI.h"
CUI UI;

int main (int argc, char *argv[])
{
  UI.DisplayHeader (HEADER1 NAME);
  COMMAND command (argc, argv);    // Распарсить команду
  if (command.ok)                  // Если парсинг был удачен и можно выполнить команду
    PROCESS (&command, &UI);       //   Выполнить разобранную команду
  printf ("\n");
  return command.ok? EXIT_SUCCESS : FREEARC_EXIT_ERROR;
}
