/*
 * Record reboot reason
 *
 * Copyright (c) 2014 csp <mincore@163.com>
 *
 * This software may be used and distributed according to the terms
 * of the GNU Public License, incorporated herein by reference.
 */

#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/kmsg_dump.h>
#include <asm/uaccess.h>
#include <linux/notifier.h>
#include <linux/oom.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/reboot_reason.h>

#define MAX_KMSG_PATH 128 
#define PANIC_DUMP_SIZE 64*1024

/* 
 * bit[15:13] --- 上上上次重启原因, bit15是OOM标志
 * bit[12:10] --- 上上次重启原因, bit12是OOM标志
 * bit[9:7]   --- 上次重启原因, bit9是OOM标志
 * bit[6:4]   --- 即将重启的原因, bit6是OOM标志
 * bit[3:0]   --- Magic 0101
 */
struct reboot_reason {
	u16 magic:4;
	u16 now:3;
	u16 last1:3;
	u16 last2:3;
	u16 last3:3;
};

#define MAGIC 0x5

static int system_ooming = 0;
static int system_rebooting = 0;

static int dh_oom_callback(struct notifier_block *nfb, unsigned long dummy, void *parm);
static int dh_reboot_callback(struct notifier_block *nfb, unsigned long dummy, void *parm);
static void dh_panic_callback(struct kmsg_dumper *dumper, enum kmsg_dump_reason reason);
static int reboot_dumper_update(enum REBOOT_REASON reason);

static int file_write(const char *file, int flags, const void *buf, ssize_t count, loff_t pos)
{
	mm_segment_t old_fs;
	int ret;
	struct file *filp;

	filp = filp_open(file, flags, 0600);
	if (!filp) {
		printk("%s, open %s failed\n", __func__, file);
		return -EINVAL;
	}

	old_fs = get_fs();
	set_fs(get_ds());
	ret = filp->f_op->write(filp, buf, count, &pos);
	set_fs(old_fs);

	filp_close(filp, NULL);

	return ret;
}

static struct notifier_block dh_oom_notifier = {
	.notifier_call = dh_oom_callback
};

static struct notifier_block dh_reboot_notifier = {
	.notifier_call = dh_reboot_callback
};

struct reboot_dumper {
	struct cmos_ops *cmos_ops;	
	u8 cmos_off;
	u8 cmos_cnt;

	struct kmsg_dumper kdumper;
	char kmsg_path[MAX_KMSG_PATH];
	char kmsg[PANIC_DUMP_SIZE];
};

static struct reboot_dumper dumper;

static bool reboot_dumper_load(u16 *reason)
{
	return 2 == dumper.cmos_ops->read(dumper.cmos_off, 
			(u8 *)reason, dumper.cmos_cnt);
}

static bool reboot_dumper_restore(u16 reason)
{
	return 2 == dumper.cmos_ops->write(dumper.cmos_off, 
			(const u8 *)&reason, dumper.cmos_cnt);
}

static int reboot_dumper_shift(void)
{
	u16 old, new;
	struct reboot_reason *r = (struct reboot_reason *)&old;

	if (!reboot_dumper_load(&old)) {
		printk("%s, cmos read failed\n", __func__);
		return -1;
	}
	
	if (r->magic != MAGIC)
		new = MAGIC;
	else
		new = ((old >> 4) << 7) | (old & 0xf);

	if (!reboot_dumper_restore(new)) {
		printk("%s, cmos write failed\n", __func__);
		return -1;
	}
	
	return 0;
}

static int reboot_dumper_oom(void)
{
	u16 old, new;
	
	if (!reboot_dumper_load(&old)) {
		printk("%s, cmos read failed\n", __func__);
		return -1;
	}
	
	new = old | (1 << 6);

	if (old == new)
		return 0;

	if (!reboot_dumper_restore(new)) {
		printk("%s, cmos write failed\n", __func__);
		return -1;
	}
	
	return 0;
}

/* 下面这个函数在可能重启前调用一次 */
static int reboot_dumper_update(enum REBOOT_REASON reason)
{
	u16 old, new;

	if (!reboot_dumper_load(&old)) {
		printk("%s, cmos read failed\n", __func__);
		return -1;
	}
	
	new = ((old >> 6) << 6) | (reason << 4) | (old & 0xf);
	
	if (old == new)
		return 0;
	
	if (!reboot_dumper_restore(new)) {
		printk("%s, cmos write failed\n", __func__);
		return -1;
	}	

	return 0;
}

static int dh_cmos_record(int reason)
{
	return reboot_dumper_update(reason);
}

static void do_show_reboot_reason(struct seq_file *m, const char *prefix, unsigned char x)
{
	const char *strings[] = {
		"unknown", "normal reboot", "emergency restart", "hard wachdog reboot",
	};

	seq_printf(m, "%s: %s%s\n", prefix, strings[x & 3],
			(x & 4) ? ", oom" : "");
}

static int show_reboot_reason(struct seq_file *m, void *v) 
{
	u16 old;
	struct reboot_reason *r = (struct reboot_reason *)&old;

	if (!reboot_dumper_load(&old)) {
		printk("%s, cmos read failed\n", __func__);
		return -1;
	}

	if (r->magic != MAGIC)	{
		printk("%s magic %d does not match\n", __func__, r->magic);
		return -1;
	}

	seq_printf(m, "reboot stack\n");
	do_show_reboot_reason(m, "0", r->last1);
	do_show_reboot_reason(m, "1", r->last2);
	do_show_reboot_reason(m, "2", r->last3);

	return 0;
}

static int dh_oom_callback(struct notifier_block *nfb, unsigned long dummy, void *parm)
{
	if (sysctl_panic_on_oom) {
		reboot_dumper_oom();
		system_ooming = 1;
	}
	return NOTIFY_OK;
}

static int dh_reboot_callback(struct notifier_block *nfb, unsigned long dummy, void *parm)
{
	reboot_dumper_update(REBOOT_REASON_REBOOT);
	system_rebooting = 1;	
	return NOTIFY_OK; 
}

static void dh_panic_callback(struct kmsg_dumper *kdumper, enum kmsg_dump_reason reason)
{
	struct reboot_dumper *pdump;
	size_t text_len;
	const char *name;
	char filename[128];
	static atomic_t panicking = ATOMIC_INIT(0);
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;
	
	switch (reason) {
	case KMSG_DUMP_OOPS:
		name = "oops";
		break;
	case KMSG_DUMP_PANIC:
		atomic_add(1, &panicking);
		name = "panic";
		break;
	case KMSG_DUMP_EMERG:
		dh_cmos_record(REBOOT_REASON_EMERG);
		return;
	default:
		return;
	}

	if (atomic_read(&panicking) > 1 || system_ooming || system_rebooting)
		return;

	if (!spin_trylock_irqsave(&lock, flags))
		return;

	pdump = container_of(kdumper, struct reboot_dumper, kdumper);
	kmsg_dump_get_buffer(kdumper, false, pdump->kmsg, sizeof(pdump->kmsg), &text_len);
	snprintf(filename, sizeof(filename), "%s/%s", pdump->kmsg_path, name);
	printk("dumping kernel %s message to file: %s, text_len = %zd\n", name,
			filename, text_len);

	local_irq_enable();
	file_write(filename, O_RDWR | O_CREAT | O_SYNC | O_TRUNC, pdump->kmsg, text_len, 0);
	local_irq_disable();

	spin_unlock_irqrestore(&lock, flags);
}

static void test_oom(void)
{
	while (1) {
		kmalloc(1023*11, GFP_KERNEL);
	}
}

static void test_oops(void)
{
	char *p = NULL;
	*p = 1;
}

static char* str_skip_whitespace(char *s)
{
	while (*s == ' ' || *s == '\t') 
		++s;

	return s;
}
	
static char* str_next_word(char **buf)
{
	int len;
	char *word;

	word = str_skip_whitespace(*buf);

	if (*word == '\0')
		return NULL;

	len = strcspn(word, " \t\n");

	if (word[len] != '\0')
		word[len++] = '\0';

	*buf = str_skip_whitespace(word + len);

	return word;
}

static ssize_t proc_reboot_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	char tmp[8];
	char *str = tmp;
	char *key;
	size_t size = min(sizeof(tmp), count);

	memset(tmp, 0, sizeof(tmp));
	if (copy_from_user(tmp, buf, size))
		return -EFAULT;
		
	key = str_next_word(&str);
	if (!key)
		return -EPERM;

	if (strcmp(key, "oom") == 0) {
		test_oom();
		return count;
	}

	if (strcmp(key, "oops") == 0) {
		test_oops();
		return count;
	}

	return -EINVAL;
}

static int proc_reboot_open(struct inode *inode, struct file *file)
{
    return single_open(file, show_reboot_reason, NULL);
}

static const struct file_operations test_ops = { 
    .owner      = THIS_MODULE,
    .open       = proc_reboot_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = single_release,
    .write		= proc_reboot_write,
};

static int __init dh_panic_dump_init(void)
{
	register_oom_notifier(&dh_oom_notifier);
	register_reboot_notifier(&dh_reboot_notifier);
	kmsg_dump_register(&dumper.kdumper);

	proc_create("reboot", S_IWUSR, NULL, &test_ops);

	return 0;
}

static void __exit dh_panic_dump_exit(void)
{
}

int reboot_dumper_init(struct cmos_ops *ops, u8 off, u8 cnt, const char *kmsg_path)
{
	dumper.cmos_ops = ops;
	dumper.cmos_off = off;
	dumper.cmos_cnt = cnt;
	dumper.kdumper.dump = dh_panic_callback,

	snprintf(dumper.kmsg_path, MAX_KMSG_PATH, "%s", kmsg_path);
	reboot_dumper_shift();
	return 0;
}

int reboot_dumper_record(enum REBOOT_REASON reason)
{
	return reboot_dumper_update(reason);	
}

module_init(dh_panic_dump_init)
module_exit(dh_panic_dump_exit)
