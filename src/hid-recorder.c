/*
 * Hid replay / recorder
 *
 * Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright (c) 2012 Red Hat, Inc.
 *
 * Based on: "Hidraw Userspace Example" copyrighted as this:
 *   Copyright (c) 2010 Alan Ott <alan@signal11.us>
 *   Copyright (c) 2010 Signal 11 Software
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

/* Linux */
#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>

/* Unix */
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

/* C */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define _GNU_SOURCE
#include <errno.h>
extern char *program_invocation_name;
extern char *program_invocation_short_name;

/**
 * Print usage information.
 */
static int usage(void)
{
	printf("USAGE:\n");
	printf("   %s /dev/hidrawX\n", program_invocation_short_name);

	return EXIT_FAILURE;
}

static const struct option long_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ 0, },
};

/* Return 1 if the difference is negative, otherwise 0.  */
int timeval_subtract(struct timeval *result, struct timeval *t2, struct timeval *t1)
{
	long int diff = (t2->tv_usec + 1000000 * t2->tv_sec) - (t1->tv_usec + 1000000 * t1->tv_sec);
	result->tv_sec = diff / 1000000;
	result->tv_usec = diff % 1000000;

	return (diff < 0);
}

int main(int argc, char **argv)
{
	int fd;
	int i, res, desc_size = 0;
	char buf[4096];
	struct hidraw_report_descriptor rpt_desc;
	struct hidraw_devinfo info;
	char *device;
	struct timeval starttime, currenttime;

	while (1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "h", long_options, &option_index);
		if (c == -1)
			break;
		return usage();
	}

	if (optind < argc)
		device = argv[optind++];
	else {
		fprintf(stderr, "no hidraw device provided\n");
		return usage();
	}

	fd = open(device, O_RDWR);

	if (fd < 0) {
		perror("Unable to open device");
		return 1;
	}

	memset(&rpt_desc, 0x0, sizeof(rpt_desc));
	memset(&info, 0x0, sizeof(info));
	memset(buf, 0x0, sizeof(buf));
	memset(&starttime, 0x0, sizeof(starttime));

	/* Get Report Descriptor Size */
	res = ioctl(fd, HIDIOCGRDESCSIZE, &desc_size);
	if (res < 0)
		perror("HIDIOCGRDESCSIZE");

	/* Get Report Descriptor */
	rpt_desc.size = desc_size;
	res = ioctl(fd, HIDIOCGRDESC, &rpt_desc);
	if (res < 0) {
		perror("HIDIOCGRDESC");
	} else {
		printf("R: %d", desc_size);
		for (i = 0; i < rpt_desc.size; i++)
			printf(" %02hhx", rpt_desc.value[i]);
		printf("\n");
	}

	/* Get Raw Name */
	res = ioctl(fd, HIDIOCGRAWNAME(256), buf);
	if (res < 0)
		perror("HIDIOCGRAWNAME");
	else
		printf("N: %s\n", buf);

	/* Get Physical Location */
	res = ioctl(fd, HIDIOCGRAWPHYS(256), buf);
	if (res < 0)
		perror("HIDIOCGRAWPHYS");
	else
		printf("P: %s\n", buf);

	/* Get Raw Info */
	res = ioctl(fd, HIDIOCGRAWINFO, &info);
	if (res < 0) {
		perror("HIDIOCGRAWINFO");
	} else {
		printf("I: %x %04hx %04hx\n", info.bustype, info.vendor, info.product);
	}

	while (1) {
		/* Get a report from the device */
		res = read(fd, buf, sizeof(buf));
		if (res < 0) {
			perror("read");
			break;
		} else {
			if (!starttime.tv_sec && !starttime.tv_usec)
				gettimeofday(&starttime, NULL);
			gettimeofday(&currenttime, NULL);
			timeval_subtract(&currenttime, &currenttime, &starttime);
			printf("E: %lu.%06u %d", currenttime.tv_sec, (unsigned)currenttime.tv_usec, res);
			for (i = 0; i < res; i++)
				printf(" %02hhx", buf[i]);
			printf("\n");
			fflush(stdout);
		}
	}
	close(fd);
	return 0;
}
