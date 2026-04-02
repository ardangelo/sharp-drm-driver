obj-m += sharp-drm.o
sharp-drm-objs += src/main.o src/drm_iface.o src/params_iface.o src/ioctl_iface.o
ccflags-y := -g -Wno-declaration-after-statement

.PHONY: all clean install uninstall install_modules install_aux

ifeq ($(KERNELRELEASE),)
KERNELRELEASE := $(shell uname -r)
endif

ifeq ($(LINUX_DIR),)
LINUX_DIR := /lib/modules/$(KERNELRELEASE)/build
endif

all:
	$(MAKE) -C '$(LINUX_DIR)' M='$(shell pwd)' modules

install_modules:
	$(MAKE) -C '$(LINUX_DIR)' M='$(shell pwd)' modules_install
	depmod -a

install: install_modules install_aux

install_aux:
	@grep -qxF 'sharp-drm' /etc/modules || echo 'sharp-drm' >> /etc/modules

uninstall:
	@sed -i.save '/sharp-drm/d' /etc/modules

clean:
	$(MAKE) -C '$(LINUX_DIR)' M='$(shell pwd)' clean
