LINK.o = $(LINK.cc)
CXXFLAGS = -std=c++17 -g -Wall -Wno-sign-compare 
CCFLAGS = -g

LINK_OBJS = link.o mapped_file.o omf.o expr.o

# static link if using mingw32 or mingw64 to make redistribution easier.
# also add mingw directory.
ifeq ($(MSYSTEM),MINGW32)
	LINK_OBJS += mingw/err.o
	CPPFLAGS += -I mingw/
	LDLIBS += -static
endif

ifeq ($(MSYSTEM),MINGW64)
	LINK_OBJS += mingw/err.o
	CPPFLAGS += -I mingw/
	LDLIBS += -static
endif



snlink: $(LINK_OBJS)
	$(LINK.o) $^ $(LDLIBS) -o $@


link.o : link.cpp link.h sn.h
expr.o :  expr.cpp link.h sn.h
mingw/err.o : mingw/err.c mingw/err.h
