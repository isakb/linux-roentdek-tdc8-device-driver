This is a Linux device driver for a prototype time-to-digital
conversion card.

The driver has been tested with Ubuntu 8.04 with a real-time
kernel. I have not maintained it since 2008. Feel free to try it out
if you find it useful, but use at your own risk.

Usage
=====

To compile:
   make

To load the module into the kernel:
   sudo ./tdc_load

To unload the module:
   sudo ./tdc_unload

These scripts also create `/dev/tdc` which is used to access the TDC card.




This project is not maintained at the moment.
