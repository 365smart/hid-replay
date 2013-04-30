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
 * the Free Software Foundation; either version 2 of the License, or
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

#define _GNU_SOURCE

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
#include <dirent.h>

/* C */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define _GNU_SOURCE
#include <errno.h>
extern char *program_invocation_name;
extern char *program_invocation_short_name;

#define DEV_DIR "/dev"
#define HIDRAW_DEV_NAME "hidraw"

#define HID_DBG_DIR "/sys/kernel/debug/hid"
#define HID_DBG_RDESC "rdesc"
#define HID_DBG_events "events"

enum hid_recorder_mode {
	MODE_HIDRAW,
	MODE_HID_DEBUGFS,
};

/**
 * Print usage information.
 */
static int usage(void)
{
	printf("USAGE:\n");
	printf("   %s [OPTION] [/dev/hidrawX]\n", program_invocation_short_name);

	printf("\n");
	printf("where OPTION is either:\n");
	printf("   -h or --help: print this message\n");
	printf("   -d or --debugfs: use HID debugfs instead of hidraw node (use this when\n"
		"                    no events are coming from hidraw while using the device)\n");
	return EXIT_FAILURE;
}

static const struct option long_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "debugfs", no_argument, NULL, 'd' },
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

/**
 * Filter for the AutoDevProbe scandir on /dev.
 *
 * @param dir The current directory entry provided by scandir.
 *
 * @return Non-zero if the given directory entry starts with "hidraw", or zero
 * otherwise.
 */
static int is_hidraw_device(const struct dirent *dir) {
	return strncmp(HIDRAW_DEV_NAME, dir->d_name, 6) == 0;
}

/**
 * Scans all /dev/hidraw*, display them and ask the user which one to
 * open.
 *
 * code taken from evtest.c
 *
 * @return The hidraw device file name of the device file selected. This
 * string is allocated and must be freed by the caller.
 */
static char* scan_devices(void)
{
	struct dirent **namelist;
	int i, ndev, devnum, res;
	char *filename;

	ndev = scandir(DEV_DIR, &namelist, is_hidraw_device, alphasort);
	if (ndev <= 0)
		return NULL;

	fprintf(stderr, "Available devices:\n");

	for (i = 0; i < ndev; i++)
	{
		char fname[64];
		int fd = -1;
		char name[256] = "???";

		snprintf(fname, sizeof(fname),
			 "%s/%s", DEV_DIR, namelist[i]->d_name);
		fd = open(fname, O_RDONLY);
		if (fd < 0) {
			free(namelist[i]);
			continue;
		}

		/* Get Raw Name */
		res = ioctl(fd, HIDIOCGRAWNAME(256), name);
		if (res >= 0)
			fprintf(stderr, "%s:	%s\n", fname, name);
		close(fd);
		free(namelist[i]);
	}

	free(namelist);

	fprintf(stderr, "Select the device event number [0-%d]: ", ndev - 1);
	scanf("%d", &devnum);

	if (devnum >= ndev || devnum < 0)
		return NULL;

	asprintf(&filename, "%s/%s%d",
		 DEV_DIR, HIDRAW_DEV_NAME,
		 devnum);

	return filename;
}

static int rdesc_match(struct hidraw_report_descriptor *rpt_desc, const char *str, int size)
{
	int i;
	int rdesc_size_str = (size - 1) / 3; /* remove terminating \0,
						2 chars per u8 + space (or \n for the last) */

	if (rdesc_size_str != rpt_desc->size)
		return 0;

	for (i = 0; i < rdesc_size_str; i++) {
		__u8 v;
		sscanf(&str[i*3], "%hhx ", &v);
		if (v != rpt_desc->value[i])
			break;
	}

	return i == rdesc_size_str;
}

static char* find_hid_dbg(struct hidraw_devinfo *info, struct hidraw_report_descriptor *rpt_desc)
{
	struct dirent **namelist;
	int i, ndev;
	char *filename = NULL;
	char target_name[16];
	char *buf_read = NULL;
	size_t buf_size = 0;

	snprintf(target_name, sizeof(target_name),
		 "%04d:%04X:%04X:", info->bustype, info->vendor, info->product);

	ndev = scandir(HID_DBG_DIR, &namelist, NULL, alphasort);
	if (ndev <= 0)
		return NULL;

	for (i = 0; i < ndev; i++)
	{
		char fname[256];
		FILE *file;
		char name[256] = "???";
		int size;

		snprintf(fname, sizeof(fname),
			 "%s/%s/rdesc", HID_DBG_DIR, namelist[i]->d_name);
		file = fopen(fname, "r");
		if (!file) {
			free(namelist[i]);
			continue;
		}

		/* Get Report Descriptor */
		size = getline(&buf_read, &buf_size, file);
		if (rdesc_match(rpt_desc, buf_read, size)) {
			filename = malloc(256);
			snprintf(filename, 256,
				 "%s/%s/events", HID_DBG_DIR, namelist[i]->d_name);
		}
		fclose(file);
		free(namelist[i]);
	}

	free(namelist);
	free(buf_read);

	return filename;
}

static int fetch_hidraw_information(int fd, struct hidraw_report_descriptor *rpt_desc,
		struct hidraw_devinfo *info, char *name, char *phys)
{
	int i, res, desc_size = 0;
	memset(rpt_desc, 0x0, sizeof(rpt_desc));
	memset(info, 0x0, sizeof(info));
	memset(name, 0x0, 256);
	memset(phys, 0x0, 256);

	/* Get Report Descriptor Size */
	res = ioctl(fd, HIDIOCGRDESCSIZE, &desc_size);
	if (res < 0) {
		perror("HIDIOCGRDESCSIZE");
		return EXIT_FAILURE;
	}

	/* Get Report Descriptor */
	rpt_desc->size = desc_size;
	res = ioctl(fd, HIDIOCGRDESC, rpt_desc);
	if (res < 0) {
		perror("HIDIOCGRDESC");
		return EXIT_FAILURE;
	} else {
		printf("R: %d", desc_size);
		for (i = 0; i < rpt_desc->size; i++)
			printf(" %02hhx", rpt_desc->value[i]);
		printf("\n");
	}

	/* Get Raw Name */
	res = ioctl(fd, HIDIOCGRAWNAME(256), name);
	if (res < 0) {
		perror("HIDIOCGRAWNAME");
		return EXIT_FAILURE;
	} else
		printf("N: %s\n", name);

	/* Get Physical Location */
	res = ioctl(fd, HIDIOCGRAWPHYS(256), phys);
	if (res < 0) {
		perror("HIDIOCGRAWPHYS");
		return EXIT_FAILURE;
	} else
		printf("P: %s\n", phys);

	/* Get Raw Info */
	res = ioctl(fd, HIDIOCGRAWINFO, info);
	if (res < 0) {
		perror("HIDIOCGRAWINFO");
		return EXIT_FAILURE;
	} else {
		printf("I: %x %04hx %04hx\n", info->bustype, info->vendor, info->product);
	}

	return EXIT_SUCCESS;
}

static void print_currenttime(struct timeval *starttime)
{
	struct timeval currenttime;
	if (!starttime->tv_sec && !starttime->tv_usec)
		gettimeofday(starttime, NULL);
	gettimeofday(&currenttime, NULL);
	timeval_subtract(&currenttime, &currenttime, starttime);

	printf("%lu.%06u", currenttime.tv_sec, (unsigned)currenttime.tv_usec);
}

static int read_hiddbg_event(FILE *file, struct timeval *starttime,
		char **buf_read, char **buf_write, size_t *buf_size)
{
	int size;
	int old_buf_size = *buf_size;

	/* Get a report from the device */
	size = getline(buf_read, buf_size, file);
	if (size < 0) {
		perror("read");
		return size;
	}

	if (old_buf_size != *buf_size) {
		if (old_buf_size)
			*buf_write = realloc(*buf_write, *buf_size);
		else
			*buf_write = malloc(*buf_size);
		if (!*buf_write) {
			perror("memory allocation");
			return -1;
		}
	}

	if (size > 8 && strncmp(*buf_read, "report ", 7) == 0) {
		int rsize;
		char numbered[16];
		sscanf(*buf_read, "report (size %d) (%[^)]) = %[^\n]\n", &rsize, numbered, *buf_write);
		printf("E: ");
		print_currenttime(starttime);
		printf(" %d %s\n", rsize, *buf_write);
		fflush(stdout);
	}
	return size;
}

static int read_hidraw_event(int fd, struct timeval *starttime)
{
	char buf[4096];
	struct timeval currenttime;
	int i, res;

	/* Get a report from the device */
	res = read(fd, buf, sizeof(buf));
	if (res < 0) {
		perror("read");
	} else {
		printf("E: ");
		print_currenttime(starttime);
		printf(" %d", res);

		for (i = 0; i < res; i++)
			printf(" %02hhx", buf[i]);
		printf("\n");
		fflush(stdout);
	}
	return res;
}

int main(int argc, char **argv)
{
	int fd;
	int ret;
	struct hidraw_report_descriptor rpt_desc;
	struct hidraw_devinfo info;
	char name[256];
	char phys[256];
	char *device;
	char *hid_dbg_event = NULL;
	struct timeval starttime;
	FILE *hid_dbg_file = NULL;
	char *buf_read = NULL;
	char *buf_write = NULL;
	size_t buf_size = 0;
	enum hid_recorder_mode mode = MODE_HIDRAW;

	while (1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "hd", long_options, &option_index);
		if (c == -1)
			break;
		switch (c) {
		case 'd':
			mode = MODE_HID_DEBUGFS;
			break;
		default:
			return usage();
		}
	}

	if (optind < argc)
		device = argv[optind++];
	else {
		if (getuid() != 0)
			fprintf(stderr, "Not running as root, some devices "
				"may not be available.\n");

		device = scan_devices();
		if (!device)
			return usage();
	}

	fd = open(device, O_RDWR);

	if (fd < 0) {
		perror("Unable to open device");
		return EXIT_FAILURE;
	}

	if (fetch_hidraw_information(fd, &rpt_desc, &info, name, phys) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	/* try to use hid debug sysfs instead of hidraw to retrieve the events */
	if (mode == MODE_HID_DEBUGFS)
		hid_dbg_event = find_hid_dbg(&info, &rpt_desc);

	if (hid_dbg_event) {
		fprintf(stderr, "reading debug interface %s instead of %s\n",
			hid_dbg_event, device);
		/* keep fd opened to keep the device powered */
		hid_dbg_file = fopen(hid_dbg_event, "r");
		if (!hid_dbg_file) {
			perror("Unable to open HID debug interface");
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	memset(&starttime, 0x0, sizeof(starttime));
	do {
		if (hid_dbg_file)
			ret = read_hiddbg_event(hid_dbg_file, &starttime, &buf_read, &buf_write, &buf_size);
		else
			ret = read_hidraw_event(fd, &starttime);
	} while (ret >= 0);

out:
	if (hid_dbg_event)
		free(hid_dbg_event);
	if (hid_dbg_file)
		fclose(hid_dbg_file);
	if (buf_size) {
		free(buf_read);
		free(buf_write);
	}
	close(fd);
	return ret;
}
