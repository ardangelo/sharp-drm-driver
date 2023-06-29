obj-m += sharp.o
sharp-objs += main.o drm_iface.o params_iface.o ioctl_iface.o

export KROOT=/lib/modules/$(shell uname -r)/build

all:  modules

modules modules_install clean::
	@$(MAKE) -C $(KROOT) M=$(shell pwd) $@

clean::
	rm -rf   Module.symvers modules.order
