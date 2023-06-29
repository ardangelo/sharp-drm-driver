#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "params_iface.h"
#include "drm_iface.h"

int g_param_mono_cutoff = 32;
int g_param_mono_invert = 0;

static int set_param_u8(const char *val, const struct kernel_param *kp)
{
	int rc, result;

	// Parse string value
	if ((rc = kstrtoint(val, 10, &result)) || (result < 0) || (result > 0xff)) {
		return -EINVAL;
	}

	rc = param_set_int(val, kp);

	// Refresh framebuffer
	(void)drm_refresh();

	return rc;
}

static const struct kernel_param_ops u8_param_ops = {
	.set = set_param_u8,
	.get = param_get_int,
};

module_param_cb(mono_cutoff, &u8_param_ops, &g_param_mono_cutoff, 0660);
MODULE_PARM_DESC(mono_cutoff,
	"Greyscale value from 0-255 after which a mono pixel will be activated");

module_param_cb(mono_invert, &u8_param_ops, &g_param_mono_invert, 0660);
MODULE_PARM_DESC(mono_invert, "0 for no inversion, 1 for inversion");

int params_probe(void)
{
	return 0;
}

void params_remove(void)
{
	return;
}

void params_shutdown(void)
{
	return;
}
