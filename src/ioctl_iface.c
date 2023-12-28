#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>

#include "params_iface.h"
#include "drm_iface.h"
#include "ioctl_iface.h"

int ioctl_probe(void)
{
	return 0;
}

void ioctl_remove(void)
{}

int sharp_memory_ioctl_get_version(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	*(uint32_t*)data = 0x54000104;
	return 0;
}

int sharp_memory_ioctl_set_invert(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	params_set_mono_invert(*(uint32_t*)data);

	drm_redraw_fb(dev, -1);

	return 0;
}

int sharp_memory_ioctl_set_indicator(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	uint8_t idx, ch;

	idx = ((uint32_t)data) >> 8;
	ch = ((uint32_t)data) & 0xff;

	drm_set_indicator(dev, idx, ch);

	return 0;
}
