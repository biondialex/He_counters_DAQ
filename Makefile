CXX           =
ObjSuf        = o
SrcSuf        = cxx
ExeSuf        =
DllSuf        = so
OutPutOpt     = -o # keep whitespace after "-o"

ROOTCFLAGS   := $(shell root-config --cflags)
ROOTLDFLAGS  := $(shell root-config --ldflags)
ROOTLIBS     := $(shell root-config --libs)
ROOTGLIBS    := $(shell root-config --glibs)
HASTHREAD    := $(shell root-config --has-thread)

CXX           = g++
CXXFLAGS      = -O -Wall -fPIC
LD            = g++
LDFLAGS       = -O
SOFLAGS       = -shared

CXXFLAGS     += $(ROOTCFLAGS)
LDFLAGS      += $(ROOTLDFLAGS)
LIBS          = $(ROOTLIBS) $(SYSLIBS) 
GLIBS         = $(ROOTGLIBS) $(SYSLIBS) -lspcm_linux

#------------------------------------------------------------------------------

SOURCES     := $(wildcard *.$(SrcSuf)) MyMainFrameDict.$(SrcSuf)
OBJECTS     := $(SOURCES:.$(SrcSuf)=.$(ObjSuf))
RUNGUI        = SPECT_GUI$(ExeSuf)

OBJS          = $(OBJECTS)

PROGRAMS      = $(RUNGUI)

.SUFFIXES: .$(SrcSuf) .$(ObjSuf) .$(DllSuf)

all:            $(PROGRAMS)

$(RUNGUI):      $(OBJECTS)
		$(LD) $(LDFLAGS) $^ $(GLIBS) $(OutPutOpt)$@
		@echo "$@ done"

.$(SrcSuf).$(ObjSuf):
	$(CXX) $(CXXFLAGS) -c $<

MyMainFrameDict.$(SrcSuf): MyMainFrame.h MyConfigFrame.h LinkDef.h
		@echo "Generating dictionary $@..."
		@rootcint -f $@ -c $^

clean:
		rm -f *.o *.pcm *Dict.* $(RUNGUI)

distclean:		clean
		rm -f $(PROGRAMS)


