
CFILES = $(wildcard *.cpp) 
COBJS = $(patsubst %.cpp, %.o, $(CFILES))

OBJS += $(COBJS)

libsrc.o : $(OBJS)
	$(CROSS_COMPILE)ld -r $^ -o $@

%.o : %.cpp
	$(CROSS_COMPILE)g++ $< -c -o $@ $(CFLAGS)

.PHONY : clean
clean : 
	rm *.o -f