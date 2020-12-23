// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sun6i_hwspinlock_test.c - hardware spinlock enhanced test module for sun6i_hwspinlock_mod driver
 * Copyright (C) 2020 Wilken Gottwalt <wilken.gottwalt@posteo.net>
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/hwspinlock.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>

#define DRIVER_NAME		"sun6i_hwspinlock_test2"

#define SPINLOCK_BASE_ID	0
#define BITSTR_LEN		36

#define START_LOCK		0
#define LOCKS			32
#define MAX_LOCKS		256
#define MIN_ATTEMPTS		1
#define MAX_ATTEMPTS		10
#define MIN_HOLDTIME		0
#define MAX_HOLDTIME		1000000
#define MIN_PRINTTIME		100
#define MAX_PRINTTIME		5000
#define MIN_LOOPS		1
#define MAX_LOOPS		10000
#define MAX_MODE		2

#define bit(val, bitnr) (((val) & (1 << (bitnr))) ? 1 : 0)

static int start_lock = START_LOCK;
module_param(start_lock, int, 0444);
MODULE_PARM_DESC(start_lock, "start at hwlock (default: 0 (0..255))");
static int max_locks = LOCKS;
module_param(max_locks, int, 0444);
MODULE_PARM_DESC(max_locks, "amount of hwlocks to test (default: 32 (1..256))");
static int attempts = 3;
module_param(attempts, int, 0444);
MODULE_PARM_DESC(attempts, "lock/unlock attempts per hwlock (default: 3 (1..10))");
static int holdtime;
module_param(holdtime, int, 0444);
MODULE_PARM_DESC(holdtime, "time period to hold a lock in us (default: 0 (0..1000000))");
static int printtime = 1000;
module_param(printtime, int, 0444);
MODULE_PARM_DESC(printtime, "interval for status printk mode in ms (default: 1000 (500..5000))");
static int loops = MIN_LOOPS;
module_param(loops, int, 0444);
MODULE_PARM_DESC(loops, "amount of test loops to run (default: 1 (1..10000))");
static int mode;
module_param(mode, int, 0444);
MODULE_PARM_DESC(mode, "debugfs only, status printk, normal test, crust test (default: 0 (0..3))");

struct sun6i_hwspinlock_test2_data {
	struct dentry *debugfs;
	void __iomem *io_base;
	int slock;
	int mlocks;
	int attempts;
	int holdtime;
	int printtime;
	int loops;
	int statmode;
};

static void bit_string(struct sun6i_hwspinlock_test2_data *priv, char *str)
{
	u32 inuse = readl(priv->io_base);

	snprintf(str, BITSTR_LEN,
		 "%d%d%d%d%d%d%d%d_%d%d%d%d%d%d%d%d_%d%d%d%d%d%d%d%d_%d%d%d%d%d%d%d%d",
		 bit(inuse, 0), bit(inuse, 1), bit(inuse, 2), bit(inuse, 3),
		 bit(inuse, 4), bit(inuse, 5), bit(inuse, 6), bit(inuse, 7),
		 bit(inuse, 8), bit(inuse, 9), bit(inuse, 10), bit(inuse, 11),
		 bit(inuse, 12), bit(inuse, 13), bit(inuse, 14), bit(inuse, 15),
		 bit(inuse, 16), bit(inuse, 17), bit(inuse, 18), bit(inuse, 19),
		 bit(inuse, 20), bit(inuse, 21), bit(inuse, 22), bit(inuse, 23),
		 bit(inuse, 24), bit(inuse, 25), bit(inuse, 26), bit(inuse, 27),
		 bit(inuse, 28), bit(inuse, 29), bit(inuse, 30), bit(inuse, 31));
}

#ifdef CONFIG_DEBUG_FS

static int hwlocks_inuse_show(struct seq_file *seqf, void *unused)
{
	struct sun6i_hwspinlock_test2_data *priv = seqf->private;
	char bitstr[BITSTR_LEN];

	bit_string(priv, bitstr);
	seq_printf(seqf, "%s\n", bitstr);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hwlocks_inuse);

static void sun6i_hwspinlock_test2_debugfs_init(struct sun6i_hwspinlock_test2_data *priv)
{
	priv->debugfs = debugfs_create_dir(DRIVER_NAME, NULL);
	debugfs_create_file("inuse", 0444, priv->debugfs, priv, &hwlocks_inuse_fops);
}

#else

static void sun6i_hwspinlock_debugfs_init(struct sun6i_hwspinlock_test2_data *priv)
{
}

#endif

static int sun6i_hwspinlock_test2_print_status(struct sun6i_hwspinlock_test2_data *priv)
{
	char bitstr[BITSTR_LEN];

	while (priv->loops) {
		--priv->loops;
		bit_string(priv, bitstr);
		pr_info("[sreg] %s\n", bitstr);
		msleep(priv->printtime);
	}

	return 0;
}

static char __bitstr[BITSTR_LEN];

static int sun6i_hwspinlock_test2_lock(struct sun6i_hwspinlock_test2_data *priv,
				       struct hwspinlock *hwlock)
{
	int i, err;

	pr_info("[test] testing lock %d\n", hwspin_lock_get_id(hwlock));
	for (i = 0; i < priv->attempts; ++i) {
		if (priv->statmode) {
			bit_string(priv, __bitstr);
			pr_info("[sreg] before take %s\n", __bitstr);
		}
		err = hwspin_trylock(hwlock);
		if (err) {
			pr_info("[test] taking lock attempt #%d failed (%d)\n", i, err);
			return -EFAULT;
		}
		udelay(priv->holdtime);
		if (priv->statmode) {
			bit_string(priv, __bitstr);
			pr_info("[sreg] after take %s\n", __bitstr);
		}

		err = hwspin_trylock(hwlock);
		if (!err) {
			hwspin_unlock(hwlock);
			hwspin_unlock(hwlock);
			pr_info("[test] recursive taking lock attempt #%d should not happen\n", i);
			return -EFAULT;
		}
		if (priv->statmode) {
			bit_string(priv, __bitstr);
			pr_info("[sreg] after recursive take %s\n", __bitstr);
		}

		hwspin_unlock(hwlock);
		err = hwspin_trylock(hwlock);
		if (err) {
			pr_info("[test] untake lock attempt #%d failed (%d)\n", i, err);
			return -EINVAL;
		}
		hwspin_unlock(hwlock);
		if (priv->statmode) {
			bit_string(priv, __bitstr);
			pr_info("[sreg] after untake %s\n", __bitstr);
		}
		pr_info("[test]+++ attempt #%d succeded\n", i);
	}

	return 0;
}

static int sun6i_hwspinlock_test2_run(struct sun6i_hwspinlock_test2_data *priv)
{
	struct hwspinlock *hwlock;
	int i, res, err;

	pr_info("[run ]--- testing locks %d to %d ---\n", priv->slock, priv->slock + priv->mlocks);
	for (; priv->loops > 0; --priv->loops) {
		for (i = priv->slock; i < (priv->slock + priv->mlocks); ++i) {
			hwlock = hwspin_lock_request_specific(i);
			if (!hwlock) {
				pr_info("[run ]--- requesting specific lock %d failed ---\n", i);
				err = -EIO;
				continue;
			}

			res = sun6i_hwspinlock_test2_lock(priv, hwlock);
			if (res) {
				pr_info("[run ]--- testing specific lock %d failed (%d) ---\n", i,
					res);
				err = res;
			}

			res = hwspin_lock_free(hwlock);
			if (res) {
				pr_info("[run ]--- releasing specific lock %d failed (%d) ---\n", i,
					res);
				err = res;
			}
		}
	}

	return err;
}

static int sun6i_hwspinlock_test2_probe(struct platform_device *pdev)
{
	struct sun6i_hwspinlock_test2_data *priv;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->io_base = devm_platform_ioremap_resource(pdev, SPINLOCK_BASE_ID);
	if (IS_ERR(priv->io_base))
		return PTR_ERR(priv->io_base);

	if (start_lock < 0 || start_lock > (MAX_LOCKS - 1))
		priv->slock = START_LOCK;

	if (max_locks < 1 || max_locks > MAX_LOCKS)
		priv->mlocks = MAX_LOCKS - start_lock;
	else
		priv->mlocks = max_locks - start_lock;

	if (attempts < MIN_ATTEMPTS)
		priv->attempts = MIN_ATTEMPTS;
	else if (attempts > MAX_ATTEMPTS)
		priv->attempts = MAX_ATTEMPTS;
	else
		priv->attempts = attempts;

	if (holdtime < 0)
		priv->holdtime = MIN_HOLDTIME;
	else if (holdtime > MAX_HOLDTIME)
		priv->holdtime = MAX_HOLDTIME;
	else
		priv->holdtime = holdtime;

	if (printtime < MIN_PRINTTIME)
		priv->printtime = MIN_PRINTTIME;
	else if (printtime > MAX_PRINTTIME)
		priv->printtime = MAX_PRINTTIME;
	else
		priv->printtime = printtime;

	if (loops < MIN_LOOPS)
		priv->loops = MIN_LOOPS;
	else if (loops > MAX_LOOPS)
		priv->loops = MAX_LOOPS;
	else
		priv->loops = loops;

	if (mode == 3)
		priv->statmode = 1;
	else
		priv->statmode = 0;

	sun6i_hwspinlock_test2_debugfs_init(priv);
	platform_set_drvdata(pdev, priv);

	switch (mode) {
	case 0:
		return 0;

	case 1:
		return sun6i_hwspinlock_test2_print_status(priv);

	case 2 ... 3:
		return sun6i_hwspinlock_test2_run(priv);

	default:
		dev_err(&pdev->dev, "unknown mode (%d)\n", mode);
		return -ENODEV;
	}

	return 0;
}

static int sun6i_hwspinlock_test2_remove(struct platform_device *pdev)
{
	struct sun6i_hwspinlock_test2_data *priv = platform_get_drvdata(pdev);

	debugfs_remove_recursive(priv->debugfs);

	return 0;
}

static const struct of_device_id sun6i_hwspinlock_test2_ids[] = {
	{ .compatible = "allwinner,sun6i-a31-hwspinlock-stat", },
	{},
};
MODULE_DEVICE_TABLE(of, sun6i_hwspinlock_test2_ids);

static struct platform_driver sun6i_hwspinlock_test2_driver = {
	.probe	= sun6i_hwspinlock_test2_probe,
	.remove	= sun6i_hwspinlock_test2_remove,
	.driver	= {
		.name		= DRIVER_NAME,
		.of_match_table	= sun6i_hwspinlock_test2_ids,
	},
};

static int __init sun6i_hwspinlock_test2_init(void)
{
	pr_info("[init]--- SUN6I HWSPINLOCK DRIVER ENHANCED TEST ---\n");

	return platform_driver_register(&sun6i_hwspinlock_test2_driver);
}
module_init(sun6i_hwspinlock_test2_init);

static void __exit sun6i_hwspinlock_test2_exit(void)
{
	pr_info("[exit]--- SUN6I HWSPINLOCK DRIVER ENHANCED TEST ---\n");
	platform_driver_unregister(&sun6i_hwspinlock_test2_driver);
}
module_exit(sun6i_hwspinlock_test2_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SUN6I hardware spinlock enhanced test driver");
MODULE_AUTHOR("Wilken Gottwalt <wilken.gottwalt@posteo.net>");
