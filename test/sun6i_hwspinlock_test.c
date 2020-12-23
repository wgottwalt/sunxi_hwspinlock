// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sun6i_hwspinlock_test.c - hardware spinlock test module for sun6i_hwspinlock driver
 * Copyright (C) 2020 Wilken Gottwalt <wilken.gottwalt@posteo.net>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/hwspinlock.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/slab.h>

#define DRIVER_NAME		"sun6i_hwspinlock_test"

#define START_LOCK		0
#define LOCKS			32
#define MAX_LOCKS		256
#define ATTEMPTS		3
#define MAX_ATTEMPTS		10

static int start_lock = START_LOCK;
module_param(start_lock, int, 0444);
MODULE_PARM_DESC(start_lock, "start at hwlock (default: 0 (0..255))");
static int max_locks = LOCKS;
module_param(max_locks, int, 0444);
MODULE_PARM_DESC(max_locks, "amount of hwlocks to test (default: 32 (1..256))");
static int attempts = ATTEMPTS;
module_param(attempts, int, 0444);
MODULE_PARM_DESC(attempts, "lock/unlock attempts per hwlock (default: 3 (1..10))");
static int holdtime;
module_param(holdtime, int, 0444);
MODULE_PARM_DESC(holdtime, "time period to hold a lock in us (default: 0 (0..1000000))");

static int sun6i_hwspinlock_test_lock(struct hwspinlock *hwlock)
{
	int i, err;

	pr_info("[test] testing lock %d\n", hwspin_lock_get_id(hwlock));
	for (i = 0; i < attempts; ++i) {
		err = hwspin_trylock(hwlock);
		if (err) {
			pr_info("[test] taking lock attempt #%d failed (%d)\n", i, err);
			return -EFAULT;
		}
		udelay(holdtime);

		err = hwspin_trylock(hwlock);
		if (!err) {
			hwspin_unlock(hwlock);
			hwspin_unlock(hwlock);
			pr_info("[test] recursive taking lock attempt #%d should not happen\n", i);
			return -EFAULT;
		}

		hwspin_unlock(hwlock);
		err = hwspin_trylock(hwlock);
		if (err) {
			pr_info("[test] untake lock attempt #%d failed (%d)\n", i, err);
			return -EINVAL;
		}
		hwspin_unlock(hwlock);
		pr_info("[test]+++ attempt #%d succeded\n", i);
	}

	return 0;
}

static int sun6i_hwspinlock_test_run(void)
{
	struct hwspinlock *hwlock;
	int i, res, err;

	pr_info("[run ]--- testing locks %d to %d ---\n", start_lock, start_lock + max_locks - 1);
	for (i = start_lock; i < (start_lock + max_locks); ++i) {
		hwlock = hwspin_lock_request_specific(i);
		if (!hwlock) {
			pr_info("[run ]--- requesting specific lock %d failed ---\n", i);
			err = -EIO;
			continue;
		}

		res = sun6i_hwspinlock_test_lock(hwlock);
		if (res) {
			pr_info("[run ]--- testing specific lock %d failed (%d) ---\n", i, res);
			err = res;
		}

		res = hwspin_lock_free(hwlock);
		if (res) {
			pr_info("[run ]--- releasing specific lock %d failed (%d) ---\n", i, res);
			err = res;
		}
	}

	return err;
}

static const struct of_device_id sun6i_hwspinlock_test_ids[] = {
	{ .compatible = "allwinner,sun6i-a31-hwspinlock", },
	{},
};

static int __init sun6i_hwspinlock_test_init(void)
{
	struct device_node *np;

	pr_info("[init]--- SUN6I HWSPINLOCK DRIVER TEST ---\n");

	np = of_find_matching_node_and_match(NULL, sun6i_hwspinlock_test_ids, NULL);
	if (!np || !of_device_is_available(np)) {
		pr_info("[init] no known hwspinlock node found\n");
		return -ENODEV;
	}

	if (start_lock < 0 || start_lock > 255)
		start_lock = START_LOCK;

	if (max_locks < 1 || max_locks > 256)
		max_locks = MAX_LOCKS - start_lock;
	else
		max_locks = max_locks - start_lock;

	return sun6i_hwspinlock_test_run();
}
module_init(sun6i_hwspinlock_test_init);

static void __exit sun6i_hwspinlock_test_exit(void)
{
	pr_info("[exit]--- SUN6I HWSPINLOCK DRIVER TEST ---\n");
}
module_exit(sun6i_hwspinlock_test_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SUN6I hardware spinlock test driver");
MODULE_AUTHOR("Wilken Gottwalt <wilken.gottwalt@posteo.net>");
