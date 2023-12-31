#ifndef IOCTL_IFACE_H_
#define IOCTL_IFACE_H_

int ioctl_probe(void);
void ioctl_remove(void);

struct sharp_overlay_t
{
	int x, y, width, height;
	unsigned char const *pixels;
};

union sharp_memory_ioctl_ov_add_t
{
	struct sharp_overlay_t *in_overlay;
	void *out_storage;
};

struct sharp_memory_ioctl_ov_rem_t
{
	void *storage;
};

union sharp_memory_ioctl_ov_show_t
{
	void *in_storage;
	void *out_display;
};

struct sharp_memory_ioctl_ov_hide_t
{
	void *display;
};

int sharp_memory_ioctl_redraw(struct drm_device *dev, void *,
	struct drm_file *file);

int sharp_memory_ioctl_ov_add(struct drm_device *dev, \
	void *in_overlay_out_storage, struct drm_file *file);
int sharp_memory_ioctl_ov_rem(struct drm_device *dev, void *storage,
	struct drm_file *file);
int sharp_memory_ioctl_ov_show(struct drm_device *dev, \
	void *in_storage_out_display, struct drm_file *file);
int sharp_memory_ioctl_ov_hide(struct drm_device *dev, void *display,
	struct drm_file *file);
int sharp_memory_ioctl_ov_clear(struct drm_device *dev, void *,
	struct drm_file *file);

// No parameters, callable from kernel space
#define DRM_SHARP_REDRAW 0x00

// Parameters, must call from userspace
#define DRM_SHARP_OV_ADD 0x10
#define DRM_SHARP_OV_REM 0x11
#define DRM_SHARP_OV_SHOW 0x12
#define DRM_SHARP_OV_HIDE 0x13
#define DRM_SHARP_OV_CLEAR 0x14

#define DRM_IOCTL_SHARP_REDRAW \
	DRM_IO(DRM_COMMAND_BASE + DRM_SHARP_REDRAW)

#define DRM_IOCTL_SHARP_OV_ADD \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_SHARP_OV_ADD, \
		union sharp_memory_ioctl_ov_add_t)
#define DRM_IOCTL_SHARP_OV_REM \
	DRM_IOW(DRM_COMMAND_BASE + DRM_SHARP_OV_REM, \
		struct sharp_memory_ioctl_ov_rem_t)
#define DRM_IOCTL_SHARP_OV_SHOW \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_SHARP_OV_SHOW, \
		union sharp_memory_ioctl_ov_show_t)
#define DRM_IOCTL_SHARP_OV_HIDE \
	DRM_IOW(DRM_COMMAND_BASE + DRM_SHARP_OV_HIDE, \
		struct sharp_memory_ioctl_ov_hide_t)
#define DRM_IOCTL_SHARP_OV_CLEAR \
	DRM_IO(DRM_COMMAND_BASE + DRM_SHARP_OV_CLEAR)

#define DRM_IOCTL_DEF_DRV_REDRAW \
	DRM_IOCTL_DEF_DRV(SHARP_REDRAW, sharp_memory_ioctl_redraw, DRM_RENDER_ALLOW)

#define DRM_IOCTL_DEF_DRV_OV_ADD \
	DRM_IOCTL_DEF_DRV(SHARP_OV_ADD, sharp_memory_ioctl_ov_add, DRM_RENDER_ALLOW)
#define DRM_IOCTL_DEF_DRV_OV_REM \
	DRM_IOCTL_DEF_DRV(SHARP_OV_REM, sharp_memory_ioctl_ov_rem, DRM_RENDER_ALLOW)
#define DRM_IOCTL_DEF_DRV_OV_SHOW \
	DRM_IOCTL_DEF_DRV(SHARP_OV_SHOW, sharp_memory_ioctl_ov_show, DRM_RENDER_ALLOW)
#define DRM_IOCTL_DEF_DRV_OV_HIDE \
	DRM_IOCTL_DEF_DRV(SHARP_OV_HIDE, sharp_memory_ioctl_ov_hide, DRM_RENDER_ALLOW)
#define DRM_IOCTL_DEF_DRV_OV_CLEAR \
	DRM_IOCTL_DEF_DRV(SHARP_OV_CLEAR, sharp_memory_ioctl_ov_clear, DRM_RENDER_ALLOW)

#endif
