#include "cls.h"

int ClsMain (int op, CLS_CALLBACK callback, void* instance)
{
    switch(op)
    {
    case CLS_COMPRESS:
    case CLS_DECOMPRESS:
        {   
            char param[100];
            callback(instance, CLS_GET_PARAMSTR, param, 100);  // Get method parameters as single string

            const int BUFSIZE = 4096;
            char buf[BUFSIZE];
            for (int len; (len=callback(instance, CLS_PARTIAL_READ, buf, BUFSIZE)) != 0; )
            {
                if (len<0)  return len;  // Return errcode on error
                int result = callback(instance, CLS_FULL_WRITE, buf, len);
                if (result != len)  return result<0? result : CLS_ERROR_WRITE;
            }
            return CLS_OK;
        }

    default:
        return CLS_ERROR_NOT_IMPLEMENTED;
    }
}
