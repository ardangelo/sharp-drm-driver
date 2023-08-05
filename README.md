# Sharp Memory LCD Kernel Driver

DRM kernel driver for 2.7" 400x240 Sharp memory LCD panel.

## Installation

Install the Linux kernel headers

	sudo apt-get install raspberrypi-kernel-headers


To build, install, and enable the kernel module:

    make
    sudo make install

To remove:

    sudo make uninstall


## [Original fbdev module readme with pinouts and build instructions](https://github.com/w4ilun/Sharp-Memory-LCD-Kernel-Driver/blob/master/README.md)

## References

Original SPI and GPIO kernel driver at:

	https://github.com/w4ilun/Sharp-Memory-LCD-Kernel-Driver

Sharp datasheet:

	https://www.sharpsde.com/fileadmin/products/Displays/2016_SDE_App_Note_for_Memory_LCD_programming_V1.3.pdf
