// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DRM driver for 2.7" Sharp Memory LCD
 *
 * Copyright 2023 Andrew D'Angelo
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
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

#include "params_iface.h"
#include "ioctl_iface.h"
#include "drm_iface.h"

#include "indicators.h"

#define CMD_WRITE_LINE 0b10000000
#define CMD_CLEAR_SCREEN 0b00100000

static int ditherMatrix1[2][2] = {
    { 128, 179 },
	{ 204, 153 },
};

static int ditherMatrix2[2][2] = {
    {  51, 204 },
	{ 153,  102 },
};

static int ditherMatrix4[4][4] = {
    {  15, 195,  60, 240 },
	{ 135,  75, 180, 120 },
	{  45, 225,  30, 210 },
	{ 165, 105, 150,  90 }
};

struct sharp_memory_panel {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	const struct drm_display_mode *mode;
	struct drm_connector connector;
	struct spi_device *spi;
	struct drm_framebuffer *fb;

	struct timer_list vcom_timer;

	unsigned int height;
	unsigned int width;

	unsigned char *buf;
	struct spi_transfer *spi_3_xfers;
	unsigned char *cmd_buf;
	unsigned char *trailer_buf;

	char indicators[MAX_INDICATORS];

	struct gpio_desc *gpio_disp;
	struct gpio_desc *gpio_vcom;
};

static struct sharp_memory_panel* g_panel = NULL;

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
	gpiod_set_value(panel->gpio_vcom, 1);

	// Reschedule the timer
	mod_timer(&panel->vcom_timer, jiffies + msecs_to_jiffies(1000));
}

static int sharp_memory_spi_clear_screen(struct sharp_memory_panel *panel)
{
	int rc;

	// Create screen clear command SPI transfer
	panel->cmd_buf[0] = CMD_CLEAR_SCREEN;
	panel->spi_3_xfers[0].tx_buf = panel->cmd_buf;
	panel->spi_3_xfers[0].len = 1;
	panel->trailer_buf[0] = 0;
	panel->spi_3_xfers[1].tx_buf = panel->trailer_buf;
	panel->spi_3_xfers[1].len = 1;

	// Write clear screen command
	ndelay(80);

	rc = spi_sync_transfer(panel->spi, panel->spi_3_xfers, 2);

	return rc;
}

static inline u8 sharp_memory_reverse_byte(u8 b)
{
	b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
	b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
	b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
	return b;
}

static int sharp_memory_spi_write_tagged_lines(struct sharp_memory_panel *panel,
	void *line_data, size_t len)
{
	int rc;

	// Write line command
	panel->cmd_buf[0] = 0b10000000;
	panel->spi_3_xfers[0].tx_buf = panel->cmd_buf;
	panel->spi_3_xfers[0].len = 1;

	// Line data
	panel->spi_3_xfers[1].tx_buf = line_data;
	panel->spi_3_xfers[1].len = len;

	// Trailer
	panel->trailer_buf[0] = 0;
	panel->spi_3_xfers[2].tx_buf = panel->trailer_buf;
	panel->spi_3_xfers[2].len = 1;

	ndelay(80);

	rc = spi_sync_transfer(panel->spi, panel->spi_3_xfers, 3);

	return rc;
}

static void draw_indicators(struct sharp_memory_panel *panel, u8* buf, int width,
	struct drm_rect const* clip)
{
	int i, dx, dy, sx, sy;
	u8 const* ind = NULL;

	for (i = 0; i < MAX_INDICATORS; i++) {

		// Get indicator pixels
		ind = indicator_for(panel->indicators[i]);
		if (!ind) {
			continue;
		}

		// Draw indicator pixels
		for (sy = 0; sy < INDICATOR_HEIGHT; sy++) {

			if (sy < clip->y1) {
				continue;
			} else if (clip->y2 <= sy) {
				break;
			}

			dy = sy - clip->y1;

			for (sx = 0; sx < INDICATOR_WIDTH; sx++) {

				if (sx < clip->x1) {
					continue;
				} else if (clip->x2 <= sx) {
					break;
				}

				dx = (width - ((i + 1) * INDICATOR_WIDTH) + sx) - clip->x1;
				buf[dy * (clip->x2 - clip->x1) + dx] = ind[sy * INDICATOR_HEIGHT + sx];
			}
		}
	}
}

static size_t sharp_memory_gray8_to_mono_tagged_dither(u8 *buf, int width, int height, int y0)
{
	int line, b8, b1;
	unsigned char d;
	int const tagged_line_len = 2 + width / 8;

	// Iterate over lines from [0, height)
	for (line = 0; line < height; line++) {

		// Iterate over chunks of 8 source grayscale bytes
		// Each 8-byte source chunk will map to one destination mono byte
		for (b8 = 0; b8 < width; b8 += 8) {
			d = 0;

			// Iterate over each of the 8 grayscale bytes in the chunk
			// Build up the destination mono byte
			for (b1 = 0; b1 < 8; b1++) {

				// Apply dithering
				if (g_param_dither == 1) {
					if (buf[(line * width) + b8 + b1] >= ditherMatrix1[((line * width) + b8 + b1) % 2][line % 2])
						d |= 0b10000000 >> b1;
				} else if (g_param_dither == 2) {
					if (buf[(line * width) + b8 + b1] >= ditherMatrix2[((line * width) + b8 + b1) % 2][line % 2])
						d |= 0b10000000 >> b1;
				} else {
					if (buf[(line * width) + b8 + b1] >= ditherMatrix4[((line * width) + b8 + b1) % 4][line % 4])
					d |= 0b10000000 >> b1;
				}
			}

			// Apply inversion
			if (g_param_mono_invert) {
				d = ~d;
			}

			// Without the line number and trailer tags, each destination
			// mono line would have a length `width / 8`. However, we are
			// inserting the line number at the beginning of the line and
			// the zero-byte trailer at the end.
			// So the destination mono line is at index
			// `line * tagged_line_len = line * (2 + width / 8)`
			// The destination mono byte is offset by 1 to make room for
			// the line tag, written at the end of converting the current
			// line.
			buf[(line * tagged_line_len) + 1 + (b8 / 8)] = d;
		}

		// Write the line number and trailer tags
		buf[line * tagged_line_len] = sharp_memory_reverse_byte((u8)(y0 + 1)); // Indexed from 1
		buf[(line * tagged_line_len) + tagged_line_len - 1] = 0;
		y0++;
	}

	return height * tagged_line_len;
}

static size_t sharp_memory_gray8_to_mono_tagged(u8 *buf, int width, int height, int y0)
{
	int line, b8, b1;
	unsigned char d;
	int const tagged_line_len = 2 + width / 8;

	// Iterate over lines from [0, height)
	for (line = 0; line < height; line++) {

		// Iterate over chunks of 8 source grayscale bytes
		// Each 8-byte source chunk will map to one destination mono byte
		for (b8 = 0; b8 < width; b8 += 8) {
			d = 0;

			// Iterate over each of the 8 grayscale bytes in the chunk
			// Build up the destination mono byte
			for (b1 = 0; b1 < 8; b1++) {

				// Change at what gray level the mono pixel is active here
				if (buf[(line * width) + b8 + b1] >= g_param_mono_cutoff) {
					d |= 0b10000000 >> b1;
				}
			}

			// Apply inversion
			if (g_param_mono_invert) {
				d = ~d;
			}

			// Without the line number and trailer tags, each destination
			// mono line would have a length `width / 8`. However, we are
			// inserting the line number at the beginning of the line and
			// the zero-byte trailer at the end.
			// So the destination mono line is at index
			// `line * tagged_line_len = line * (2 + width / 8)`
			// The destination mono byte is offset by 1 to make room for
			// the line tag, written at the end of converting the current
			// line.
			buf[(line * tagged_line_len) + 1 + (b8 / 8)] = d;
		}

		// Write the line number and trailer tags
		buf[line * tagged_line_len] = sharp_memory_reverse_byte((u8)(y0 + 1)); // Indexed from 1
		buf[(line * tagged_line_len) + tagged_line_len - 1] = 0;
		y0++;
	}

	return height * tagged_line_len;
}

// Use DMA to get grayscale representation, then convert to mono
// with line number and trailer tags suitable for multi-line write
// Output is stored in `buf`, which must be at least W*H bytes
static int sharp_memory_clip_mono_tagged(struct sharp_memory_panel* panel, size_t* result_len,
	u8* buf, struct drm_framebuffer *fb, struct drm_rect const* clip)
{
	int rc;
	struct drm_gem_dma_object *dma_obj;
	struct iosys_map dst, vmap;

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

	// Add status indicators
	if (g_param_indicators) {

		// Only redraw indicators if the dirty region would overwrite them
		if ((clip->x1 < (fb->width - INDICATORS_WIDTH))
		 && (clip->y1 < INDICATOR_HEIGHT)) {
			draw_indicators(panel, buf, fb->width, clip);
		}
	}

	if (g_param_dither) {
		// Convert in-place from 8-bit grayscale to dithered mono
		*result_len = sharp_memory_gray8_to_mono_tagged_dither(buf,
			(clip->x2 - clip->x1), (clip->y2 - clip->y1), clip->y1);
	} else {
		// Convert in-place from 8-bit grayscale to mono
		*result_len = sharp_memory_gray8_to_mono_tagged(buf,
			(clip->x2 - clip->x1), (clip->y2 - clip->y1), clip->y1);
	}

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
	size_t buf_len;

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

	// Convert `clip` from framebuffer to mono with line number tags
	rc = sharp_memory_clip_mono_tagged(panel, &buf_len, panel->buf, fb, &clip);
	if (rc) {
		goto out_exit;
	}

	// Write mono data to display
	rc = sharp_memory_spi_write_tagged_lines(panel, panel->buf, buf_len);

out_exit:
	// Exit DRM device resource area
	drm_dev_exit(drm_idx);

	return rc;
}


static void power_off(struct sharp_memory_panel *panel)
{
	printk(KERN_INFO "sharp_memory: powering off\n");

	/* Turn off power and all signals */
	gpiod_set_value(panel->gpio_disp, 0);
	gpiod_set_value(panel->gpio_vcom, 0);
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
	gpiod_set_value(panel->gpio_disp, 1);
	gpiod_set_value(panel->gpio_vcom, 0);
	usleep_range(5000, 10000);

	// Clear display
	if (sharp_memory_spi_clear_screen(panel)) {
		gpiod_set_value(panel->gpio_disp, 0); // Power down display, VCOM is not running
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

	return drm_connector_helper_get_modes_fixed(connector, panel->mode);
}

static struct drm_framebuffer* create_and_store_fb(struct drm_device *dev, struct drm_file *file,
	const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer* fb;

	// Initialize framebuffer
	fb = drm_gem_fb_create_with_dirty(dev, file, mode_cmd);

	// Store global framebuffer for external operations
	if (g_panel) {
		g_panel->fb = fb;
	}

	return fb;
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
	.fb_create = create_and_store_fb,
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
	.date = "20230713",
	.major = 1,
	.minor = 1,
};

int drm_probe(struct spi_device *spi)
{
	const struct drm_display_mode *mode;
	struct device *dev;
	struct sharp_memory_panel *panel;
	struct drm_device *drm;
	int ret, i;

	printk(KERN_INFO "sharp_memory: entering drm_probe\n");

	// Get DRM device from SPI struct
	dev = &spi->dev;

	// The SPI device is used to allocate DMA memory
	if (!dev->coherent_dma_mask) {
		ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_warn(dev, "Failed to set dma mask %d\n", ret);
			return ret;
		}
	}

	// Allocate panel storage
	panel = devm_drm_dev_alloc(dev, &sharp_memory_driver,
		struct sharp_memory_panel, drm);
	if (IS_ERR(panel)) {
		printk(KERN_ERR "sharp_memory: failed to allocate panel\n");
		return PTR_ERR(panel);
	}
	g_panel = panel;

	// Initialize GPIO
	panel->gpio_disp = devm_gpiod_get(dev, "disp", GPIOD_OUT_HIGH);
	if (IS_ERR(panel->gpio_disp))
		return dev_err_probe(dev, PTR_ERR(panel->gpio_disp), "Failed to get GPIO 'disp'\n");

	panel->gpio_vcom = devm_gpiod_get(dev, "vcom", GPIOD_OUT_LOW);
	if (IS_ERR(panel->gpio_vcom))
		return dev_err_probe(dev, PTR_ERR(panel->gpio_vcom), "Failed to get GPIO 'vcom'\n");

	// Initalize DRM mode
	drm = &panel->drm;
	ret = drmm_mode_config_init(drm);
	if (ret) {
		return ret;
	}
	drm->mode_config.funcs = &sharp_memory_mode_config_funcs;

	// Initialize panel contents
	panel->spi = spi;
	mode = &sharp_memory_ls027b7dh01_mode;
	panel->mode = mode;
	panel->width = mode->hdisplay;
	panel->height = mode->vdisplay;
	for (i = 0; i < MAX_INDICATORS; i++) {
		panel->indicators[i] = '\0';
	}

	// Allocate reused heap buffers suitable for SPI source
	panel->buf = devm_kzalloc(dev, panel->width * panel->height, GFP_KERNEL);
	panel->spi_3_xfers = devm_kzalloc(dev, sizeof(struct spi_transfer) * 3, GFP_KERNEL);
	panel->cmd_buf = devm_kzalloc(dev, 1, GFP_KERNEL);
	panel->trailer_buf = devm_kzalloc(dev, 1, GFP_KERNEL);

	// DRM mode settings
	drm->mode_config.min_width = mode->hdisplay;
	drm->mode_config.max_width = mode->hdisplay;
	drm->mode_config.min_height = mode->vdisplay;
	drm->mode_config.max_height = mode->vdisplay;

	// Configure DRM connector
	ret = drm_connector_init(drm, &panel->connector, &sharp_memory_connector_funcs,
		DRM_MODE_CONNECTOR_SPI);
	if (ret) {
		return ret;
	}
	drm_connector_helper_add(&panel->connector, &sharp_memory_connector_hfuncs);

	// Initialize DRM pipe
	ret = drm_simple_display_pipe_init(drm, &panel->pipe, &sharp_memory_pipe_funcs,
		sharp_memory_formats, ARRAY_SIZE(sharp_memory_formats),
		NULL, &panel->connector);
	if (ret) {
		return ret;
	}

	// Enable damaged screen area clips
	drm_plane_enable_fb_damage_clips(&panel->pipe.plane);

	drm_mode_config_reset(drm);

	printk(KERN_INFO "sharp_memory: registering DRM device\n");
	ret = drm_dev_register(drm, 0);
	if (ret) {
		return ret;
	}

	// fbdev setup
	spi_set_drvdata(spi, drm);
	drm_fbdev_generic_setup(drm, 0);

	printk(KERN_INFO "sharp_memory: successful probe\n");

	return 0;
}

void drm_remove(struct spi_device *spi)
{
	struct drm_device *drm;
	struct device *dev;
	struct sharp_memory_panel *panel;

	printk(KERN_INFO "sharp_memory: drm_remove\n");

	// Clear global panel
	g_panel = NULL;

	// Get DRM and panel device from SPI
	drm = spi_get_drvdata(spi);

	// Clean up the GPIO descriptors
	dev = &spi->dev;
	panel = drm_to_panel(drm);

	devm_gpiod_put(dev, panel->gpio_disp);
	devm_gpiod_put(dev, panel->gpio_vcom);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static int force_redraw(struct drm_framebuffer* fb, struct drm_clip_rect* dirty_rect)
{
	if (!fb || !fb->funcs->dirty) {
		return -1;
	}

	// Call framebuffer region update handler
	return fb->funcs->dirty(fb, NULL, 0, 0, dirty_rect, 1);
}

int drm_refresh(void)
{
	struct drm_clip_rect dirty_rect;

	if (!g_panel) {
		return 0;
	}

	// Refresh framebuffer
	dirty_rect.x1 = 0;
	dirty_rect.x2 = g_panel->fb->width;
	dirty_rect.y1 = 0;
	dirty_rect.y2 = g_panel->fb->height;
	return force_redraw(g_panel->fb, &dirty_rect);
}

int drm_set_indicator(size_t idx, char c)
{
	struct drm_clip_rect dirty_rect;

	if (!g_panel || !g_panel->fb || !g_panel->fb->funcs->dirty) {
		return -1;
	}

	// Set indicator
	if (idx >= MAX_INDICATORS) {
		return -1;
	}
	g_panel->indicators[idx] = c;

	// Refresh indicator portion of framebuffer
	dirty_rect.x1 = 0;
	dirty_rect.x2 = g_panel->fb->width;
	dirty_rect.y1 = 0;
	dirty_rect.y2 = INDICATOR_HEIGHT;
	return force_redraw(g_panel->fb, &dirty_rect);
}
