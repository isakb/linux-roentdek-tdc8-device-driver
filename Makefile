KERNELDIR ?= /lib/modules/$(shell uname -r)/build

# Comment/uncomment the following line to disable/enable debugging
DEBUG = n

ifeq ($(DEBUG),y)
  DEBFLAGS = -Wall -O -g -DTDC_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

EXTRA_CFLAGS += $(DEBFLAGS)

obj-m := tdcmod.o
tdcmod-objs := TDC_Device.o tdc.o tdc_common.o tdc_fifo.o

.PHONY: all clean

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

TDC_Device.o: tdc_fifo.h tdc_common.h TDC_Device.h TDC_Device.c
tdc.o: tdc_common.h tdc.h tdc.c
tdc_common.o: tdc_common.h tdc_common.c
tdc_fifo.o: tdc_fifo.h tdc_fifo.c
