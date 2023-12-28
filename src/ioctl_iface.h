#ifndef IOCTL_IFACE_H_
#define IOCTL_IFACE_H_

int ioctl_probe(void);
void ioctl_remove(void);

int sharp_memory_ioctl_redraw(struct drm_device *dev, void *data,
	struct drm_file *file);

#define DRM_SHARP_REDRAW 0x00

#define DRM_IOCTL_SHARP_REDRAW \
	DRM_IO(DRM_COMMAND_BASE + DRM_SHARP_REDRAW)

#define DRM_IOCTL_DEF_DRV_REDRAW \
	DRM_IOCTL_DEF_DRV(SHARP_REDRAW, sharp_memory_ioctl_redraw, DRM_RENDER_ALLOW)

#endif
