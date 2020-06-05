/* This is an independent implementation of the encryption algorithm:
 *
 * Serpent by Ross Anderson, Eli Biham and Lars Knudsen
 *
 * which is a candidate algorithm in the Advanced Encryption Standard
 * programme of the US National Institute of Standards and Technology
 *
 * Copyright in this implementation is held by Dr B R Gladman but I
 * hereby give permission for its free direct or derivative use subject
 * to acknowledgment of its origin and compliance with any conditions
 * that the originators of the algorithm place on its exploitation.
 *
 * Dr Brian Gladman (gladman@seven77.demon.co.uk) 14th January 1999
 */

/* modified in order to use the libmcrypt API by Nikos Mavroyanopoulos
 * All modifications are placed under the license of libmcrypt.
 */

/* modified in order to use the LibTomCrypt API by Bulat Ziganshin
 * Test vectors are imported from serpentv.dat file from Crypto++ library
 * All modifications are placed under the license of LibTomCrypt.
 */

/*
Algorithm serpent (serpent.c)

128 bit key:
Key Setup:    2366 cycles
Encrypt:       954 cycles =    26.8 mbits/sec
Decrypt:       907 cycles =    28.2 mbits/sec
Mean:          931 cycles =    27.5 mbits/sec

192 bit key:
Key Setup:    2382 cycles
Encrypt:       967 cycles =    26.5 mbits/sec
Decrypt:       915 cycles =    28.0 mbits/sec
Mean:          941 cycles =    27.2 mbits/sec

256 bit key:
Key Setup:    2360 cycles
Encrypt:       967 cycles =    26.5 mbits/sec
Decrypt:       915 cycles =    28.0 mbits/sec
Mean:          941 cycles =    27.2 mbits/sec
*/

#include "tomcrypt.h"

#ifdef LTC_SERPENT

const struct ltc_cipher_descriptor serpent_desc =
{
    "serpent",
    101,
    16, 32, 16, 0,
    &serpent_setup,
    &serpent_ecb_encrypt,
    &serpent_ecb_decrypt,
    &serpent_test,
    &serpent_done,
    &serpent_keysize,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

/* Partially optimised Serpent S Box boolean functions derived  */
/* using a recursive descent analyser but without a full search */
/* of all subtrees. This set of S boxes is the result of work   */
/* by Sam Simpson and Brian Gladman using the spare time on a   */
/* cluster of high capacity servers to search for S boxes with  */
/* this customised search engine.                               */
/*                                                              */
/* Copyright:   Dr B. R Gladman (gladman@seven77.demon.co.uk)   */
/*              and Sam Simpson (s.simpson@mia.co.uk)           */
/*              17th December 1998                              */
/*                                                              */
/* We hereby give permission for information in this file to be */
/* used freely subject only to acknowledgement of its origin    */

/* 15 terms */

#define sb0(a,b,c,d,e,f,g,h)    \
    t1 = a ^ d;     \
    t2 = a & d;     \
    t3 = c ^ t1;    \
    t6 = b & t1;    \
    t4 = b ^ t3;    \
    t10 = ~t3;      \
    h = t2 ^ t4;    \
    t7 = a ^ t6;    \
    t14 = ~t7;      \
    t8 = c | t7;    \
    t11 = t3 ^ t7;  \
    g = t4 ^ t8;    \
    t12 = h & t11;  \
    f = t10 ^ t12;  \
    e = t12 ^ t14

/* 15 terms */

#define ib0(a,b,c,d,e,f,g,h)    \
    t1 = ~a;        \
    t2 = a ^ b;     \
    t3 = t1 | t2;   \
    t4 = d ^ t3;    \
    t7 = d & t2;    \
    t5 = c ^ t4;    \
    t8 = t1 ^ t7;   \
    g = t2 ^ t5;    \
    t11 = a & t4;   \
    t9 = g & t8;    \
    t14 = t5 ^ t8;  \
    f = t4 ^ t9;    \
    t12 = t5 | f;   \
    h = t11 ^ t12;  \
    e = h ^ t14

/* 14 terms!  */

#define sb1(a,b,c,d,e,f,g,h)    \
    t1 = ~a;        \
    t2 = b ^ t1;    \
    t3 = a | t2;    \
    t4 = d | t2;    \
    t5 = c ^ t3;    \
    g = d ^ t5;     \
    t7 = b ^ t4;    \
    t8 = t2 ^ g;    \
    t9 = t5 & t7;   \
    h = t8 ^ t9;    \
    t11 = t5 ^ t7;  \
    f = h ^ t11;    \
    t13 = t8 & t11; \
    e = t5 ^ t13

/* 17 terms */

#define ib1(a,b,c,d,e,f,g,h)    \
    t1 = a ^ d;     \
    t2 = a & b;     \
    t3 = b ^ c;     \
    t4 = a ^ t3;    \
    t5 = b | d;     \
    t7 = c | t1;    \
    h = t4 ^ t5;    \
    t8 = b ^ t7;    \
    t11 = ~t2;      \
    t9 = t4 & t8;   \
    f = t1 ^ t9;    \
    t13 = t9 ^ t11; \
    t12 = h & f;    \
    g = t12 ^ t13;  \
    t15 = a & d;    \
    t16 = c ^ t13;  \
    e = t15 ^ t16

/* 16 terms */

#define sb2(a,b,c,d,e,f,g,h)    \
    t1 = ~a;        \
    t2 = b ^ d;     \
    t3 = c & t1;    \
    t13 = d | t1;   \
    e = t2 ^ t3;    \
    t5 = c ^ t1;    \
    t6 = c ^ e;     \
    t7 = b & t6;    \
    t10 = e | t5;   \
    h = t5 ^ t7;    \
    t9 = d | t7;    \
    t11 = t9 & t10; \
    t14 = t2 ^ h;   \
    g = a ^ t11;    \
    t15 = g ^ t13;  \
    f = t14 ^ t15

/* 16 terms */

#define ib2(a,b,c,d,e,f,g,h)    \
    t1 = b ^ d;     \
    t2 = ~t1;       \
    t3 = a ^ c;     \
    t4 = c ^ t1;    \
    t7 = a | t2;    \
    t5 = b & t4;    \
    t8 = d ^ t7;    \
    t11 = ~t4;      \
    e = t3 ^ t5;    \
    t9 = t3 | t8;   \
    t14 = d & t11;  \
    h = t1 ^ t9;    \
    t12 = e | h;    \
    f = t11 ^ t12;  \
    t15 = t3 ^ t12; \
    g = t14 ^ t15

/* 17 terms */

#define sb3(a,b,c,d,e,f,g,h)    \
    t1 = a ^ c;     \
    t2 = d ^ t1;    \
    t3 = a & t2;    \
    t4 = d ^ t3;    \
    t5 = b & t4;    \
    g = t2 ^ t5;    \
    t7 = a | g;     \
    t8 = b | d;     \
    t11 = a | d;    \
    t9 = t4 & t7;   \
    f = t8 ^ t9;    \
    t12 = b ^ t11;  \
    t13 = g ^ t9;   \
    t15 = t3 ^ t8;  \
    h = t12 ^ t13;  \
    t16 = c & t15;  \
    e = t12 ^ t16

/* 16 term solution that performs less well than 17 term one
   in my environment (PPro/PII)

#define sb3(a,b,c,d,e,f,g,h)    \
    t1 = a ^ b;     \
    t2 = a & c;     \
    t3 = a | d;     \
    t4 = c ^ d;     \
    t5 = t1 & t3;   \
    t6 = t2 | t5;   \
    g = t4 ^ t6;    \
    t8 = b ^ t3;    \
    t9 = t6 ^ t8;   \
    t10 = t4 & t9;  \
    e = t1 ^ t10;   \
    t12 = g & e;    \
    f = t9 ^ t12;   \
    t14 = b | d;    \
    t15 = t4 ^ t12; \
    h = t14 ^ t15
*/

/* 17 terms */

#define ib3(a,b,c,d,e,f,g,h)    \
    t1 = b ^ c;     \
    t2 = b | c;     \
    t3 = a ^ c;     \
    t7 = a ^ d;     \
    t4 = t2 ^ t3;   \
    t5 = d | t4;    \
    t9 = t2 ^ t7;   \
    e = t1 ^ t5;    \
    t8 = t1 | t5;   \
    t11 = a & t4;   \
    g = t8 ^ t9;    \
    t12 = e | t9;   \
    f = t11 ^ t12;  \
    t14 = a & g;    \
    t15 = t2 ^ t14; \
    t16 = e & t15;  \
    h = t4 ^ t16

/* 15 terms */

#define sb4(a,b,c,d,e,f,g,h)    \
    t1 = a ^ d;     \
    t2 = d & t1;    \
    t3 = c ^ t2;    \
    t4 = b | t3;    \
    h = t1 ^ t4;    \
    t6 = ~b;        \
    t7 = t1 | t6;   \
    e = t3 ^ t7;    \
    t9 = a & e;     \
    t10 = t1 ^ t6;  \
    t11 = t4 & t10; \
    g = t9 ^ t11;   \
    t13 = a ^ t3;   \
    t14 = t10 & g;  \
    f = t13 ^ t14

/* 17 terms */

#define ib4(a,b,c,d,e,f,g,h)    \
    t1 = c ^ d;     \
    t2 = c | d;     \
    t3 = b ^ t2;    \
    t4 = a & t3;    \
    f = t1 ^ t4;    \
    t6 = a ^ d;     \
    t7 = b | d;     \
    t8 = t6 & t7;   \
    h = t3 ^ t8;    \
    t10 = ~a;       \
    t11 = c ^ h;    \
    t12 = t10 | t11;\
    e = t3 ^ t12;   \
    t14 = c | t4;   \
    t15 = t7 ^ t14; \
    t16 = h | t10;  \
    g = t15 ^ t16

/* 16 terms */

#define sb5(a,b,c,d,e,f,g,h)    \
    t1 = ~a;        \
    t2 = a ^ b;     \
    t3 = a ^ d;     \
    t4 = c ^ t1;    \
    t5 = t2 | t3;   \
    e = t4 ^ t5;    \
    t7 = d & e;     \
    t8 = t2 ^ e;    \
    t10 = t1 | e;   \
    f = t7 ^ t8;    \
    t11 = t2 | t7;  \
    t12 = t3 ^ t10; \
    t14 = b ^ t7;   \
    g = t11 ^ t12;  \
    t15 = f & t12;  \
    h = t14 ^ t15

/* 16 terms */

#define ib5(a,b,c,d,e,f,g,h)    \
    t1 = ~c;        \
    t2 = b & t1;    \
    t3 = d ^ t2;    \
    t4 = a & t3;    \
    t5 = b ^ t1;    \
    h = t4 ^ t5;    \
    t7 = b | h;     \
    t8 = a & t7;    \
    f = t3 ^ t8;    \
    t10 = a | d;    \
    t11 = t1 ^ t7;  \
    e = t10 ^ t11;  \
    t13 = a ^ c;    \
    t14 = b & t10;  \
    t15 = t4 | t13; \
    g = t14 ^ t15

/* 15 terms */

#define sb6(a,b,c,d,e,f,g,h)    \
    t1 = ~a;        \
    t2 = a ^ d;     \
    t3 = b ^ t2;    \
    t4 = t1 | t2;   \
    t5 = c ^ t4;    \
    f = b ^ t5;     \
    t13 = ~t5;      \
    t7 = t2 | f;    \
    t8 = d ^ t7;    \
    t9 = t5 & t8;   \
    g = t3 ^ t9;    \
    t11 = t5 ^ t8;  \
    e = g ^ t11;    \
    t14 = t3 & t11; \
    h = t13 ^ t14

/* 15 terms */

#define ib6(a,b,c,d,e,f,g,h)    \
    t1 = ~a;        \
    t2 = a ^ b;     \
    t3 = c ^ t2;    \
    t4 = c | t1;    \
    t5 = d ^ t4;    \
    t13 = d & t1;   \
    f = t3 ^ t5;    \
    t7 = t3 & t5;   \
    t8 = t2 ^ t7;   \
    t9 = b | t8;    \
    h = t5 ^ t9;    \
    t11 = b | h;    \
    e = t8 ^ t11;   \
    t14 = t3 ^ t11; \
    g = t13 ^ t14

/* 17 terms */

#define sb7(a,b,c,d,e,f,g,h)    \
    t1 = ~c;        \
    t2 = b ^ c;     \
    t3 = b | t1;    \
    t4 = d ^ t3;    \
    t5 = a & t4;    \
    t7 = a ^ d;     \
    h = t2 ^ t5;    \
    t8 = b ^ t5;    \
    t9 = t2 | t8;   \
    t11 = d & t3;   \
    f = t7 ^ t9;    \
    t12 = t5 ^ f;   \
    t15 = t1 | t4;  \
    t13 = h & t12;  \
    g = t11 ^ t13;  \
    t16 = t12 ^ g;  \
    e = t15 ^ t16

/* 17 terms */

#define ib7(a,b,c,d,e,f,g,h)    \
    t1 = a & b;     \
    t2 = a | b;     \
    t3 = c | t1;    \
    t4 = d & t2;    \
    h = t3 ^ t4;    \
    t6 = ~d;        \
    t7 = b ^ t4;    \
    t8 = h ^ t6;    \
    t11 = c ^ t7;   \
    t9 = t7 | t8;   \
    f = a ^ t9;     \
    t12 = d | f;    \
    e = t11 ^ t12;  \
    t14 = a & h;    \
    t15 = t3 ^ f;   \
    t16 = e ^ t14;  \
    g = t15 ^ t16

#define k_xor(r,a,b,c,d)    \
    a ^= spkey->l_key[4 * r +  8]; \
    b ^= spkey->l_key[4 * r +  9]; \
    c ^= spkey->l_key[4 * r + 10]; \
    d ^= spkey->l_key[4 * r + 11]

#define k_set(r,a,b,c,d)    \
    a = spkey->l_key[4 * r +  8];  \
    b = spkey->l_key[4 * r +  9];  \
    c = spkey->l_key[4 * r + 10];  \
    d = spkey->l_key[4 * r + 11]

#define k_get(r,a,b,c,d)    \
    spkey->l_key[4 * r +  8] = a;  \
    spkey->l_key[4 * r +  9] = b;  \
    spkey->l_key[4 * r + 10] = c;  \
    spkey->l_key[4 * r + 11] = d

/* the linear transformation and its inverse    */

#define rot(a,b,c,d)    \
    a = ROL(a, 13);    \
    c = ROL(c, 3);     \
    d ^= c ^ (a << 3);  \
    b ^= a ^ c;         \
    d = ROL(d, 7);     \
    b = ROL(b, 1);     \
    a ^= b ^ d;         \
    c ^= d ^ (b << 7);  \
    a = ROL(a, 5);     \
    c = ROL(c, 22)

#define irot(a,b,c,d)   \
    c = ROR(c, 22);    \
    a = ROR(a, 5);     \
    c ^= d ^ (b << 7);  \
    a ^= b ^ d;         \
    d = ROR(d, 7);     \
    b = ROR(b, 1);     \
    d ^= c ^ (a << 3);  \
    b ^= a ^ c;         \
    c = ROR(c, 3);     \
    a = ROR(a, 13)

/* initialise the key schedule from the user supplied key   */
int serpent_setup(const unsigned char *key, int key_len, int num_rounds, symmetric_key *skey)
{
        serpent_key   *spkey  = &skey->serpent;
        const ulong32 *in_key = (ulong32 *) key;
	ulong32 i, lk, a, b, c, d, e, f, g, h;
	ulong32 t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14,
	    t15, t16;

	key_len *= 8;
	if (key_len > 256)

		return CRYPT_INVALID_KEYSIZE;

	i = 0;
	lk = (key_len + 31) / 32;

	while (i < lk) {
#ifdef ENDIAN_BIG
		spkey->l_key[i] = byteswap32(in_key[i]);
#else
		spkey->l_key[i] = (in_key[i]);
#endif
		i++;
	}

	if (key_len < 256) {
		while (i < 8)

			spkey->l_key[i++] = 0;

		i = key_len / 32;
		lk = 1 << key_len % 32;
		spkey->l_key[i] = (spkey->l_key[i] & (lk - 1)) | lk;
	}

	for (i = 0; i < 132; ++i) {
		lk = spkey->l_key[i] ^ spkey->l_key[i +
						    3] ^ spkey->l_key[i +
								      5] ^
		    spkey->l_key[i + 7] ^ 0x9e3779b9 ^ i;

		spkey->l_key[i + 8] = (lk << 11) | (lk >> 21);
	}

	k_set(0, a, b, c, d);
	sb3(a, b, c, d, e, f, g, h);
	k_get(0, e, f, g, h);
	k_set(1, a, b, c, d);
	sb2(a, b, c, d, e, f, g, h);
	k_get(1, e, f, g, h);
	k_set(2, a, b, c, d);
	sb1(a, b, c, d, e, f, g, h);
	k_get(2, e, f, g, h);
	k_set(3, a, b, c, d);
	sb0(a, b, c, d, e, f, g, h);
	k_get(3, e, f, g, h);
	k_set(4, a, b, c, d);
	sb7(a, b, c, d, e, f, g, h);
	k_get(4, e, f, g, h);
	k_set(5, a, b, c, d);
	sb6(a, b, c, d, e, f, g, h);
	k_get(5, e, f, g, h);
	k_set(6, a, b, c, d);
	sb5(a, b, c, d, e, f, g, h);
	k_get(6, e, f, g, h);
	k_set(7, a, b, c, d);
	sb4(a, b, c, d, e, f, g, h);
	k_get(7, e, f, g, h);
	k_set(8, a, b, c, d);
	sb3(a, b, c, d, e, f, g, h);
	k_get(8, e, f, g, h);
	k_set(9, a, b, c, d);
	sb2(a, b, c, d, e, f, g, h);
	k_get(9, e, f, g, h);
	k_set(10, a, b, c, d);
	sb1(a, b, c, d, e, f, g, h);
	k_get(10, e, f, g, h);
	k_set(11, a, b, c, d);
	sb0(a, b, c, d, e, f, g, h);
	k_get(11, e, f, g, h);
	k_set(12, a, b, c, d);
	sb7(a, b, c, d, e, f, g, h);
	k_get(12, e, f, g, h);
	k_set(13, a, b, c, d);
	sb6(a, b, c, d, e, f, g, h);
	k_get(13, e, f, g, h);
	k_set(14, a, b, c, d);
	sb5(a, b, c, d, e, f, g, h);
	k_get(14, e, f, g, h);
	k_set(15, a, b, c, d);
	sb4(a, b, c, d, e, f, g, h);
	k_get(15, e, f, g, h);
	k_set(16, a, b, c, d);
	sb3(a, b, c, d, e, f, g, h);
	k_get(16, e, f, g, h);
	k_set(17, a, b, c, d);
	sb2(a, b, c, d, e, f, g, h);
	k_get(17, e, f, g, h);
	k_set(18, a, b, c, d);
	sb1(a, b, c, d, e, f, g, h);
	k_get(18, e, f, g, h);
	k_set(19, a, b, c, d);
	sb0(a, b, c, d, e, f, g, h);
	k_get(19, e, f, g, h);
	k_set(20, a, b, c, d);
	sb7(a, b, c, d, e, f, g, h);
	k_get(20, e, f, g, h);
	k_set(21, a, b, c, d);
	sb6(a, b, c, d, e, f, g, h);
	k_get(21, e, f, g, h);
	k_set(22, a, b, c, d);
	sb5(a, b, c, d, e, f, g, h);
	k_get(22, e, f, g, h);
	k_set(23, a, b, c, d);
	sb4(a, b, c, d, e, f, g, h);
	k_get(23, e, f, g, h);
	k_set(24, a, b, c, d);
	sb3(a, b, c, d, e, f, g, h);
	k_get(24, e, f, g, h);
	k_set(25, a, b, c, d);
	sb2(a, b, c, d, e, f, g, h);
	k_get(25, e, f, g, h);
	k_set(26, a, b, c, d);
	sb1(a, b, c, d, e, f, g, h);
	k_get(26, e, f, g, h);
	k_set(27, a, b, c, d);
	sb0(a, b, c, d, e, f, g, h);
	k_get(27, e, f, g, h);
	k_set(28, a, b, c, d);
	sb7(a, b, c, d, e, f, g, h);
	k_get(28, e, f, g, h);
	k_set(29, a, b, c, d);
	sb6(a, b, c, d, e, f, g, h);
	k_get(29, e, f, g, h);
	k_set(30, a, b, c, d);
	sb5(a, b, c, d, e, f, g, h);
	k_get(30, e, f, g, h);
	k_set(31, a, b, c, d);
	sb4(a, b, c, d, e, f, g, h);
	k_get(31, e, f, g, h);
	k_set(32, a, b, c, d);
	sb3(a, b, c, d, e, f, g, h);
	k_get(32, e, f, g, h);

        return CRYPT_OK;
}

/* encrypt a block of text  */

int serpent_ecb_encrypt(const unsigned char *pt, unsigned char *ct, symmetric_key *skey)
{
    serpent_key *spkey = &skey->serpent;
    ulong32 a, b, c, d, e, f, g, h;
    ulong32 t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14,
        t15, t16;

    /* load input */
    LOAD32L(a,&pt[0]); LOAD32L(b,&pt[4]);
    LOAD32L(c,&pt[8]); LOAD32L(d,&pt[12]);

    k_xor(0, a, b, c, d);
    sb0(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(1, e, f, g, h);
    sb1(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(2, a, b, c, d);
    sb2(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(3, e, f, g, h);
    sb3(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(4, a, b, c, d);
    sb4(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(5, e, f, g, h);
    sb5(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(6, a, b, c, d);
    sb6(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(7, e, f, g, h);
    sb7(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(8, a, b, c, d);
    sb0(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(9, e, f, g, h);
    sb1(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(10, a, b, c, d);
    sb2(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(11, e, f, g, h);
    sb3(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(12, a, b, c, d);
    sb4(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(13, e, f, g, h);
    sb5(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(14, a, b, c, d);
    sb6(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(15, e, f, g, h);
    sb7(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(16, a, b, c, d);
    sb0(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(17, e, f, g, h);
    sb1(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(18, a, b, c, d);
    sb2(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(19, e, f, g, h);
    sb3(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(20, a, b, c, d);
    sb4(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(21, e, f, g, h);
    sb5(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(22, a, b, c, d);
    sb6(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(23, e, f, g, h);
    sb7(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(24, a, b, c, d);
    sb0(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(25, e, f, g, h);
    sb1(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(26, a, b, c, d);
    sb2(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(27, e, f, g, h);
    sb3(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(28, a, b, c, d);
    sb4(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(29, e, f, g, h);
    sb5(e, f, g, h, a, b, c, d);
    rot(a, b, c, d);
    k_xor(30, a, b, c, d);
    sb6(a, b, c, d, e, f, g, h);
    rot(e, f, g, h);
    k_xor(31, e, f, g, h);
    sb7(e, f, g, h, a, b, c, d);
    k_xor(32, a, b, c, d);

    /* store output */
    STORE32L(a, &ct[0]); STORE32L(b, &ct[4]);
    STORE32L(c, &ct[8]); STORE32L(d, &ct[12]);
    return CRYPT_OK;
}

/* decrypt a block of text  */

int serpent_ecb_decrypt(const unsigned char *ct, unsigned char *pt, symmetric_key *skey)
{
    serpent_key *spkey = &skey->serpent;
    ulong32 a, b, c, d, e, f, g, h;
    ulong32 t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14,
        t15, t16;

    /* load input */
    LOAD32L(a,&ct[0]); LOAD32L(b,&ct[4]);
    LOAD32L(c,&ct[8]); LOAD32L(d,&ct[12]);

    k_xor(32, a, b, c, d);
    ib7(a, b, c, d, e, f, g, h);
    k_xor(31, e, f, g, h);
    irot(e, f, g, h);
    ib6(e, f, g, h, a, b, c, d);
    k_xor(30, a, b, c, d);
    irot(a, b, c, d);
    ib5(a, b, c, d, e, f, g, h);
    k_xor(29, e, f, g, h);
    irot(e, f, g, h);
    ib4(e, f, g, h, a, b, c, d);
    k_xor(28, a, b, c, d);
    irot(a, b, c, d);
    ib3(a, b, c, d, e, f, g, h);
    k_xor(27, e, f, g, h);
    irot(e, f, g, h);
    ib2(e, f, g, h, a, b, c, d);
    k_xor(26, a, b, c, d);
    irot(a, b, c, d);
    ib1(a, b, c, d, e, f, g, h);
    k_xor(25, e, f, g, h);
    irot(e, f, g, h);
    ib0(e, f, g, h, a, b, c, d);
    k_xor(24, a, b, c, d);
    irot(a, b, c, d);
    ib7(a, b, c, d, e, f, g, h);
    k_xor(23, e, f, g, h);
    irot(e, f, g, h);
    ib6(e, f, g, h, a, b, c, d);
    k_xor(22, a, b, c, d);
    irot(a, b, c, d);
    ib5(a, b, c, d, e, f, g, h);
    k_xor(21, e, f, g, h);
    irot(e, f, g, h);
    ib4(e, f, g, h, a, b, c, d);
    k_xor(20, a, b, c, d);
    irot(a, b, c, d);
    ib3(a, b, c, d, e, f, g, h);
    k_xor(19, e, f, g, h);
    irot(e, f, g, h);
    ib2(e, f, g, h, a, b, c, d);
    k_xor(18, a, b, c, d);
    irot(a, b, c, d);
    ib1(a, b, c, d, e, f, g, h);
    k_xor(17, e, f, g, h);
    irot(e, f, g, h);
    ib0(e, f, g, h, a, b, c, d);
    k_xor(16, a, b, c, d);
    irot(a, b, c, d);
    ib7(a, b, c, d, e, f, g, h);
    k_xor(15, e, f, g, h);
    irot(e, f, g, h);
    ib6(e, f, g, h, a, b, c, d);
    k_xor(14, a, b, c, d);
    irot(a, b, c, d);
    ib5(a, b, c, d, e, f, g, h);
    k_xor(13, e, f, g, h);
    irot(e, f, g, h);
    ib4(e, f, g, h, a, b, c, d);
    k_xor(12, a, b, c, d);
    irot(a, b, c, d);
    ib3(a, b, c, d, e, f, g, h);
    k_xor(11, e, f, g, h);
    irot(e, f, g, h);
    ib2(e, f, g, h, a, b, c, d);
    k_xor(10, a, b, c, d);
    irot(a, b, c, d);
    ib1(a, b, c, d, e, f, g, h);
    k_xor(9, e, f, g, h);
    irot(e, f, g, h);
    ib0(e, f, g, h, a, b, c, d);
    k_xor(8, a, b, c, d);
    irot(a, b, c, d);
    ib7(a, b, c, d, e, f, g, h);
    k_xor(7, e, f, g, h);
    irot(e, f, g, h);
    ib6(e, f, g, h, a, b, c, d);
    k_xor(6, a, b, c, d);
    irot(a, b, c, d);
    ib5(a, b, c, d, e, f, g, h);
    k_xor(5, e, f, g, h);
    irot(e, f, g, h);
    ib4(e, f, g, h, a, b, c, d);
    k_xor(4, a, b, c, d);
    irot(a, b, c, d);
    ib3(a, b, c, d, e, f, g, h);
    k_xor(3, e, f, g, h);
    irot(e, f, g, h);
    ib2(e, f, g, h, a, b, c, d);
    k_xor(2, a, b, c, d);
    irot(a, b, c, d);
    ib1(a, b, c, d, e, f, g, h);
    k_xor(1, e, f, g, h);
    irot(e, f, g, h);
    ib0(e, f, g, h, a, b, c, d);
    k_xor(0, a, b, c, d);

    /* store output */
    STORE32L(a, &pt[0]); STORE32L(b, &pt[4]);
    STORE32L(c, &pt[8]); STORE32L(d, &pt[12]);
    return CRYPT_OK;
}


/**
  Performs a self-test of the Serpent block cipher
  @return CRYPT_OK if functional, CRYPT_NOP if self-test has been disabled
*/
int serpent_test(void)
{
 #ifndef LTC_TEST
    return CRYPT_NOP;
 #else
 static const struct {
     int keylen;
     unsigned char key[32], pt[16], ct[16];
 } tests[] = {
      { 16,
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0xd2, 0x9d, 0x57, 0x6f, 0xce, 0xa3, 0xa3, 0xa7, 0xed, 0x90, 0x99, 0xf2, 0x92, 0x73, 0xd7, 0x8e },
        { 0xb2, 0x28, 0x8b, 0x96, 0x8a, 0xe8, 0xb0, 0x86, 0x48, 0xd1, 0xce, 0x96, 0x06, 0xfd, 0x99, 0x2d }
      },

      { 16,
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0xd2, 0x9d, 0x57, 0x6f, 0xce, 0xa3, 0xa3, 0xa7, 0xed, 0x90, 0x99, 0xf2, 0x6d, 0x8c, 0x28, 0x71 },
        { 0x56, 0x3a, 0x84, 0x03, 0xff, 0x53, 0x09, 0xd6, 0x23, 0x70, 0xb1, 0xdc, 0xf5, 0xa1, 0x1e, 0xdd }
      },

      { 16,
        { 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 },
        { 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01 },
        { 0xd5, 0xba, 0xa0, 0x0a, 0x4b, 0xb9, 0xd8, 0xa7, 0xc9, 0x81, 0xc8, 0xdc, 0x90, 0xd8, 0x9d, 0x92 }
      },

      { 16,
        { 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 },
        { 0x14, 0x5f, 0x0b, 0x8b, 0x66, 0x31, 0x76, 0xb9, 0x5d, 0xca, 0xb7, 0xe9, 0xdc, 0xd5, 0xcc, 0x24 },
        { 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01 }
      },

      { 24,
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0xd2, 0x9d, 0x57, 0x6f, 0xce, 0xab, 0xa3, 0xa7, 0xed, 0x98, 0x99, 0xf2, 0x92, 0x7b, 0xd7, 0x8e },
        { 0x13, 0x0e, 0x35, 0x3e, 0x10, 0x37, 0xc2, 0x24, 0x05, 0xe8, 0xfa, 0xef, 0xb2, 0xc3, 0xc3, 0xe9 }
      },

      { 24,
        { 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 },
        { 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01 },
        { 0xda, 0x86, 0x08, 0x42, 0xb7, 0x20, 0x80, 0x2b, 0xf4, 0x04, 0xa4, 0xc7, 0x10, 0x34, 0x87, 0x9a }
      },

      { 24,
        { 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 },
        { 0xb2, 0x69, 0x6b, 0xd0, 0xd9, 0x8c, 0x17, 0x95, 0x3e, 0x42, 0x39, 0x22, 0x5d, 0x27, 0x20, 0x2c },
        { 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01 }
      },

      { 32,
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x92, 0x07, 0x47, 0x32, 0xd8, 0x4e, 0x18, 0x41, 0xa0, 0x13, 0xa0, 0x03, 0x4c, 0x52, 0xbf, 0x50 },
        { 0x81, 0xc4, 0xeb, 0x7b, 0x8a, 0xd9, 0xa8, 0xd0, 0xf2, 0xaa, 0x5d, 0x7b, 0xd6, 0x26, 0xb5, 0x60 }
      },

      { 32,
        { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 },
        { 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01 },
        { 0x93, 0xdf, 0x9a, 0x3c, 0xaf, 0xe3, 0x87, 0xbd, 0x99, 0x9e, 0xeb, 0xe3, 0x93, 0xa1, 0x7f, 0xca }
      }
   };


 symmetric_key key;
 unsigned char tmp[2][16];
 int err, i, y;

 for (i = 0; i < (int)(sizeof(tests)/sizeof(tests[0])); i++) {
    if ((err = serpent_setup(tests[i].key, tests[i].keylen, 0, &key)) != CRYPT_OK) {
       return err;
    }
    serpent_ecb_encrypt(tests[i].pt, tmp[0], &key);
    serpent_ecb_decrypt(tmp[0], tmp[1], &key);
    if (XMEMCMP(tmp[0], tests[i].ct, 16) != 0 || XMEMCMP(tmp[1], tests[i].pt, 16) != 0) {
#if 0
       printf("serpent failed test %d, %d, %d\n", i, XMEMCMP(tmp[0], tests[i].ct, 16), XMEMCMP(tmp[1], tests[i].pt, 16));
#endif
       return CRYPT_FAIL_TESTVECTOR;
    }
      /* now see if we can encrypt all zero bytes 1000 times, decrypt and come back where we started */
      for (y = 0; y < 16; y++) tmp[0][y] = 0;
      for (y = 0; y < 1000; y++) serpent_ecb_encrypt(tmp[0], tmp[0], &key);
      for (y = 0; y < 1000; y++) serpent_ecb_decrypt(tmp[0], tmp[0], &key);
      for (y = 0; y < 16; y++) if (tmp[0][y] != 0) return CRYPT_FAIL_TESTVECTOR;
 }
 return CRYPT_OK;
#endif
}

/** Terminate the context
   @param skey    The scheduled key
*/
void serpent_done(symmetric_key *skey)
{
}

/**
  Gets suitable key size
  @param keysize [in/out] The length of the recommended key (in bytes).  This function will store the suitable size back in this variable.
  @return CRYPT_OK if the input key size is acceptable.
*/
int serpent_keysize(int *keysize)
{
   LTC_ARGCHK(keysize);
   if (*keysize < 16)
      return CRYPT_INVALID_KEYSIZE;
   if (*keysize < 24) {
      *keysize = 16;
      return CRYPT_OK;
   } else if (*keysize < 32) {
      *keysize = 24;
      return CRYPT_OK;
   } else {
      *keysize = 32;
      return CRYPT_OK;
   }
}

#endif
