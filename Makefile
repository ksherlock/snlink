LINK.o = $(LINK.cc)
CXXFLAGS = -std=c++17 -g -Wall -Wno-sign-compare 
CCFLAGS = -g

LINK_OBJS = link.o sn.o mapped_file.o omf.o expr.o set_file_type.o afp/libafp.a
NM_OBJS = nm.o sn.o mapped_file.o

# static link if using mingw32 or mingw64 to make redistribution easier.
# also add mingw directory.
ifeq ($(MSYSTEM),MINGW32)
	LINK_OBJS += mingw/err.o
	NM_OBJS += mingw/err.o
	CPPFLAGS += -I mingw/
	LDLIBS += -static
endif

ifeq ($(MSYSTEM),MINGW64)
	LINK_OBJS += mingw/err.o
	CPPFLAGS += -I mingw/
	LDLIBS += -static
endif

.PHONY: all
all: sn-link sn-nm

.PHONY: clean
clean:
	$(RM) sn-link sn-asm $(LINK_OBJS) $(NM_OBJS)
	$(MAKE) -C afp clean

sn-link: $(LINK_OBJS)
	$(LINK.o) $^ $(LDLIBS) -o $@

sn-nm: $(NM_OBJS)
	$(LINK.o) $^ $(LDLIBS) -o $@


.PHONY: subdirs
subdirs :
	$(MAKE) -C afp

afp/libafp.a : subdirs

set_file_type.o : CPPFLAGS += -I afp/include
set_file_type.o : set_file_type.cpp

link.o : link.cpp sn.h
nm.o : nm.cpp sn.h
expr.o :  expr.cpp sn.h
omf.o : omf.cpp omf.h
sn.o : sn.cpp sn.h
mingw/err.o : mingw/err.c mingw/err.h
