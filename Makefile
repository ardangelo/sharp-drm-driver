obj-m += sharp.o
sharp-objs += src/main.o src/drm_iface.o src/params_iface.o src/ioctl_iface.o
ccflags-y := -DDEBUG -g -std=gnu99 -Wno-declaration-after-statement

.PHONY: all clean install uninstall

# LINUX_DIR is set by Buildroot, but not if running manually
ifeq ($(LINUX_DIR),)
LINUX_DIR := /lib/modules/$(shell uname -r)/build
endif

BOOT_CONFIG_LINE := dtoverlay=sharp

all: sharp.ko sharp.dtbo

sharp.ko: src/*.c src/*.h
	$(MAKE) -C '$(LINUX_DIR)' M='$(shell pwd)' modules

sharp.dtbo: sharp.dts
	dtc -@ -I dts -O dtb -W no-unit_address_vs_reg -o $@ $<

install: sharp.ko sharp.dtbo
	# Install kernel module
	$(MAKE) -C '$(LINUX_DIR)' M='$(shell pwd)' modules_install
	# Install device tree overlay
	install -D -m 0644 sharp.dtbo /boot/overlays/
	# Add configuration line if it wasn't already there
	grep -qxF '$(BOOT_CONFIG_LINE)' /boot/config.txt \
		|| echo -e '[all]\n$(BOOT_CONFIG_LINE)' >> /boot/config.txt
	# Add auto-load module line if it wasn't already there
	grep -qxF 'sharp' /etc/modules \
		|| echo 'sharp' >> /etc/modules
	# Rebuild dependencies
	depmod -A

uninstall:
	# Remove auto-load module line and create a backup file
	sed -i.save '/sharp/d' /etc/modules
	# Remove configuration line and create a backup file
	sed -i.save '/$(BOOT_CONFIG_LINE)/d' /boot/config.txt
	# Remove device tree overlay
	rm -f /boot/overlays/sharp.dtbo

clean:
	$(MAKE) -C '$(LINUX_DIR)' M='$(shell pwd)' clean
