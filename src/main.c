// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DRM driver for 2.7" Sharp Memory LCD
 *
 * Copyright 2023 Andrew D'Angelo
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#include "drm_iface.h"
#include "params_iface.h"
#include "ioctl_iface.h"

// QEMU mode: write display data to a serial device instead of SPI
static char *qemu_display_dev = NULL;
module_param(qemu_display_dev, charp, 0444);
MODULE_PARM_DESC(qemu_display_dev, "Serial device for QEMU display output (e.g. /dev/ttyS2)");

static struct platform_device *qemu_pdev = NULL;

static int sharp_memory_probe(struct spi_device *spi)
{
	int ret;

	printk(KERN_INFO "sharp_memory: entering sharp_memory_probe\n");

	if ((ret = drm_probe(spi))) {
		return ret;
	}

	if ((ret = params_probe())) {
		return ret;
	}

	if ((ret = ioctl_probe())) {
		return ret;
	}

	printk(KERN_INFO "sharp_memory: successful probe\n");

	return 0;
}

static void sharp_memory_remove(struct spi_device *spi)
{
	drm_clear_overlays();

	ioctl_remove();
	params_remove();
	drm_remove(spi);
}

static void sharp_memory_shutdown(struct spi_device *spi)
{
	sharp_memory_remove(spi);
}

static struct spi_driver sharp_memory_spi_driver = {
	.driver = {
		.name = "sharp-drm",
	},
	.probe = sharp_memory_probe,
	.remove = sharp_memory_remove,
	.shutdown = sharp_memory_shutdown,
};

static int __init sharp_memory_init(void)
{
	int ret;

	if (qemu_display_dev) {
		printk(KERN_INFO "sharp_memory: QEMU mode, serial device: %s\n",
			qemu_display_dev);

		qemu_pdev = platform_device_alloc("sharp-drm-qemu", 0);
		if (!qemu_pdev)
			return -ENOMEM;

		ret = platform_device_add(qemu_pdev);
		if (ret) {
			platform_device_put(qemu_pdev);
			return ret;
		}

		ret = drm_probe_qemu(&qemu_pdev->dev, qemu_display_dev);
		if (ret)
			goto err_pdev;

		ret = params_probe();
		if (ret)
			goto err_drm;

		ret = ioctl_probe();
		if (ret)
			goto err_params;

		return 0;

err_params:
		params_remove();
err_drm:
		drm_remove_qemu(&qemu_pdev->dev);
err_pdev:
		platform_device_del(qemu_pdev);
		platform_device_put(qemu_pdev);
		return ret;
	}

	return spi_register_driver(&sharp_memory_spi_driver);
}

static void __exit sharp_memory_exit(void)
{
	if (qemu_display_dev && qemu_pdev) {
		ioctl_remove();
		params_remove();
		drm_remove_qemu(&qemu_pdev->dev);
		platform_device_del(qemu_pdev);
		platform_device_put(qemu_pdev);
	} else {
		spi_unregister_driver(&sharp_memory_spi_driver);
	}
}

module_init(sharp_memory_init);
module_exit(sharp_memory_exit);

MODULE_VERSION("1.6");
MODULE_DESCRIPTION("Sharp Memory LCD DRM driver");
MODULE_AUTHOR("Andrew D'Angelo");
MODULE_LICENSE("GPL");

void sharp_memory_set_invert(int setting)
{
	params_set_mono_invert(setting);
}
EXPORT_SYMBOL_GPL(sharp_memory_set_invert);

void* sharp_memory_add_overlay(int x, int y, int width, int height,
	unsigned char const* pixels)
{
	return drm_add_overlay(x, y, width, height, pixels);
}
EXPORT_SYMBOL_GPL(sharp_memory_add_overlay);

void sharp_memory_remove_overlay(void* entry)
{
	drm_remove_overlay(entry);
}
EXPORT_SYMBOL_GPL(sharp_memory_remove_overlay);

void* sharp_memory_show_overlay(void* storage)
{
	return drm_show_overlay(storage);
}
EXPORT_SYMBOL_GPL(sharp_memory_show_overlay);

void sharp_memory_hide_overlay(void* display)
{
	drm_hide_overlay(display);
}
EXPORT_SYMBOL_GPL(sharp_memory_hide_overlay);

void sharp_memory_clear_overlays(void)
{
	drm_clear_overlays();
}
EXPORT_SYMBOL_GPL(sharp_memory_clear_overlays);

