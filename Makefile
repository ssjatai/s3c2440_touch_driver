obj-m += touch.o

KDIR := /home/linxiaodong/下载/linux-2.6.22.6

PWD ?= $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules
	