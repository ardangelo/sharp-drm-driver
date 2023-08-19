obj-m += sharp-drm.o
sharp-drm-objs += src/main.o src/drm_iface.o src/params_iface.o src/ioctl_iface.o
ccflags-y := -g -std=gnu99 -Wno-declaration-after-statement

dtb-y += sharp-drm.dtbo

targets += $(dtbo-y)
always  := $(dtbo-y)

.PHONY: all clean install uninstall

# LINUX_DIR is set by Buildroot, but not if running manually
ifeq ($(LINUX_DIR),)
LINUX_DIR := /lib/modules/$(shell uname -r)/build
endif

# BUILD_DIR is set by DKMS, but not if running manually
ifeq ($(BUILD_DIR),)
BUILD_DIR := .
endif

BOOT_CONFIG_LINE := dtoverlay=sharp-drm
BOOT_CMDLINE_ADD := console=tty2 fbcon=font:VGA8x8 fbcon=map:10

all:
	$(MAKE) -C '$(LINUX_DIR)' M='$(shell pwd)'

install_modules:
	$(MAKE) -C '$(LINUX_DIR)' M='$(shell pwd)' modules_install
	# Rebuild dependencies
	depmod -A

install: install_modules install_aux

# Separate rule to be called from DKMS
install_aux:
	# Install device tree overlay
	install -D -m 0644 $(BUILD_DIR)/sharp-drm.dtbo /boot/overlays/
	# Add configuration line if it wasn't already there
	grep -qxF '$(BOOT_CONFIG_LINE)' /boot/config.txt \
		|| printf '[all]\ndtparam=spi=on\n$(BOOT_CONFIG_LINE)\n' >> /boot/config.txt
	# Add auto-load module line if it wasn't already there
	grep -qxF 'sharp-drm' /etc/modules \
		|| echo 'sharp-drm' >> /etc/modules
	# Configure fbcon for display
	grep -qxF '$(BOOT_CMDLINE_ADD)' /boot/cmdline.txt \
		|| sed -i.save 's/$$/ $(BOOT_CMDLINE_ADD)/' /boot/cmdline.txt
	# Rebuild dependencies
	depmod -A

uninstall:
	# Remove fbcon configuration and create a backup file
	sed -i.save 's/ $(BOOT_CMDLINE_ADD)//' /boot/cmdline.txt
	# Remove auto-load module line and create a backup file
	sed -i.save '/sharp-drm/d' /etc/modules
	# Remove configuration line and create a backup file
	sed -i.save '/$(BOOT_CONFIG_LINE)/d' /boot/config.txt
	# Remove device tree overlay
	rm -f /boot/overlays/sharp-drm.dtbo

clean:
	$(MAKE) -C '$(LINUX_DIR)' M='$(shell pwd)' clean
