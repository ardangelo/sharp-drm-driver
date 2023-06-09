# Sharp Memory LCD Kernel Driver

**Note**: I did not write this driver. I only modified it to clean up compiler warnings/errors. The original can be found here:
(http://www.librecalc.com/en/wp-content/uploads/sites/4/2014/10/sharp.c)

More information can be found here:
(http://www.librecalc.com/en/downloads/)

This driver is for the LS027B7DH01. It *should* work with other Sharp Mem LCD displays by modifying all 400/240 references with the correct dimensions for your screen.

## Hookup Guide
Connect the following pins:

Display | RasPi
------- | ---------
VIN     | 3.3V      
3V3     | N/C       
GND     | GND       
SCLK    | 11 (SCLK) 
MOSI    | 10 (MOSI) 
CS      | 23        
EXTMD   | 3.3V      
DISP    | 24        
EXTIN   | 25        

## Compile/Install the driver
Verify that you have the linux kernel headers for your platform. For the RasPi these can be obtained by:
```
sudo apt-get install raspberrypi-kernel-headers
```
or more generally:
```
sudo apt-get install linux-headers-$(uname -r)
```

To compile the driver, run:
```
make
```

To install the driver, run:
```
sudo make modules_install
```

If you want the module to load at boot you'll need to add it to the /etc/modules file, like:
```
...
# This file contains...
# at boot time...
sharp
```

## Compile/Install the Device Tree Overlay
The included sharp.dts file is for the Raspberry Pi Zero W. To compile it, run:
```
dtc -@ -I dts -O dtb -o sharp.dtbo sharp.dts
```

To load it at runtime, copy it to /boot/overlays:
```
sudo cp sharp.dtbo /boot/overlays
```

And then add the following line to /boot/config.txt:
```
dtoverlay=sharp
```

## Console on Display
If you want the boot console to show up on the display, you'll need to append `fbcon=map:10` to /boot/cmdline.txt after *rootwait*, like:
```
... rootwait ... fbcon=map:10
```

To make sure the console fits on screen, uncomment the following lines in /boot/config.txt and set the resolution appropriately:
```
framebuffer_width=400
framebuffer_height=240
```
