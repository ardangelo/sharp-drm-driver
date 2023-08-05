#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>

#include "params_iface.h"
#include "drm_iface.h"
#include "ioctl_iface.h"

#define SHARP_IOC_MAGIC 0xd5
#define SHARP_IOCTQ_SET_INVERT _IOW(SHARP_IOC_MAGIC, 1, uint32_t)
#define SHARP_IOCTQ_SET_INDICATOR _IOW(SHARP_IOC_MAGIC, 2, uint32_t)
#define SHARP_IOC_MAXNR 2

static int ioctl_set_invert(unsigned long arg)
{
	params_set_mono_invert((int)arg);

	return 0;
}

static int ioctl_set_indicator(unsigned long arg)
{
	uint8_t idx, ch;

	idx = arg >> 8;
	ch = arg & 0xff;

	return drm_set_indicator(idx, ch);
}

static int sharp_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int sharp_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long int sharp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {

	case SHARP_IOCTQ_SET_INVERT:
		(void)ioctl_set_invert((int)arg);
		return 0;

	case SHARP_IOCTQ_SET_INDICATOR:
		(void)ioctl_set_indicator((int)arg);
		return 0;


	default: 
		printk(KERN_INFO "Bad command: %d", cmd);
		return -EINVAL;
	}
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = sharp_open,
	.unlocked_ioctl = sharp_ioctl,
	.release = sharp_release,
};

static struct cdev g_cdev;
static dev_t g_dev;
struct class *g_class;

int ioctl_probe(void)
{
	int rc;

	if ((rc = alloc_chrdev_region(&g_dev, 0, 1, "sharp"))) {
		printk(KERN_ERR "Failed to allocate device numbers\n");
		return rc;
	}

	g_class = class_create(THIS_MODULE, "sharp_class");
	device_create(g_class, NULL, g_dev, NULL, "sharp");

	cdev_init(&g_cdev, &fops);
	g_cdev.owner = THIS_MODULE;

	if ((rc = cdev_add(&g_cdev, g_dev, 1))) {
		unregister_chrdev_region(g_dev, 1);
		printk(KERN_ERR "Failed to add the character device\n");
		return rc;
	}

	printk(KERN_INFO "Character device registered: major = %d, minor = %d\n",
		MAJOR(g_dev), MINOR(g_dev));

	return 0;
}

void ioctl_remove(void)
{
	cdev_del(&g_cdev);
	device_destroy(g_class, g_dev);
	class_destroy(g_class);
	unregister_chrdev_region(g_dev, 1);
	printk(KERN_INFO "Character device unregistered\n");
}
