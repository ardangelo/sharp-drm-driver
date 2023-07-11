obj-m += sharp.o
sharp-objs += main.o drm_iface.o params_iface.o ioctl_iface.o

export KROOT=/lib/modules/$(shell uname -r)/build

all:  modules sharp.dtbo

modules modules_install clean::
	@$(MAKE) -C $(KROOT) M=$(shell pwd) $@

modules_install:: sharp.dtbo
	@$(MAKE) -C $(KROOT) M=$(shell pwd) $@
	cp sharp.dtbo /boot/overlays

sharp.dtbo: sharp.dts
	dtc -@ -I dts -O dtb -o sharp.dtbo sharp.dts

clean::
	rm -rf   Module.symvers modules.order
