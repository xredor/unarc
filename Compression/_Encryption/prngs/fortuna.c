/* LibTomCrypt, modular cryptographic library -- Tom St Denis
 *
 * LibTomCrypt is a library that provides various cryptographic
 * algorithms in a highly modular and flexible manner.
 *
 * The library is free for all purposes without any express
 * guarantee it works.
 *
 * Tom St Denis, tomstdenis@gmail.com, http://libtom.org
 */
#include "tomcrypt.h"

/**
  @file fortuna.c
  Fortuna PRNG, Tom St Denis
*/

/* Implementation of Fortuna by Tom St Denis

We deviate slightly here for reasons of simplicity [and to fit in the API].  First all "sources"
in the AddEntropy function are fixed to 0.  Second since no reliable timer is provided
we reseed automatically when len(pool0) >= 64 or every LTC_FORTUNA_WD calls to the read function */

#ifdef LTC_FORTUNA

/* requries LTC_SHA512 and AES  */
#if !(defined(LTC_RIJNDAEL) && defined(LTC_SHA512))
   #error LTC_FORTUNA requires LTC_SHA512 and LTC_RIJNDAEL (AES)
#endif

#ifndef LTC_FORTUNA_POOLS
   #warning LTC_FORTUNA_POOLS was not previously defined (old headers?)
   #define LTC_FORTUNA_POOLS 32
#endif

#if LTC_FORTUNA_POOLS < 4 || LTC_FORTUNA_POOLS > 32
   #error LTC_FORTUNA_POOLS must be in [4..32]
#endif

const struct ltc_prng_descriptor fortuna_desc = {
    "fortuna", 1024,
    &fortuna_start,
    &fortuna_add_entropy,
    &fortuna_ready,
    &fortuna_read,
    &fortuna_done,
    NULL,
    NULL,
    NULL
};

/* update the IV */
static void fortuna_update_iv(prng_state *prng)
{
   int            x;
   unsigned char *IV;
   /* update IV */
   IV = prng->fortuna.IV;
   for (x = 0; x < BLOCK_SIZE; x++) {
      IV[x] = (IV[x] + 1) & 255;
      if (IV[x] != 0) break;
   }
}

/* reseed the PRNG */
static int fortuna_reseed(prng_state *prng)
{
   unsigned char tmp[MAXBLOCKSIZE];
   hash_state    md;
   int           err, x;

   ++prng->fortuna.reset_cnt;

   /* new K == LTC_SHA512(K || s) where s == LTC_SHA512(P0) || LTC_SHA512(P1) ... */
   sha512_init(&md);
   if ((err = sha512_process(&md, prng->fortuna.K, K_SIZE)) != CRYPT_OK) {
      sha512_done(&md, tmp);
      return err;
   }

   for (x = 0; x < LTC_FORTUNA_POOLS; x++) {
       if (x == 0 || ((prng->fortuna.reset_cnt >> (x-1)) & 1) == 0) {
          /* terminate this hash */
          if ((err = sha512_done(&prng->fortuna.pool[x], tmp)) != CRYPT_OK) {
             sha512_done(&md, tmp);
             return err;
          }
          /* add it to the string */
          if ((err = sha512_process(&md, tmp, HASH_SIZE)) != CRYPT_OK) {
             sha512_done(&md, tmp);
             return err;
          }
          /* reset this pool */
          if ((err = sha512_init(&prng->fortuna.pool[x])) != CRYPT_OK) {
             sha512_done(&md, tmp);
             return err;
          }
       } else {
          break;
       }
   }

   /* finish key */
   if ((err = sha512_done(&md, prng->fortuna.K)) != CRYPT_OK) {
      return err;
   }
   if ((err = rijndael_enc_setup(prng->fortuna.K, KEY_SIZE, 0, &prng->fortuna.skey)) != CRYPT_OK) {
      return err;
   }
   fortuna_update_iv(prng);

   /* reset pool len */
   prng->fortuna.pool0_len = 0;
   prng->fortuna.wd        = 0;


#ifdef LTC_CLEAN_STACK
   zeromem(&md, sizeof(md));
   zeromem(tmp, sizeof(tmp));
#endif

   return CRYPT_OK;
}

/**
  Start the PRNG
  @param prng     [out] The PRNG state to initialize
  @return CRYPT_OK if successful
*/
int fortuna_start(prng_state *prng)
{
   int err, x, y;
   unsigned char tmp[MAXBLOCKSIZE];

   LTC_ARGCHK(prng != NULL);

   /* initialize the pools */
   for (x = 0; x < LTC_FORTUNA_POOLS; x++) {
       if ((err = sha512_init(&prng->fortuna.pool[x])) != CRYPT_OK) {
          for (y = 0; y < x; y++) {
              sha512_done(&prng->fortuna.pool[y], tmp);
          }
          return err;
       }
   }
   prng->fortuna.pool_idx = prng->fortuna.pool0_len = prng->fortuna.wd = 0;
   prng->fortuna.reset_cnt = 0;

   /* reset bufs */
   zeromem(prng->fortuna.K, K_SIZE);
   if ((err = rijndael_enc_setup(prng->fortuna.K, KEY_SIZE, 0, &prng->fortuna.skey)) != CRYPT_OK) {
      for (x = 0; x < LTC_FORTUNA_POOLS; x++) {
          sha512_done(&prng->fortuna.pool[x], tmp);
      }
      return err;
   }
   zeromem(prng->fortuna.IV, BLOCK_SIZE);

   LTC_MUTEX_INIT(&prng->fortuna.prng_lock)

   return CRYPT_OK;
}

/**
  Add entropy to the PRNG state
  @param in       The data to add
  @param inlen    Length of the data to add
  @param prng     PRNG state to update
  @return CRYPT_OK if successful
*/
int fortuna_add_entropy(const unsigned char *in, unsigned long inlen, prng_state *prng)
{
   unsigned char tmp[2];
   int           err;

   LTC_ARGCHK(in  != NULL);
   LTC_ARGCHK(prng != NULL);

   /* ensure inlen <= HASH_SIZE */
   while (inlen > HASH_SIZE) {
      int res = fortuna_add_entropy (in, HASH_SIZE, prng);
      if (res != CRYPT_OK)   return res;
      in    += HASH_SIZE;
      inlen -= HASH_SIZE;
   }

   LTC_MUTEX_LOCK(&prng->fortuna.prng_lock);

   /* add s || length(in) || in to pool[pool_idx] */
   tmp[0] = 0;
   tmp[1] = (unsigned char)inlen;
   if ((err = sha512_process(&prng->fortuna.pool[prng->fortuna.pool_idx], tmp, 2)) != CRYPT_OK) {
      LTC_MUTEX_UNLOCK(&prng->fortuna.prng_lock);
      return err;
   }
   if ((err = sha512_process(&prng->fortuna.pool[prng->fortuna.pool_idx], in, inlen)) != CRYPT_OK) {
      LTC_MUTEX_UNLOCK(&prng->fortuna.prng_lock);
      return err;
   }
   if (prng->fortuna.pool_idx == 0) {
      prng->fortuna.pool0_len += inlen;
   }
   if (++(prng->fortuna.pool_idx) == LTC_FORTUNA_POOLS) {
      prng->fortuna.pool_idx = 0;
   }

   LTC_MUTEX_UNLOCK(&prng->fortuna.prng_lock);
   return CRYPT_OK;
}

/**
  Make the PRNG ready to read from
  @param prng   The PRNG to make active
  @return CRYPT_OK if successful
*/
int fortuna_ready(prng_state *prng)
{
   return fortuna_reseed(prng);
}

/**
  Read from the PRNG
  @param out      Destination
  @param outlen   Length of output
  @param prng     The active PRNG to read from
  @return Number of octets read
*/
unsigned long fortuna_read(unsigned char *out, unsigned long outlen, prng_state *prng)
{
   unsigned char tmp[BLOCK_SIZE];
   int           err, i;
   unsigned long tlen;

   LTC_ARGCHK(out  != NULL);
   LTC_ARGCHK(prng != NULL);

   LTC_MUTEX_LOCK(&prng->fortuna.prng_lock);

   /* do we have to reseed? */
   if (++prng->fortuna.wd == LTC_FORTUNA_WD || prng->fortuna.pool0_len >= 64) {
      if ((err = fortuna_reseed(prng)) != CRYPT_OK) {
         LTC_MUTEX_UNLOCK(&prng->fortuna.prng_lock);
         return 0;
      }
   }

   /* now generate the blocks required */
   tlen = outlen;

   /* handle whole blocks without the extra XMEMCPY */
   while (outlen >= BLOCK_SIZE) {
      /* encrypt the IV and store it */
      rijndael_enc_ecb_encrypt(prng->fortuna.IV, out, &prng->fortuna.skey);
      out += BLOCK_SIZE;
      outlen -= BLOCK_SIZE;
      fortuna_update_iv(prng);
   }

   /* left over bytes? */
   if (outlen > 0) {
      rijndael_enc_ecb_encrypt(prng->fortuna.IV, tmp, &prng->fortuna.skey);
      XMEMCPY(out, tmp, outlen);
      fortuna_update_iv(prng);
   }

   /* generate new key */
   for (i=0; i+BLOCK_SIZE <= KEY_SIZE; i+=BLOCK_SIZE) {
      rijndael_enc_ecb_encrypt(prng->fortuna.IV, prng->fortuna.K+i, &prng->fortuna.skey); fortuna_update_iv(prng);
   }
   if ((err = rijndael_enc_setup(prng->fortuna.K, KEY_SIZE, 0, &prng->fortuna.skey)) != CRYPT_OK) {
      LTC_MUTEX_UNLOCK(&prng->fortuna.prng_lock);
      return 0;
   }

#ifdef LTC_CLEAN_STACK
   zeromem(tmp, sizeof(tmp));
#endif
   LTC_MUTEX_UNLOCK(&prng->fortuna.prng_lock);
   return tlen;
}

/**
  Terminate the PRNG
  @param prng   The PRNG to terminate
  @return CRYPT_OK if successful
*/
int fortuna_done(prng_state *prng)
{
   int           err, x;
   unsigned char tmp[HASH_SIZE];

   LTC_ARGCHK(prng != NULL);
   LTC_MUTEX_LOCK(&prng->fortuna.prng_lock);

   /* terminate all the hashes */
   for (x = 0; x < LTC_FORTUNA_POOLS; x++) {
       if ((err = sha512_done(&(prng->fortuna.pool[x]), tmp)) != CRYPT_OK) {
          LTC_MUTEX_UNLOCK(&prng->fortuna.prng_lock);
          return err;
       }
   }
   /* call cipher done when we invent one ;-) */

#ifdef LTC_CLEAN_STACK
   zeromem(tmp, sizeof(tmp));
#endif

   LTC_MUTEX_UNLOCK(&prng->fortuna.prng_lock);
   return CRYPT_OK;
}

#endif


/* $Source: /cvs/libtom/libtomcrypt/src/prngs/fortuna.c,v $ */
/* $Revision: 1.16 $ */
/* $Date: 2007/05/12 14:32:35 $ */
