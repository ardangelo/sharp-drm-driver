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

int sharp_memory_ioctl_ov_add(struct drm_device *dev,
	void *in_overlay_out_storage, struct drm_file *file)
{
	union sharp_memory_ioctl_ov_add_t *add
		= (union sharp_memory_ioctl_ov_add_t *)in_overlay_out_storage;
	unsigned char *pixels = NULL;
	unsigned long copy_from_user_rc;
	struct sharp_overlay_t *ov = add->in_overlay;

	// Copy pixel buffer from userspace
	if ((pixels = (unsigned char*)kmalloc(ov->width * ov->height, GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "sharp_drm: failed to allocate overlay buffer\n");
		return -1;
	}
	if ((copy_from_user_rc = copy_from_user(pixels, ov->pixels, ov->width * ov->height))) {
		printk(KERN_ERR "sharp_drm: failed to copy overlay buffer from userspace (could not copy %zu/%zu)\n",
			copy_from_user_rc, ov->width * ov->height);
		kfree(pixels);
		return -1;
	}

	// Add overlay (copies `pixels`)
	add->out_storage = drm_add_overlay(ov->x, ov->y, ov->width, ov->height,
		pixels);
	kfree(pixels);

	return 0;
}

int sharp_memory_ioctl_ov_rem(struct drm_device *dev, void *storage_,
	struct drm_file *file)
{
	struct sharp_memory_ioctl_ov_rem_t * storage
		= (struct sharp_memory_ioctl_ov_rem_t *)storage_;

	drm_remove_overlay(storage->storage);

	return 0;
}

int sharp_memory_ioctl_ov_show(struct drm_device *dev,
	void *in_storage_out_display, struct drm_file *file)
{
	union sharp_memory_ioctl_ov_show_t *show
		= (union sharp_memory_ioctl_ov_show_t *)in_storage_out_display;

	show->out_display = drm_show_overlay(show->in_storage);

	drm_redraw_fb(dev, -1);

	return 0;
}

int sharp_memory_ioctl_ov_hide(struct drm_device *dev, void *display_,
	struct drm_file *file)
{
	struct sharp_memory_ioctl_ov_hide_t *display
		= (struct sharp_memory_ioctl_ov_hide_t *)display_;

	drm_hide_overlay(display->display);

	drm_redraw_fb(dev, -1);

	return 0;
}

int sharp_memory_ioctl_ov_clear(struct drm_device *dev, void *_,
	struct drm_file *file)
{
	drm_clear_overlays();

	return 0;
}

