LINK.o = $(LINK.cc)
CXXFLAGS = -std=c++17 -g -Wall -Wno-sign-compare 
CCFLAGS = -g

LINK_OBJS = link.o mapped_file.o omf.o expr.o

snlink: $(LINK_OBJS)
	$(LINK.o) $^ $(LDLIBS) -o $@
