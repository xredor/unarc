extern "C" {
#include "C_Encryption.h"
#define LTC_NO_CIPHERS
#define   LTC_BLOWFISH
#define   LTC_RIJNDAEL
#define     ENCRYPT_ONLY
#define   LTC_TWOFISH
#define   LTC_SERPENT
#define LTC_NO_HASHES
#define   LTC_SHA1
#define   LTC_SHA512
#define LTC_NO_MATH
#define LTC_NO_TEST
#include "ciphers/aes/aes.c"
#include "ciphers/blowfish.c"
#include "ciphers/twofish/twofish.c"
#include "ciphers/serpent.c"
#include "crypt/crypt_argchk.c"
#include "crypt/crypt_cipher_descriptor.c"
#include "crypt/crypt_cipher_is_valid.c"
#include "crypt/crypt_find_cipher.c"
#include "crypt/crypt_find_hash.c"
#include "crypt/crypt_find_prng.c"
#include "crypt/crypt_hash_descriptor.c"
#include "crypt/crypt_hash_is_valid.c"
#include "crypt/crypt_prng_descriptor.c"
#include "crypt/crypt_prng_is_valid.c"
#include "crypt/crypt_register_cipher.c"
#include "crypt/crypt_register_hash.c"
#include "crypt/crypt_register_prng.c"
#include "hashes/helper/hash_memory.c"
#include "hashes/sha1.c"
#include "hashes/sha2/sha512.c"
#include "mac/hmac/hmac_done.c"
#include "mac/hmac/hmac_init.c"
#include "mac/hmac/hmac_memory.c"
#include "mac/hmac/hmac_process.c"
#include "misc/error_to_string.c"
#include "misc/pkcs5/pkcs_5_2.c"
#include "misc/zeromem.c"
#include "modes/ctr/ctr_decrypt.c"
#include "modes/ctr/ctr_done.c"
#include "modes/ctr/ctr_encrypt.c"
#include "modes/ctr/ctr_start.c"
#include "modes/cfb/cfb_decrypt.c"
#include "modes/cfb/cfb_done.c"
#include "modes/cfb/cfb_encrypt.c"
#include "modes/cfb/cfb_start.c"
#include "prngs/fortuna.c"
}


/*-------------------------------------------------*/
/* Инициализация библиотеки шифрования LibTomCrypt */
/*-------------------------------------------------*/

// Зарегистировать все включённые в программу алгоритмы
int register_all()
{
    register_cipher (&aes_enc_desc);
    register_cipher (&blowfish_desc);
    register_cipher (&serpent_desc);
    register_cipher (&twofish_desc);
    register_hash (&sha1_desc);
    register_hash (&sha512_desc);
#ifndef LTC_NO_TEST
    CHECK (blowfish_test()==CRYPT_OK, (s,"blowfish_test failed!"));
//    CHECK (rijndael_test()==CRYPT_OK, (s,"rijndael_test failed!"));
    CHECK (serpent_test ()==CRYPT_OK, (s,"serpent_test failed!"));
    CHECK (twofish_test ()==CRYPT_OK, (s,"twofish_test failed!"));
    CHECK (sha1_test    ()==CRYPT_OK, (s,"sha1_test failed!"));
    CHECK (sha512_test  ()==CRYPT_OK, (s,"sha512_test failed!"));
//    CHECK (hmac_test    ()==CRYPT_OK, (s,"hmac_test failed!"));
//    CHECK (ctr_test     ()==CRYPT_OK, (s,"ctr_test failed!"));
//    CHECK (cfb_test     ()==CRYPT_OK, (s,"cfb_test failed!"));
#endif
    return 0;
}
int call_register_all = register_all();

// Размер буфера Fortuna PRNG
int fortuna_size (void)
{
    return sizeof(prng_state);
}


/*------------------------------------------------------*/
/* Обобщённый интерфейс к режимам шифрования (CFB,CTR)  */
/*------------------------------------------------------*/

struct EncryptionMode
{
    int mode;
    symmetric_CTR ctr;
    symmetric_CFB cfb;

    EncryptionMode (int _mode) {mode = _mode;}

    char *name()
    {
        switch (mode) {
        case 0: return "ctr";
        case 1: return "cfb";
        }
    }

    int start (int cipher, BYTE *iv, BYTE *key, int keysize, int rounds)
    {
        switch (mode) {
        case 0: return ctr_start (cipher, iv, key, keysize, rounds, CTR_COUNTER_LITTLE_ENDIAN, &ctr);
        case 1: return cfb_start (cipher, iv, key, keysize, rounds, &cfb);
        }
    }

    int encrypt (BYTE *pt, BYTE *ct, int len)
    {
        switch (mode) {
        case 0: return ctr_encrypt(pt, ct, len, &ctr);
        case 1: return cfb_encrypt(pt, ct, len, &cfb);
        }
    }

    int decrypt (BYTE *pt, BYTE *ct, int len)
    {
        switch (mode) {
        case 0: return ctr_decrypt(pt, ct, len, &ctr);
        case 1: return cfb_decrypt(pt, ct, len, &cfb);
        }
    }

    int done()
    {
        switch (mode) {
        case 0: return ctr_done (&ctr);
        case 1: return cfb_done (&cfb);
        }
    }
};

// Найти номер режима шифрования по его имени
int find_mode (char *name)
{
    if (strequ(name,"ctr"))  return  0;
    if (strequ(name,"cfb"))  return  1;
    else                     return -1;
}


/*-------------------------------------------------*/
/* Пользовательские функции                        */
/*-------------------------------------------------*/

// Генерация ключа по паролю и salt с использованием numIterations итераций хеширования (PKCS5#2)
void Pbkdf2Hmac (const BYTE *pwd, int pwdSize, const BYTE *salt, int saltSize,
                 int numIterations, BYTE *key, int keySize)
{
    int hash = find_hash("sha512");
    unsigned long ulKeySize = keySize;
    pkcs_5_alg2 (pwd, pwdSize, salt, saltSize, numIterations, hash, key, &ulKeySize);
}

// Зашифровывает или расшифровывает поток данных, в зависимости от значения DoEncryption
int docrypt (enum TEncrypt DoEncryption, int cipher, int mode, BYTE *key, int keysize, int rounds, BYTE *iv,
             CALLBACK_FUNC *callback, void *auxdata)
{
    EncryptionMode encryptor(mode);
    encryptor.start (cipher, iv, key, keysize, rounds);

    int InSize = FREEARC_ERRCODE_NOT_ENOUGH_MEMORY;  // количество прочитанных байт или код ошибки
    int RemainderSize = 0;                           // необработанный остаток предыдущего блока (всегда 0 в нынешней реализации)
    BYTE* Buf = (BYTE*)malloc(LARGE_BUFFER_SIZE);    // место для данных
    if (!Buf)   goto Exit;                           // выход при нехватке памяти

    while ( (InSize = callback ("read", Buf+RemainderSize, LARGE_BUFFER_SIZE-RemainderSize, auxdata)) >= 0 )  // выход при ошибке чтения
    {
        if ((InSize+=RemainderSize)==0)     break;  // выход, если данных больше нет

        DoEncryption==ENCRYPT
          ? encryptor.encrypt(Buf, Buf, InSize)
          : encryptor.decrypt(Buf, Buf, InSize);

        int OutSize = InSize, x;
        if( (x=callback("write",Buf,OutSize,auxdata))<0 )   {InSize=x; break;}  // выход при ошибке записи
        RemainderSize = InSize-OutSize;
        // Перенесём необработанный остаток данных в начало буфера
        if (RemainderSize>0)                memmove (Buf, Buf+OutSize, RemainderSize);
    }
Exit:
    encryptor.done();
    free (Buf);
    return InSize;  // возвратим код ошибки или 0 если всё в порядке
}


/*-------------------------------------------------*/
/* Реализация класса ENCRYPTION_METHOD             */
/*-------------------------------------------------*/

// Конструктор, присваивающий параметрам метода сжатия значения по умолчанию
ENCRYPTION_METHOD::ENCRYPTION_METHOD()
{
    cipher        = -1;
    mode          = -1;
    numIterations = 1000;
    rounds        = 0;
    keySize       = -1;
    strcpy(key,  "");
    strcpy(iv,   "");
    strcpy(salt, "");
    strcpy(code, "");
}

// Универсальный метод, отвечает на запросы "encryption?", "KeySize" и "IVSize"
int ENCRYPTION_METHOD::doit (char *what, int param, void *data, CALLBACK_FUNC *callback)
{
         if (strequ (what, "encryption?"))    return 1;               // Да, это алгоритм шифрования
    else if (strequ (what, "keySize"))        return keySize;         // Возвращает размер ключа, используемого в данном методе сжатия
    else if (strequ (what, "ivSize"))         return ivSize;          // Возвращает размер InitVector, используемого в данном методе сжатия
    else if (strequ (what, "numIterations"))  return numIterations;   // Возвращает количество итераций, используемых при генерации ключа по password+salt
    else                                      return COMPRESSION_METHOD::doit (what, param, data, callback);  // Передать остальные вызовы родительской процедуре
}

// Декодирование строки, записанной в шестнадцатеричном виде, в последовательность байт
void decode16 (char *src, BYTE *dst)
{
    for( ; src[0] && src[1]; src+=2)
        *dst++ = char2int(src[0]) * 16 + char2int(src[1]);
}


// Функция распаковки
int ENCRYPTION_METHOD::decompress (CALLBACK_FUNC *callback, void *auxdata)
{
    BYTE key_bytes[MAXKEYSIZE];  decode16 (key, key_bytes);
    BYTE iv_bytes [MAXKEYSIZE];  decode16 (iv,  iv_bytes);
    return docrypt (DECRYPT, cipher, mode, key_bytes, strlen(key)/2, rounds, iv_bytes, callback, auxdata);
}

#ifndef FREEARC_DECOMPRESS_ONLY

// Функция упаковки
int ENCRYPTION_METHOD::compress (CALLBACK_FUNC *callback, void *auxdata)
{
    BYTE key_bytes[MAXKEYSIZE];  decode16 (key, key_bytes);
    BYTE iv_bytes [MAXKEYSIZE];  decode16 (iv,  iv_bytes);
    return docrypt (ENCRYPT, cipher, mode, key_bytes, strlen(key)/2, rounds, iv_bytes, callback, auxdata);
}

// Записать в buf[MAX_METHOD_STRLEN] строку, описывающую метод сжатия и его параметры (функция, обратная к parse_ENCRYPTION)
void ENCRYPTION_METHOD::ShowCompressionMethod (char *buf)
{
    sprintf (buf, "%s-%d/%s:n%d:r%d%s%s%s%s%s%s%s%s"
                                        , cipher_descriptor[cipher].name, keySize*8
                                        , EncryptionMode(mode).name()
                                        , numIterations
                                        , rounds
                                        , *key ?":k":"", key
                                        , *iv  ?":i":"", iv
                                        , *salt?":s":"", salt
                                        , *code?":c":"", code
                                        );
}

#endif  // !defined (FREEARC_DECOMPRESS_ONLY)

// Конструирует объект типа ENCRYPTION_METHOD с заданными параметрами упаковки
// или возвращает NULL, если это другой метод сжатия или допущена ошибка в параметрах
COMPRESSION_METHOD* parse_ENCRYPTION (char** parameters)
{
    int error = 0;  // Признак того, что при разборе параметров произошла ошибка

    // Делаем локальную копию, поскольку split портит строку
    char local_method[MAX_METHOD_STRLEN];
    strncopy (local_method, parameters[0], MAX_METHOD_STRLEN);

    // Разбиваем строку метода на максимум 2 части, разделённые знаком '/'
    // Это метод и режим шифрования (например, "aes/cfb")
    char *parts[3];
    split (local_method, '/', parts, 3);
    int mode    = parts[1]? find_mode(parts[1]) : 0;

    // Разбиваем строку метода на максимум 2 части, разделённые знаком '-'
    // После '-' может быть указан размер ключа в битах (например, "aes-128")
    split (local_method, '-', parts, 3);

    int cipher  = find_cipher(parts[0]);
    int keySize = parts[1]? parseInt (parts[1], &error)/8 : 0;
    if (mode<0 || cipher<0 || error)   return NULL;   // Это не метод ENCRYPTION

    ENCRYPTION_METHOD *p = new ENCRYPTION_METHOD;
    p->cipher  = cipher;
    p->mode    = mode;
    p->keySize = keySize? keySize : cipher_descriptor[cipher].max_key_length;
    p->ivSize  = cipher_descriptor[cipher].block_length;

    // Переберём все параметры метода (или выйдем раньше при возникновении ошибки при разборе очередного параметра)
    while (*++parameters && !error)
    {
      char* param = *parameters;
      switch (*param) {                    // Параметры, содержащие значения
        case 'k':  strncopy (p->key,  param+1, sizeof (p->key));    continue;
        case 'i':  strncopy (p->iv,   param+1, sizeof (p->iv));     continue;
        case 's':  strncopy (p->salt, param+1, sizeof (p->salt));   continue;
        case 'c':  strncopy (p->code, param+1, sizeof (p->code));   continue;
        case 'n':  p->numIterations  = parseInt (param+1, &error);  continue;
        case 'r':  p->rounds         = parseInt (param+1, &error);  continue;
        default :  error=1;                                         continue;
      }
    }
    if (error)  {delete p; return NULL;}  // Ошибка при парсинге параметров метода
    return p;
}

static int ENCRYPTION_x = AddCompressionMethod (parse_ENCRYPTION);   // Зарегистрируем парсер метода ENCRYPTION

