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

	unsigned char *buf;
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
	mod_timer(&panel->vcom_timer, jiffies + msecs_to_jiffies(1000));
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

static void sharp_memory_gray8_to_mono_reversed(u8 *buf, size_t len)
{
	size_t i, j;
	unsigned char b;
	for (i = 0; i < len; i += 8) {
		b = 0;
		for (j = 0; j < 8; j++) {
			if (buf[i + j] & BIT(7)) {
				b |= 0b10000000 >> j;
			}
		}
		buf[i / 8] = b;
	}
}

// Use DMA to get grayscale representation, then convert to mono
// Output is stored in `buf`, which must be at least W*H bytes
static int sharp_memory_clip_mono(u8* buf,
	struct drm_framebuffer *fb, struct drm_rect const* clip)
{
	int rc;
	struct drm_gem_dma_object *dma_obj;
	size_t clip_len;
	struct iosys_map dst, vmap;

	// buf is the size of the whole screen, but only the clip region
	// is copied from framebuffer
	clip_len = (clip->y2 - clip->y1) * fb->width;

	// Get GEM memory manager
	dma_obj = drm_fb_dma_get_gem_obj(fb, 0);

	// Start DMA area
	rc = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (rc) {
		return rc;
	}

	// Initialize destination (buf) and source (video)
	iosys_map_set_vaddr(&dst, buf);
	iosys_map_set_vaddr(&vmap, dma_obj->vaddr);
	// DMA `clip` into `buf` and convert to 8-bit grayscale
	drm_fb_xrgb8888_to_gray8(&dst, NULL, &vmap, fb, clip); 

	// End DMA area
	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);

	// Convert in-place from 8-bit grayscale to mono
	sharp_memory_gray8_to_mono_reversed(buf, clip_len);

	// Success
	return 0;
}

static int sharp_memory_fb_dirty(struct drm_framebuffer *fb,
	struct drm_rect const* dirty_rect)
{
	int rc;
	struct drm_rect clip;
	struct sharp_memory_panel *panel;
	int drm_idx;
	u8 *line;
	int y;

	// Clip dirty region rows
	clip.x1 = 0;
	clip.x2 = fb->width;
	clip.y1 = dirty_rect->y1;
	clip.y2 = dirty_rect->y2;

	// Get panel info from DRM struct
	panel = drm_to_panel(fb->dev);

	// Enter DRM device resource area
	if (!drm_dev_enter(fb->dev, &drm_idx)) {
		return -ENODEV;
	}

	// Get mono contents of `clip`
	rc = sharp_memory_clip_mono(panel->buf, fb, &clip);
	if (rc) {
		goto out_exit;
	}

	// Write mono data to display
	line = panel->buf;
	for (y = clip.y1; y < clip.y2; y++) {
		sharp_memory_spi_write_line(panel, y, line, fb->width / 8);
		line += (fb->width / 8);
	}

	// Success
	rc = 0;

out_exit:
	// Exit DRM device resource area
	drm_dev_exit(drm_idx);

	return rc;
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
	struct sharp_memory_panel *panel;
	struct spi_device *spi;
	int drm_idx;

	printk(KERN_INFO "sharp_memory: entering sharp_memory_pipe_enable\n");

	// Get panel and SPI device structs
	panel = drm_to_panel(pipe->crtc.dev);
	spi = panel->spi;

	// Enter DRM resource area
	if (!drm_dev_enter(pipe->crtc.dev, &drm_idx)) {
		return;
	}

	// Power up sequence
	gpio_set_value(GPIO_SCS, 0);
	gpio_set_value(GPIO_DISP, 1);
	gpio_set_value(GPIO_VCOM, 0);
	usleep_range(5000, 10000);

	// Clear display
	if (sharp_memory_spi_clear_screen(panel)) {
		gpio_set_value(GPIO_DISP, 0); // Power down display, VCOM is not running
		goto out_exit;
	}

	// Initialize and schedule the VCOM timer
	timer_setup(&panel->vcom_timer, vcom_timer_callback, 0);
	mod_timer(&panel->vcom_timer, jiffies + msecs_to_jiffies(500));

	printk(KERN_INFO "sharp_memory: completed sharp_memory_pipe_enable\n");

out_exit:
	drm_dev_exit(drm_idx);
}

static void sharp_memory_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct sharp_memory_panel *panel;
	struct spi_device *spi;

	printk(KERN_INFO "sharp_memory: sharp_memory_pipe_disable\n");

	// Get panel and SPI device structs
	panel = drm_to_panel(pipe->crtc.dev);
	spi = panel->spi;

	// Cancel the timer
	del_timer_sync(&panel->vcom_timer);

	power_off(panel);
}

static void sharp_memory_pipe_update(struct drm_simple_display_pipe *pipe,
				struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_rect rect;

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
	const struct drm_display_mode *mode;
	struct device *dev;
	struct sharp_memory_panel *panel;
	struct drm_device *drm;
	int ret;

	printk(KERN_INFO "sharp_memory: entering sharp_memory_probe\n");

	// Get DRM device from SPI struct
	dev = &spi->dev;

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
	panel->buf = devm_kzalloc(dev, panel->width * panel->height, GFP_KERNEL);

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
	struct drm_device *drm;
	struct sharp_memory_panel *panel;

	printk(KERN_DEBUG "sharp_memory: sharp_memory_remove\n");

	// Get DRM and panel device from SPI
	drm = spi_get_drvdata(spi);
	panel = drm_to_panel(drm);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
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
