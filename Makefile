
CROSS_COMPILE = arm-linux-gnueabihf-

TOPDIR = $(shell pwd)
#include
INCS += -I$(TOPDIR)/include
#lib
LIBS += -L$(TOPDIR)/lib
LIBS += -lcdc_vencoder
LIBS += -lcdc_ve
LIBS += -lcdc_memory
LIBS += -lcdc_base
LIBS += -lpthread

DEFS = -D DEBUG

export CFLAGS += \
	$(INCS)\
	$(DEFS)\
	-std=c++11

all: camera

camera: mksrc
	$(CROSS_COMPILE)g++ src/libsrc.o -o target/$@ $(LIBS)

mksrc:
	make CROSS_COMPILE=$(CROSS_COMPILE) -C src/

.PHONY clean:
clean:
	make -C src/ clean