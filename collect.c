/*
 * Record reboot reason
 *
 * Copyright (c) 2014 csp <mincore@163.com>
 *
 * This software may be used and distributed according to the terms
 * of the GNU Public License, incorporated herein by reference.
 */

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define CLLOECT_KMSG	1
#define COLLECT_MEMINFO 2
#define COLLECT_MAPS	4
#define COLLECT_FD		8

static int cp(const char *from, const char *to)
{
	char buf[8192];
	int fd_in;
	int fd_out;
	int len;
	int ret = 0;

	fd_in = open(from, O_RDONLY);
	if (fd_in == -1)
		return -1;
	
	fd_out = open(to, O_WRONLY | O_CREAT, 0644);
	if (fd_out == -1) {
		close(fd_in);
		return -1;
	}

	while (1) {
		len = read(fd_in, buf, sizeof(buf));
		if (len <= 0)
			break;
		len = write(fd_out, buf, len);
		if (len <= 0)
			break;
		ret += len;
	}

	close(fd_in);
	close(fd_out);

	return ret;
}

static int dump_links(const char *from, const char *to)
{
	FILE *fp;
	char buf[1024];
	struct stat s;
	int n;

	if (stat(from, &s) == -1)
		return -1;

	fp = fopen(to, "w");
	if (!fp)
		return -1;

	if (!S_ISDIR(s.st_mode)) {
		n = readlink(from, buf, sizeof(buf)-1);
		if (n != -1) {
			buf[n] = 0;
			fprintf(fp, "%s %s\n", from, buf);
		}
	} else {
		DIR *dir = NULL;
		struct dirent *ent = NULL;
		char file[128];

		dir = opendir(from);
		if (dir == NULL) {
			fclose(fp);
			return -1;
		}

		while ((ent = readdir(dir))) {
			if (ent->d_name[0] == '.')
				continue;
			snprintf(file, sizeof(file), "%s/%s", from, ent->d_name);
			n = readlink(file, buf, sizeof(buf)-1);
			if (n != -1) {
				buf[n] = 0;
				fprintf(fp, "%s %s\n", ent->d_name, buf);
			}
		}

		closedir(dir);
	}

	fclose(fp);

	return 0;
}

static int collect_system_info(const char *dir, int flags)
{
	char from[128];
	char to[128];

	if (flags & CLLOECT_KMSG) {
		snprintf(from, sizeof(from), "/mnt/sys_log/messages");
		snprintf(to, sizeof(to), "%s/kmsg", dir);
		cp(from, to);
	}

	if (flags & COLLECT_MEMINFO) {
		snprintf(from, sizeof(from), "/proc/meminfo");
		snprintf(to, sizeof(to), "%s/meminfo", dir);
		cp(from, to);
	}

	return 0;
}

static int collect_process_info(const char *dir, int pid, int flags)
{
	char from[128];
	char to[128];

	if (flags & COLLECT_MAPS) {
		snprintf(from, sizeof(from), "/proc/%d/maps", pid);
		snprintf(to, sizeof(to), "%s/%d_maps", dir, pid);
		cp(from, to);
	}

	if (flags & COLLECT_FD) {
		snprintf(from, sizeof(from), "/proc/%d/fd", pid);
		snprintf(to, sizeof(to), "%s/%d_fd", dir, pid);
		dump_links(from, to);
	}
	
	return 0;
}

static int collect(const char *dir, int *pids, int npid, int flags)
{
	struct stat s;
	int i;

	mkdir(dir, 0644);
	if (stat(dir, &s) == -1)
		return -1;
	if (!S_ISDIR(s.st_mode))
		return -1;

	collect_system_info(dir, flags);

	for (i=0; i<npid; i++)
		collect_process_info(dir, pids[i], flags);

	return 0;
}

static int strargv(char *str, const char *split, char **argv, int size)
{
	char *token, *saveptr;
	int argc;

	for (argc = 0; argc < size; str = NULL) {
		token = strtok_r(str, split, &saveptr);
		if (!token)
			break;
		argv[argc++] = token;
	}

	return argc;
}

static void usage(const char *str)
{
	printf("usage: %s -d /mnt/mtd/sysinfo -p pid1,pid2, -f 15\n", str);
}

int main(int argc, char **argv)
{
	char *dir = NULL;
	char *strpids = NULL;
	char *pidv[32];
	int pids[32];
	int npid = 0;
	int flags = 15;
	int opt;
	int i;
	
	while ((opt = getopt(argc, argv, "d:p:f:")) != -1) {
		switch (opt) {
		case 'd':
			dir = optarg;
			break;
		case 'p':
			strpids = optarg;
			break;
		case 'f':
			flags = atoi(optarg);
			break;
		}
	}

	if (!dir) {
		usage(argv[0]);
		return -1;
	}

	if (strpids) {
		npid = strargv(strpids, ",", pidv, 32);
		for (i=0; i<npid; i++)
			pids[i] = atoi(pidv[i]);
	}

	return collect(dir, pids, npid, flags); 
}
