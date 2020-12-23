EXTRA_CFLAGS = -DEXPORT_SYMTAB

obj-m := sun6i_hwspinlock_test2.o

KDIR ?= /lib/modules/$(shell uname -r)/build/
PWD = $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	

.PHONY: modules clean

-include $(KDIR)/Rules.make
