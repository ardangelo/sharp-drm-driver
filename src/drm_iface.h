#ifndef DRM_IFACE_H_
#define DRM_IFACE_H_

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

int drm_probe(struct spi_device *spi);
void drm_remove(struct spi_device *spi);

int drm_redraw_fb(struct drm_device *drm, int height);
void* drm_add_overlay(int x, int y, int width, int height,
	unsigned char const* pixels);
void drm_remove_overlay(void* storage);
void drm_clear_overlays(void);
void* drm_show_overlay(void* storage);
void drm_hide_overlay(void* display);

#endif
