This directory contains SDK for development of CLS-enabled compression libraries.

Just add to simple-codec.cpp actual compression and then use one of .cmd files to
compile it with either MSVC or GCC. Then rename CLS-TEST.DLL to, say, CLS-CCM.DLL
and drop it into FreeArc's directory - FreeArc will use it as CCM compression method:

arc.exe a archive -m=ccm -t

