http://www.haskell.org/bz/mm.zip
http://www.encode.ru/forums/index.php?action=vthread&forum=1&topic=443

v1.1 (2007-09-11) TTA: fixed problems with processing 24/32-bit samples
                  TTA: improved compression by 1-2%
                  MM:  fixed error with processing 'offset'

v1.0 (2007-05-22) Initial version


I am glad to present the first open-source multimedia compression library
which allows to add MM compresion features to any open-source compressor
or archiver. The library is provided under GPL license and includes 3 modules:

MMDET - multimedia detection. This module allows to examine raw chunk of data
and determine number of channels and width of data samples. Returned result may
be, for example, "skip 2 bytes at start of file and encode rest of data as 6
channels of 24-bit values". Floating-point values and values with non-intel
byte order are not recognized. MMDET module relies on comparison of applying
order-0 model to original data and data after MM preprocessing (substraction of
successive values). You are also provided with results of lz+order0 compression
which allows you to decide whether it has meaning to use MM compression at all.
Additionally, recognition by file header is available, although at this moment
only .WAV file headers are supported.

TTA - this module based on TrueAudio 2.0 (lossless audio codec developed by
Alexander Djourik and Pavel Zhilin, homepage http://www.true-audio.com/).
MMDET module (both WAV header method and entropy tests) is used to determine
structure of wave file. Like an original TTA codec, 8, 16 and 24 bits integer
and 32-bit floating-point samples are supported.

MM - simple multimedia data preprocessor which does not reduce data size
but only substracts successive values. Running grzip or ppmd on its output
allows to provide compression ratio comparable with that of rar, uharc and sbc.
It uses MMDET module to determine structure of data being compressed.



All modules include main() driver which allows to compile them as standalone
applications. Use .bat files for this purpose, while provided makefile shows
how to compile the library as part of larger application. You should also know
that MMDET standalone program displays its suggestion about selection of optimal
MM model based on simple criterion - which model gives the smaller output, while
TTA/MM modules use MMDET library with smarter criterion (which is slightly
different in MM and TTA), which allows to recognize high-precision samples
pretty reliably (MMDET in this case may suggest, for example, using
4*8bit model instead of 1*32). You can test MM/TTA autodetection features even
with WAV files by using -s switch.

Thanks to Christian Maeder and Uwe Herklotz for their help in development of
MMDET module; thanks to Radek 'Black_Fox' Liska for english translation of this
readme
