# Sharp Memory LCD Kernel Driver

DRM kernel driver for 2.7" 400x240 Sharp memory LCD panel.

## User Guide

This is a kernel device driver to provide a display device to the Linux kernel.

### Font Configuration

This module automatically adds contents to the Raspberry Pi `/boot/firmware/cmdline.txt` to configure the Linux framebuffer, including font size.

The option `fbcon=font:VGA8x8` configures default font to a smaller size, resulting in dimensions of 30 rows and 50 columns. Can be changed to `font:VGA8x16` for a larger font. More fonts and options for the `fbcon` module can be found at https://www.kernel.org/doc/Documentation/fb/fbcon.txt

### `monoset` Utility

The `monoset` script in this repo is a helper to tune the monochrome cutoff for an application. It is installed by default on Beepy Raspbian. If an application has problems with monochrome color, you can run

    monoset [0 .. 255] <command>

The default driver monochrome cutoff is 32. Setting a value of 0 will cause the whole screen to display on, and 255 to display off.

For example, the `nmtui` utility works best with a cutoff of `127`. In the default Beepy Raspbian profile, there is an alias

    alias nmtui="monoset 127 nmtui"

This will automatically apply a cutoff of 127 when `nmtui` is started from the shell. You can add your own aliases to your profile at `~/.profile`.

### Module Parameters

Several options are exposed in the directory `/sys/module/sharp_drm/parameters/`. Run

    echo <setting> | sudo tee /sys/module/sharp_drm/parameters/<param>

To make changes persistent:

* Add `sharp_drm=<param>:<setting>` to `/boot/firmware/cmdline.txt`
* Edit the file `/etc/modules` and change the line `sharp-drm` to `sharp-drm <param>=<setting>`

* `auto_clear`: `1` to blank the screen when the display driver is unloaded (default enabled). If disabled, screen contents will remain until power is removed.
* `mono_cutoff`: Consider all pixels with one of R, G, B below this threshold to be black, otherwise white (default `32`)
* `mono_invert`: `0` for white-on-black, `1` for black-on-white. Can be toggled on-device by pressing Berry, then Zero (Meta mode + 0). For more information on Meta mode keymappings, see [https://github.com/ardangelo/beepberry-keyboard-driver/README.md]
* `overlays`: 0 to disable overlays (default enabled). Not recommended to disable, overlays are used to display modifier key state and [key reference overlays](https://github.com/ardangelo/beepy-symbol-overlay/README.md)

## Developer Reference

### Building from source

Install the Linux kernel headers

	sudo apt-get install raspberrypi-kernel-headers

Build, install, and enable the kernel module:

    make
    sudo make install

To remove:

    sudo make uninstall

[Original fbdev module readme with pinouts and build instructions](https://github.com/w4ilun/Sharp-Memory-LCD-Kernel-Driver/blob/master/README.md)

## References

Original SPI and GPIO kernel driver at:

	https://github.com/w4ilun/Sharp-Memory-LCD-Kernel-Driver

Sharp datasheet:

	https://www.sharpsde.com/fileadmin/products/Displays/2016_SDE_App_Note_for_Memory_LCD_programming_V1.3.pdf
