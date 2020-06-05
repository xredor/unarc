CXX=g++

DEFINES  = -DFREEARC_UNIX -DFREEARC_INTEL_BYTE_ORDER -DFREEARC_64BIT

Extractor_DEFINES = -DFREEARC_DECOMPRESS_ONLY -D_NO_EXCEPTIONS -DUNARC
OBJDIR  = ./
LINKOBJ_FAR_PLUGIN = $(OBJDIR)/Environment.o $(OBJDIR)/Common.o $(OBJDIR)/CompressionLibrary.o \
                     $(OBJDIR)/C_LZMA.o
LINKOBJ_TINY = $(LINKOBJ_FAR_PLUGIN) $(OBJDIR)/C_BCJ.o $(OBJDIR)/C_Dict.o $(OBJDIR)/C_Delta.o
LINKOBJ_MINI = $(LINKOBJ_TINY) $(OBJDIR)/C_REP.o $(OBJDIR)/C_LZP.o $(OBJDIR)/C_PPMD.o $(OBJDIR)/C_External.o
LINKOBJ = $(LINKOBJ_MINI) $(OBJDIR)/C_MM.o $(OBJDIR)/C_TTA.o \
          $(OBJDIR)/C_Tornado.o $(OBJDIR)/C_GRZip.o

UNARC = ArcStructure.h ArcCommand.h ArcProcess.h
CUI = CUI.h
HEADERS =  Compression/Compression.h Compression/Common.h

UNIX_LINK_FLAGS = -L$(LIBDIR) -lstdc++ -lrt -lpthread -s


CODE_FLAGS  = -fno-exceptions -fno-rtti -Wall \
              -Wno-unknown-pragmas -Wno-sign-compare -Wno-conversion
OPT_FLAGS   = -ffast-math \
              -fomit-frame-pointer -fstrict-aliasing \
              -fforce-addr
DEBUG_FLAGS = -g0
CFLAGS = $(CODE_FLAGS) $(OPT_FLAGS) -O2 $(DEBUG_FLAGS) $(DEFINES) $(Extractor_DEFINES)
CFLAGS1= $(CODE_FLAGS) $(OPT_FLAGS) -O1 $(DEBUG_FLAGS) $(DEFINES) $(Extractor_DEFINES)


unarc: $(OBJDIR)/unarc.o $(LINKOBJ)
	$(CXX) $< $(LINKOBJ) $(UNIX_LINK_FLAGS) -o $@

clean:
	rm -rf unarc *.o

$(OBJDIR)/unarc.o: unarc.cpp $(UNARC) $(CUI) $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<

$(OBJDIR)/Environment.o: Environment.cpp Environment.h $(HEADERS)
	$(CXX) -c $(CFLAGS1) -o $*.o $<

$(OBJDIR)/CompressionLibrary.o: Compression/CompressionLibrary.cpp $(HEADERS)
	$(CXX) -c $(CFLAGS1) -o $*.o $<

$(OBJDIR)/Common.o: Compression/Common.cpp $(HEADERS)
	$(CXX) -c $(CFLAGS1) -o $*.o $<

$(OBJDIR)/C_External.o: Compression/External/C_External.cpp Compression/External/C_External.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<


$(OBJDIR)/C_BCJ.o: Compression/LZMA2/C_BCJ.cpp Compression/LZMA2/C_BCJ.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<

$(OBJDIR)/C_LZMA.o: Compression/LZMA2/C_LZMA.cpp Compression/LZMA2/C_LZMA.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<

$(OBJDIR)/C_Dict.o: Compression/Dict/C_Dict.cpp Compression/Dict/C_Dict.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<

$(OBJDIR)/C_Delta.o: Compression/Delta/C_Delta.cpp Compression/Delta/C_Delta.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<

$(OBJDIR)/C_REP.o: Compression/REP/C_REP.cpp Compression/REP/C_REP.h Compression/REP/rep.cpp $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<

$(OBJDIR)/C_LZP.o: Compression/LZP/C_LZP.cpp Compression/LZP/C_LZP.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<

$(OBJDIR)/C_PPMD.o: Compression/PPMD/C_PPMD.cpp Compression/PPMD/C_PPMD.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -O1 -o $*.o $<

$(OBJDIR)/C_MM.o: Compression/MM/C_MM.cpp Compression/MM/C_MM.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<

$(OBJDIR)/C_TTA.o: Compression/MM/C_TTA.cpp Compression/MM/C_TTA.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<

$(OBJDIR)/C_Tornado.o: Compression/Tornado/C_Tornado.cpp Compression/Tornado/C_Tornado.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<

$(OBJDIR)/C_GRZip.o: Compression/GRZip/C_GRZip.cpp Compression/GRZip/C_GRZip.h $(HEADERS)
	$(CXX) -c $(CFLAGS) -o $*.o $<
