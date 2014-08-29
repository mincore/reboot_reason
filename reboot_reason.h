/*
 * Record reboot reason
 *
 * Copyright (c) 2014 csp <mincore@163.com>
 *
 * This software may be used and distributed according to the terms
 * of the GNU Public License, incorporated herein by reference.
 */

#ifndef _DH_PANIC_DUMP
#define _DH_PANIC_DUMP

enum REBOOT_REASON {
	REBOOT_REASON_UNKNOWN	= 0,	/* 非正常重启，未知原因，包括主板掉电后开机 */
	REBOOT_REASON_REBOOT	= 1,	/* 正常重启 */
	REBOOT_REASON_EMERG		= 2,	/* 非正常重启，主动让南桥发出reset信号 */
	REBOOT_REASON_WDT		= 3,	/* 非正常重启，看门狗到期重启 */
};

struct cmos_ops {
	int (*read)(u8 off, u8 *buf, u8 count);
	int (*write)(u8 off, const u8 *buf, u8 count);
};

int reboot_dumper_init(struct cmos_ops *ops, u8 off, u8 cnt, const char *kmsg_path);
int reboot_dumper_record(enum REBOOT_REASON reason);

#endif
