
/*
http://askubuntu.com/questions/645/how-do-you-reset-a-usb-device-from-the-command-line
$ lsusb
Bus 001 Device 006: ID 0bda:2838 Realtek Semiconductor Corp. RTL2838 DVB-T
$ (sudo) ./reset_usb /dev/bus/usb/001/006
*/

#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

int main(int argc, char **argv) {
	const char *filename;
	int fd;

	filename = argv[1];
	fd = open(filename, O_WRONLY);
	ioctl(fd, USBDEVFS_RESET, 0);
	close(fd);

	return 0;
}
