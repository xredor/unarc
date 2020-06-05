#include <stdio.h>
#include <string.h>
extern "C" {
#include "C_CLS.h"
}


/*-------------------------------------------------*/
/* Реализация класса CLS_METHOD                    */
/*-------------------------------------------------*/

int cb(void* _instance, int op, void *ptr, int n)
{
    CLS_METHOD *instance = (CLS_METHOD*)_instance;
    switch(op)
    {
    case CLS_GET_PARAMSTR:
        strcpy((char*)ptr, instance->params);      //// add overflow checking!
        return CLS_OK;
    case CLS_FULL_READ:                            // FA read/write operations are full
    case CLS_PARTIAL_READ:
        return instance->callback("read", ptr, n, instance->auxdata);
    case CLS_FULL_WRITE:
    case CLS_PARTIAL_WRITE:
        return instance->callback("write", ptr, n, instance->auxdata);
    case CLS_MALLOC:
        *(void**)ptr = malloc(n);
        return *(void**)ptr? CLS_OK : CLS_ERROR_NOT_ENOUGH_MEMORY;
    case CLS_FREE:
        free(ptr);
        return CLS_OK;
    default:
        return CLS_ERROR_NOT_IMPLEMENTED;
    }
}

// Функция распаковки
int CLS_METHOD::decompress (CALLBACK_FUNC *_callback, void *_auxdata)
{
    callback = _callback;
    auxdata  = _auxdata;
    return ClsMain(CLS_DECOMPRESS, cb, this);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int CLS_METHOD::compress (CALLBACK_FUNC *_callback, void *_auxdata)
{
    callback = _callback;
    auxdata  = _auxdata;
    return ClsMain(CLS_COMPRESS, cb, this);
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_CLS)
void CLS_METHOD::ShowCompressionMethod (char *buf)
{
    if (strequ(params,""))
      then strcpy (buf, name);
      else sprintf(buf, "%s:%s", name, params);
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)


// ПОДДЕРЖКА ПРОИЗВОЛЬНЫХ ВНЕШНИХ УПАКОВЩИКОВ **********************************************************************

// Конструирует объект типа CLS_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка в параметрах
COMPRESSION_METHOD* parse_CLS (char** parameters, void *method_template)
{
  if (strequ (parameters[0], ((CLS_METHOD*)method_template)->name)) {
    // Если название метода (нулевой параметр) соответствует названию проверяемого CLS метода, то разберём остальные параметры
    CLS_METHOD *p = new CLS_METHOD (*(CLS_METHOD*)method_template);

    // Record method parameters if exists
    if (parameters[1])
        strcpy(p->params, parameters[1]);

    return p;
  } else {
    return NULL;   // Это не метод CLS
  }
}


// Add CLS-enabled compressors from cls-*.dll
int AddClsCompressors()
{
    int registered_methods = 0;

#ifdef FREEARC_WIN  // Non-Windows platforms aren't yet supported
    // Get program's executable filename
    char path[MY_FILENAME_MAX];
    GetModuleFileNameA(NULL, path, MY_FILENAME_MAX);

    // Replace basename part with "cls-*.dll"
    char *basename = strrchr(path,'\\')+1;
    strcpy(basename, "cls-*.dll");

    // Find all cls-*.dll from program's directory
    WIN32_FIND_DATAA FindData;
    HANDLE ff = FindFirstFileA(path, &FindData);
    for(BOOL found = (ff!=INVALID_HANDLE_VALUE); found; found = FindNextFileA(ff, &FindData))
    {
        // Put full DLL filename into `path`
        strcpy(basename, FindData.cFileName);

        // If DLL contains ClsMain() function - register it as dll-based compressor
        HMODULE dll = LoadLibraryA(path);
        CLS_MAIN *ClsMain = (CLS_MAIN*) GetProcAddress (dll, "ClsMain");
        if (ClsMain)
        {
            str_end(path)[-4] = '\0';
            strlwr(basename);
            CLS_METHOD *cls = new CLS_METHOD(basename+4, ClsMain);
            AddExternalCompressionMethod (parse_CLS, cls);
            registered_methods++;
        }
    }
#endif
    return registered_methods;
}


// Register all DLL-based compression algorithms
static int CLS_x = AddClsCompressors();

