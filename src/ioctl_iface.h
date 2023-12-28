#ifndef IOCTL_IFACE_H_
#define IOCTL_IFACE_H_

int ioctl_probe(void);
void ioctl_remove(void);

int sharp_memory_ioctl_get_version(struct drm_device *dev, void *data,
	struct drm_file *file);
int sharp_memory_ioctl_set_invert(struct drm_device *dev, void *data,
	struct drm_file *file);
int sharp_memory_ioctl_set_indicator(struct drm_device *dev, void *data,
	struct drm_file *file);

#define DRM_SHARP_GET_VERSION 0x00
#define DRM_SHARP_SET_INVERT 0x01
#define DRM_SHARP_SET_INDICATOR 0x02

#define DRM_IOCTL_SHARP_GET_VERSION \
	DRM_IOR(DRM_COMMAND_BASE + DRM_SHARP_GET_VERSION, uint32_t)
#define DRM_IOCTL_SHARP_SET_INVERT \
	DRM_IOW(DRM_COMMAND_BASE + DRM_SHARP_SET_INVERT, uint32_t)
#define DRM_IOCTL_SHARP_SET_INDICATOR \
	DRM_IOW(DRM_COMMAND_BASE + DRM_SHARP_SET_INDICATOR, uint32_t)

#define DRM_IOCTL_DEF_DRV_GET_VERSION \
	DRM_IOCTL_DEF_DRV(SHARP_GET_VERSION, sharp_memory_ioctl_get_version, DRM_RENDER_ALLOW)
#define DRM_IOCTL_DEF_DRV_SET_INVERT \
	DRM_IOCTL_DEF_DRV(SHARP_SET_INVERT, sharp_memory_ioctl_set_invert, DRM_RENDER_ALLOW)
#define DRM_IOCTL_DEF_DRV_SET_INDICATOR \
	DRM_IOCTL_DEF_DRV(SHARP_SET_INDICATOR, sharp_memory_ioctl_set_indicator, DRM_RENDER_ALLOW)

#endif
