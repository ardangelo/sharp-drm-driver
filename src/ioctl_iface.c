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

int sharp_memory_ioctl_redraw(struct drm_device *dev, void *data,
	struct drm_file *file)
{
	drm_redraw_fb(dev, -1);
	return 0;
}
