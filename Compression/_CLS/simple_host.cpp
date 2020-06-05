#include <string.h>
#include <stdlib.h>
#include <io.h>

#include "cls.h"
char **save_argv;

int callback(void* instance, int op, void *ptr, int n)
{
    switch(op)
    {
    case CLS_GET_PARAMSTR:
        strncpy((char*)ptr, save_argv[1]? save_argv[1] : "", n);
        ((char*)ptr)[n-1] = '\0';
        return CLS_OK;
    case CLS_FULL_READ:
    case CLS_PARTIAL_READ:
        return read(0,ptr,n);
    case CLS_FULL_WRITE:
    case CLS_PARTIAL_WRITE:
        return write(1,ptr,n);
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

int main (int, char **argv)
{
    save_argv = argv;

    ClsMain(CLS_INIT, callback, NULL);
    int result = ClsMain(CLS_COMPRESS, callback, NULL);
    ClsMain(CLS_DONE, callback, NULL);

    return result;
}
