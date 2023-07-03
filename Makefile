LINK.o = $(LINK.cc)
CXXFLAGS = -std=c++14 -g -Wall -Wno-sign-compare 
CCFLAGS = -g

snlink: $(LINK_OBJS)
	$(LINK.o) $^ $(LDLIBS) -o $@
