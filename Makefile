########################################################################
#
##              --- CAEN SpA - Computing Division ---
#
##   CAENDigitizer Software Project
#
##   Created  :  October    2009      (Rel. 1.0)
#
##   Auth: A. Lucchesi
#
#########################################################################
ARCH	=	`uname -m`

OUTDIR  =    	./bin/$(ARCH)/Release/
OUTNAME =    	ReadoutTest_Digitizer.bin
OUT     =    	$(OUTDIR)/$(OUTNAME)

CC	=	g++

COPTS	=	-fPIC -DLINUX -O2

#FLAGS	=	-soname -s
#FLAGS	=       -Wall,-soname -s
#FLAGS	=	-Wall,-soname -nostartfiles -s
#FLAGS	=	-Wall,-soname

CXXFLAGS	+=`root-config --cflags`
LDFLAGS 	+=`root-config --libs`

DEPLIBS	=	-lCAENDigitizer -lSpectrum -lMLP -lTreePlayer

LIBS	=	-L..

INCLUDEDIR =	-I./include

OBJS	=	src/ReadoutTest_Digitizer.o src/keyb.o src/_CAENDigitizer_DPPCIx740.o
INCLUDES =	./include/*

#########################################################################

all	:	$(OUT)

clean	:
		/bin/rm -f $(OBJS) $(OUT)

$(OUT)	:	$(OBJS)
		/bin/rm -f $(OUT)
		if [ ! -d $(OUTDIR) ]; then mkdir -p $(OUTDIR); fi
		$(CC) $(FLAGS) -o $(OUT) $(OBJS) $(DEPLIBS)  $(CXXFLAGS) $(LDFLAGS)

$(OBJS)	:	$(INCLUDES) Makefile

%.o	:	%.c
		$(CC) $(COPTS) $(INCLUDEDIR) $(CXXFLAGS) $(LDFLAGS) -c -o $@ $<

