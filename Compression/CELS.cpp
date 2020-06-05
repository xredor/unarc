#include <stdio.h>
#include "CELS.h"

namespace CELS
{

// ***********************************************************************************************************************
// Реализация класса COMPRESSION_METHOD                                                                                  *
// ***********************************************************************************************************************

int COMPRESSION_METHOD::server()
{
  char *service = p._str("service");

  // Global services
  if (strequ (service, "register"))               return FREEARC_OK; //to do: Register();

  // Invocation-specific services
  if (strequ (service, "decompress"))             {parse_method(); return decompress (p._callback("callback"), p);}
#ifndef FREEARC_DECOMPRESS_ONLY
  if (strequ (service, "compress"))               {parse_method(); return compress   (p._callback("callback"), p);}
  if (strequ (service, "canonize"))               {parse_method();                                     char a[1000]; ShowCompressionMethod(a); return p._return(a);}
  if (strequ (service, "SetCompressionMem"))      {parse_method(); SetCompressionMem  (p._int("mem")); char a[1000]; ShowCompressionMethod(a); return p._return(a);}
  if (strequ (service, "SetDictionary"))          {parse_method(); SetDictionary      (p._int("mem")); char a[1000]; ShowCompressionMethod(a); return p._return(a);}
  if (strequ (service, "SetBlockSize"))           {parse_method(); SetBlockSize       (p._int("mem")); char a[1000]; ShowCompressionMethod(a); return p._return(a);}
  if (strequ (service, "SetDecompressionMem"))    {parse_method(); SetDecompressionMem(p._int("mem")); char a[1000]; ShowCompressionMethod(a); return p._return(a);}
  if (strequ (service, "GetCompressionMem"))      {parse_method(); return p._return (GetCompressionMem());}
  if (strequ (service, "GetDictionary"))          {parse_method(); return p._return (GetDictionary());}
  if (strequ (service, "GetBlockSize"))           {parse_method(); return p._return (GetBlockSize());}
#endif
  if (strequ (service, "GetDecompressionMem"))    {parse_method(); return p._return (GetDecompressionMem());}

  return FREEARC_ERRCODE_NOT_IMPLEMENTED;
}


// ****************************************************************************************************************************
// МЕТОД "СЖАТИЯ" STORING *****************************************************************************************************
// ****************************************************************************************************************************

// Функция "(рас)паковки", копирующая данные один в один
int copy_data (CALLBACK_FUNC *callback, void *auxdata)
{
  char buf[BUFFER_SIZE]; int len, errcode;
  for(;;)
  {
    READ_LEN_OR_EOF(len, buf, BUFFER_SIZE);
    WRITE(buf, len);
  }
finished:
  return errcode;
}

// Реализация метода "сжатия" STORING
struct STORING_METHOD : COMPRESSION_METHOD
{
  // Конструктор, присваивающий параметрам метода значения по умолчанию
  STORING_METHOD (TABI_ELEMENT* params) : COMPRESSION_METHOD(params) {}
  // Функции распаковки и упаковки
  virtual int decompress (CALLBACK_FUNC *callback, void *auxdata)    {return copy_data (callback, auxdata);}
#ifndef FREEARC_DECOMPRESS_ONLY
  virtual int compress   (CALLBACK_FUNC *callback, void *auxdata)    {return copy_data (callback, auxdata);}

  // Разбирает строку с параметрами метода
  virtual void parse_method()
  {
    if (!strequ (p._str("method"), "storing"))
      throw "STORING_METHOD:parse_method";
  }

  // Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия (функция, обратная к parse_method)
  virtual void ShowCompressionMethod (char *buf)   {sprintf (buf, "storing");}

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

// Function that represents STORING compression method
int storing_server (TABI_ELEMENT* params)
{
  return STORING_METHOD(params).server();
}

// Register STORING method in CELS
int storing_register = CELS_Register(storing_server);

}  // namespace CELS


// ****************************************************************************************************************************
// ПОДДЕРЖКА ТАБЛИЦЫ ЗАРЕГИСТРИРОВАННЫХ МЕТОДОВ СЖАТИЯ И ПОИСК В ЭТОЙ ТАБЛИЦЕ РЕАЛИЗАЦИИ ЧИСТО КОНКРЕТНОГО МЕТОДА *************
// ****************************************************************************************************************************

// Кол-во зарегистрированных методов сжатия и таблица, куда они заносятся
int methodsCount = 0;
TABI_FUNCTION* methodsTable[MAX_COMPRESSION_METHODS];

// Compression method registration
int CELS_Register (TABI_FUNCTION *method)
{
  CHECK (methodsCount < elements(methodsTable), (s,"INTERNAL ERROR: Overflow of compression methods table"));
  int result = (*method)(TABI_DYNAMAP("service","register"));
  if (result==FREEARC_OK)
    methodsTable[methodsCount++] = method;
  return result;
}

// Central CELS function that provides all CELS services
int CELS_Call (TABI_ELEMENT* params)
{
  TABI_MAP p(params); p.dump();
  char *service = p._str("service");

  // ==== AUTO-SERVICES =====================================
  // Ignore zero mem parameter to Set* calls
  if (start_with (service, "Set") && isupper(service[3]))
    if (p._int("mem",1)==0)
      return p._return(p._str("method"));

  // Limit* = Set* if Get*>limit
  if (start_with (service, "Limit") && isupper(service[5]))                 return p._return(p._str("method"));   // to do: get & set
/*
  {
    char new_service[MAX_METHOD_STRLEN];
    sprintf(new_service, "Get%s", service+5);
    MemSize mem = TABI_callret(CELS_Call, TABI_DYNAMAP(p) ("service", new_service));
    if (mem <= p._longlong("mem"))         // if method already uses less memory than specified limit
      return p._return(p._str("method"));  // then return method unmodified
    sprintf(new_service, "Set%s", service+5);
    return TABI_call(CELS_Call, TABI_DYNAMAP(p) ("service", new_service));
  }
*/
  if (strequ (service, "encryption?"))               return 1;                             // to do: aes-specific
  if (start_with (p._str("method",""), "aes"))       return p._return(p._str("method"));   // to do: aes-specific
  //default: GetBlockSize        {return 0;}
  //default: SetBlockSize        {}
  // ========================================================

  // Find appropriate method to service this call
  for (int i=0; i<methodsCount; i++)
  {
    try
    {
      int x = methodsTable[i](params);
      if (x!=FREEARC_ERRCODE_NOT_IMPLEMENTED)
        return x;
    }
    catch (...)
    {
    }
  }
  return FREEARC_ERRCODE_NOT_IMPLEMENTED;
}

// Temporary
extern "C" {
void tabi_dump(TABI_ELEMENT *params, int n=0)
{
	TABI_MAP(params).dump(n);
}
}

