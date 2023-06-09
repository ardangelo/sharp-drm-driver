// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DRM driver for 2.7" Sharp Memory LCD
 *
 * Copyright 2023 Andrew D'Angelo
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/sched/clock.h>
#include <linux/spi/spi.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modes.h>
#include <drm/drm_rect.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#define CMD_WRITE_LINE 0b10000000
#define CMD_CLEAR_SCREEN 0b00100000

#define GPIO_DISP 22
#define GPIO_SCS 8
#define GPIO_VCOM 23

struct sharp_memory_panel {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	const struct drm_display_mode *mode;
	struct drm_connector connector;
	struct spi_device *spi;

	struct timer_list vcom_timer;

	unsigned int height;
	unsigned int width;
};

static inline struct sharp_memory_panel *drm_to_panel(struct drm_device *drm)
{
	return container_of(drm, struct sharp_memory_panel, drm);
}

static void vcom_timer_callback(struct timer_list *t)
{
	static u8 vcom_setting = 0;

	struct sharp_memory_panel *panel = from_timer(panel, t, vcom_timer);

	// Toggle the GPIO pin
	vcom_setting = (vcom_setting) ? 0 : 1;
	gpio_set_value(GPIO_VCOM, vcom_setting);

	// Reschedule the timer
	mod_timer(&panel->vcom_timer, jiffies + msecs_to_jiffies(500));
}

static int sharp_memory_spi_clear_screen(struct sharp_memory_panel *panel)
{
	struct spi_transfer tr[1] = {};
	int ret;
	u8 *command_buf;

	command_buf = kmalloc(2, GFP_KERNEL);
	if (!command_buf) {
		return -ENOMEM;
	}

	// Clear screen command and trailer
	command_buf[0] = CMD_CLEAR_SCREEN;
	command_buf[1] = 0;
	tr[0].tx_buf = command_buf;
	tr[0].len = 2;

	ndelay(80);
	gpio_set_value(GPIO_SCS, 1);
	ret = spi_sync_transfer(panel->spi, tr, 1);
	gpio_set_value(GPIO_SCS, 0);

	goto out_free;

out_free:
	kfree(command_buf);

	return ret;
}

static inline u8 sharp_memory_reverse_byte(u8 b)
{
	b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
	b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
	b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
	return b;
}

static int sharp_memory_spi_write_line(struct sharp_memory_panel *panel,
	size_t y, const void *line_data, size_t len)
{
	void *tx_buf = NULL;
	struct spi_transfer tr[3] = {};
	u8 *command_buf, *trailer_buf;
	int ret;

	command_buf = kmalloc(2, GFP_KERNEL);
	if (!command_buf) {
		return -ENOMEM;
	}

	trailer_buf = kzalloc(2, GFP_KERNEL);
	if (!trailer_buf) {
		return -ENOMEM;
	}

	// Write line at line Y command
	command_buf[0] = CMD_WRITE_LINE;
	command_buf[1] = sharp_memory_reverse_byte((u8)(y + 1)); // Indexed from 1
	tr[0].tx_buf = command_buf;
	tr[0].len = 2;

	// Line data
	tx_buf = kmemdup(line_data, len, GFP_KERNEL);
	tr[1].tx_buf = tx_buf;
	tr[1].len = len;

	// Trailer
	tr[2].tx_buf = trailer_buf;
	tr[2].len = 2;

	ndelay(80);
	gpio_set_value(GPIO_SCS, 1);
	ret = spi_sync_transfer(panel->spi, tr, 3);
	gpio_set_value(GPIO_SCS, 0);

	goto out_free;

out_free:
	kfree(trailer_buf);
	kfree(tx_buf);
	kfree(command_buf);

	return ret;
}

static void sharp_memory_gray8_to_mono_reversed(u8 *dst, u8 const *src,
	int line_width, struct drm_rect const* clip)
{
#if 0
	u8 *gray8 = buf, *mono = buf;
	int y, xb, i;

	for (y = clip->y1; y < clip->y2; y++) {
		for (xb = clip->x1; xb < clip->x2; xb++) {
			u8 byte = 0x00;

			for (i = 0; i < 8; i++) {
				int x = xb * 8 + i;

				byte >>= 1;
				if (gray8[y * line_width + x] >> 7) {
					byte |= BIT(7);
				}
			}
			*mono++ = byte;
		}
	}
#else
	int x, y, i;
	for (y = clip->y1; y < clip->y2; y++) {
		for (x = clip->x1; x < clip->x2; x++) {
			dst[(y * line_width + x) / 8] = 0;
		}
		for (x = clip->x1; x < clip->x2; x++) {
			if (src[y * line_width + x] & BIT(7)) {
				dst[(y * line_width + x) / 8] |= 0b10000000 >> (x % 8);
			}
		}
	}
#endif
}

static int sharp_memory_fb_dirty(struct drm_framebuffer *fb,
	struct drm_rect const* dirty_rect)
{
	struct drm_gem_dma_object *dma_obj = drm_fb_dma_get_gem_obj(fb, 0);
	struct sharp_memory_panel *panel = drm_to_panel(fb->dev);
	unsigned int dst_pitch = 0;
	struct iosys_map dst, vmap;
	struct drm_rect clip;
	int idx, y, ret = 0;
	u8 *buf = NULL, *buf2 = NULL;

	if (!drm_dev_enter(fb->dev, &idx)) {
		return -ENODEV;
	}

	// Clip dirty region rows
	clip.x1 = 0;
	clip.x2 = fb->width;
#if 0
	clip.y1 = dirty_rect->y1;
	clip.y2 = dirty_rect->y2;
#else
	clip.y1 = 0;
	clip.y2 = fb->height;
#endif

	// buf is the size of the whole screen, but only the clip region
	// is copied from framebuffer
	//buf = kmalloc_array(fb->width, fb->height, GFP_KERNEL);
	buf = kmalloc(fb->width * fb->height, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out_exit;
	}
	buf2 = kmalloc(panel->width * panel->height / 8, GFP_KERNEL);
	memset(buf, fb->width * fb->height, 0);
	memset(buf2, panel->width * panel->height / 8, 0);

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret) {
		goto out_free;
	}

	iosys_map_set_vaddr(&dst, buf);
	iosys_map_set_vaddr(&vmap, dma_obj->vaddr);
	drm_fb_xrgb8888_to_gray8(&dst, NULL, &vmap, fb, &clip); 

	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);

	sharp_memory_gray8_to_mono_reversed(buf2, buf, fb->width, &clip);

	for (y = clip.y1; y < clip.y2; y++) {
		sharp_memory_spi_write_line(panel, y,
			&buf2[(y * panel->width) / 8], panel->width / 8);
	}

out_free:
	kfree(buf2);
	kfree(buf);
out_exit:
	drm_dev_exit(idx);

	return ret;
}


static void power_off(struct sharp_memory_panel *panel)
{
	printk(KERN_INFO "sharp_memory: powering off\n");

	/* Turn off power and all signals */
	gpio_set_value(GPIO_SCS, 0);
	gpio_set_value(GPIO_DISP, 0);
	gpio_set_value(GPIO_VCOM, 0);
}

static void sharp_memory_pipe_enable(struct drm_simple_display_pipe *pipe,
	struct drm_crtc_state *crtc_state, struct drm_plane_state *plane_state)
{
	printk(KERN_INFO "sharp_memory: entering sharp_memory_pipe_enable\n");

	struct sharp_memory_panel *panel = drm_to_panel(pipe->crtc.dev);
	struct spi_device *spi = panel->spi;
	int idx;

	if (!drm_dev_enter(pipe->crtc.dev, &idx)) {
		return;
	}

	/* Power up sequence */
	gpio_set_value(GPIO_SCS, 0);
	gpio_set_value(GPIO_DISP, 1);
	gpio_set_value(GPIO_VCOM, 0);
	usleep_range(5000, 10000);

	// Clear display
	sharp_memory_spi_clear_screen(panel);

	// Initialize and schedule the VCOM timer
	timer_setup(&panel->vcom_timer, vcom_timer_callback, 0);
	mod_timer(&panel->vcom_timer, jiffies + msecs_to_jiffies(500));

	printk(KERN_INFO "sharp_memory: completed sharp_memory_pipe_enable\n");

out_exit:
	drm_dev_exit(idx);
}

static void sharp_memory_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	printk(KERN_INFO "sharp_memory: sharp_memory_pipe_disable\n");

	struct sharp_memory_panel *panel = drm_to_panel(pipe->crtc.dev);
	struct spi_device *spi = panel->spi;

	// Cancel the timer
	del_timer_sync(&panel->vcom_timer);

	power_off(panel);
}

static void sharp_memory_pipe_update(struct drm_simple_display_pipe *pipe,
				struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_rect rect;
	int idx;

	if (!pipe->crtc.state->active) {
		return;
	}

	if (drm_atomic_helper_damage_merged(old_state, state, &rect)) {
		sharp_memory_fb_dirty(state->fb, &rect);
	}
}

static const struct drm_simple_display_pipe_funcs sharp_memory_pipe_funcs = {
	.enable = sharp_memory_pipe_enable,
	.disable = sharp_memory_pipe_disable,
	.update = sharp_memory_pipe_update,
	.prepare_fb = drm_gem_simple_display_pipe_prepare_fb,
};

static int sharp_memory_connector_get_modes(struct drm_connector *connector)
{
	struct sharp_memory_panel *panel = drm_to_panel(connector->dev);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, panel->mode);
	if (!mode) {
		DRM_ERROR("Failed to duplicate mode\n");
		return 0;
	}

	drm_mode_set_name(mode);
	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_connector_helper_funcs sharp_memory_connector_hfuncs = {
	.get_modes = sharp_memory_connector_get_modes,
};

static const struct drm_connector_funcs sharp_memory_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_mode_config_funcs sharp_memory_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const uint32_t sharp_memory_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const struct drm_display_mode sharp_memory_ls027b7dh01_mode = {
	DRM_SIMPLE_MODE(400, 240, 59, 35),
};

DEFINE_DRM_GEM_DMA_FOPS(sharp_memory_fops);

static const struct drm_driver sharp_memory_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &sharp_memory_fops,
	DRM_GEM_DMA_DRIVER_OPS_VMAP,
	.name = "sharp_memory",
	.desc = "Sharp Memory LCD panel",
	.date = "20230526",
	.major = 1,
	.minor = 0,
};

static int sharp_memory_probe(struct spi_device *spi)
{
	printk(KERN_INFO "sharp_memory: entering sharp_memory_probe\n");

	const struct drm_display_mode *mode;
	struct device *dev = &spi->dev;
	struct sharp_memory_panel *panel;
	struct drm_device *drm;
	int ret;

	/* The SPI device is used to allocate dma memory */
	if (!dev->coherent_dma_mask) {
		ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_warn(dev, "Failed to set dma mask %d\n", ret);
			return ret;
		}
	}

	panel = devm_drm_dev_alloc(dev, &sharp_memory_driver,
		struct sharp_memory_panel, drm);
	if (IS_ERR(panel)) {
		printk(KERN_ERR "sharp_memory: failed to allocate panel\n");
		return PTR_ERR(panel);
	}

	drm = &panel->drm;

	ret = drmm_mode_config_init(drm);
	if (ret) {
		return ret;
	}
	drm->mode_config.funcs = &sharp_memory_mode_config_funcs;

	panel->spi = spi;

	mode = &sharp_memory_ls027b7dh01_mode;
	panel->mode = mode;
	panel->width = mode->hdisplay;
	panel->height = mode->vdisplay;

	drm->mode_config.min_width = mode->hdisplay;
	drm->mode_config.max_width = mode->hdisplay;
	drm->mode_config.min_height = mode->vdisplay;
	drm->mode_config.max_height = mode->vdisplay;

	drm_connector_helper_add(&panel->connector, &sharp_memory_connector_hfuncs);
	ret = drm_connector_init(drm, &panel->connector, &sharp_memory_connector_funcs,
		DRM_MODE_CONNECTOR_SPI);
	if (ret) {
		return ret;
	}

	ret = drm_simple_display_pipe_init(drm, &panel->pipe, &sharp_memory_pipe_funcs,
		sharp_memory_formats, ARRAY_SIZE(sharp_memory_formats),
		NULL, &panel->connector);
	if (ret) {
		return ret;
	}

	drm_mode_config_reset(drm);

	printk(KERN_INFO "sharp_memory: registering DRM device\n");

	ret = drm_dev_register(drm, 0);
	if (ret) {
		return ret;
	}

	spi_set_drvdata(spi, drm);
	drm_fbdev_generic_setup(drm, 0);

	printk(KERN_INFO "sharp_memory: successful probe\n");

	return 0;
}

static void sharp_memory_remove(struct spi_device *spi)
{
	printk(KERN_DEBUG "sharp_memory: entered sharp_memory_remove\n");
	struct drm_device *drm = spi_get_drvdata(spi);
	printk(KERN_DEBUG "sharp_memory: completed spi_get_drvdata\n");
	struct sharp_memory_panel *panel = drm_to_panel(drm);
	printk(KERN_DEBUG "sharp_memory: completed drm_to_panel\n");

	drm_dev_unplug(drm);
	printk(KERN_DEBUG "sharp_memory: completed drm_dev_unplug\n");
	drm_atomic_helper_shutdown(drm);
	printk(KERN_DEBUG "sharp_memory: completed drm_atomic_helper_shutdown\n");
}

static void sharp_memory_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver sharp_memory_spi_driver = {
	.driver = {
		.name = "sharp",
	},
	.probe = sharp_memory_probe,
	.remove = sharp_memory_remove,
	.shutdown = sharp_memory_shutdown,
};
module_spi_driver(sharp_memory_spi_driver);

MODULE_DESCRIPTION("Sharp Memory LCD DRM driver");
MODULE_AUTHOR("Andrew D'Angelo");
MODULE_LICENSE("GPL");
