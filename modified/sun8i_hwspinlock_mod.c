// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sun8i_hwspinlock.c - hardware spinlock driver for Allwinner SoCs
 * Copyright (C) 2020 Wilken Gottwalt <wilken.gottwalt@posteo.net>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "hwspinlock_internal.h"

#define DRIVER_NAME		"sun8i_hwspinlock_mod"

#define SPINLOCK_BASE_ID	0 /* there is only one hwspinlock device per SoC */
#define SPINLOCK_SYSSTATUS_REG	0x0000
#define SPINLOCK_NOTTAKEN	0

struct sun8i_hwspinlock_mod_data {
	struct hwspinlock_device *bank;
	struct reset_control *reset;
	struct clk *ahb_clk;
	struct dentry *debugfs;
	int nlocks;
};

#ifdef CONFIG_DEBUG_FS

static int hwlocks_supported_show(struct seq_file *seqf, void *unused)
{
	struct sun8i_hwspinlock_mod_data *priv = seqf->private;

	seq_printf(seqf, "%d\n", priv->nlocks);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hwlocks_supported);

static void sun8i_hwspinlock_mod_debugfs_init(struct sun8i_hwspinlock_mod_data *priv)
{
	priv->debugfs = debugfs_create_dir(DRIVER_NAME, NULL);
	debugfs_create_file("supported", 0444, priv->debugfs, priv, &hwlocks_supported_fops);
}

#else

static void sun8i_hwspinlock_mod_debugfs_init(struct sun8i_hwspinlock_mod_data *priv)
{
}

#endif

static int sun8i_hwspinlock_mod_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	return (readl(lock_addr) == SPINLOCK_NOTTAKEN);
}

static void sun8i_hwspinlock_mod_unlock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	writel(SPINLOCK_NOTTAKEN, lock_addr);
}

static const struct hwspinlock_ops sun8i_hwspinlock_mod_ops = {
	.trylock	= sun8i_hwspinlock_mod_trylock,
	.unlock		= sun8i_hwspinlock_mod_unlock,
};

static void sun8i_hwspinlock_disable(void *data)
{
	struct sun8i_hwspinlock_mod_data *priv = data;

	debugfs_remove_recursive(priv->debugfs);
	reset_control_assert(priv->reset);
	clk_disable_unprepare(priv->ahb_clk);
}

static int sun8i_hwspinlock_mod_probe(struct platform_device *pdev)
{
	struct sun8i_hwspinlock_mod_data *priv;
	struct hwspinlock *hwlock;
	void __iomem *io_base;
	void __iomem *io_locks;
	u32 num_banks;
	int err, i;

	io_base = devm_platform_ioremap_resource(pdev, SPINLOCK_BASE_ID);
	if (IS_ERR(io_base)) {
		err = PTR_ERR(io_base);
		dev_err(&pdev->dev, "unable to request first MMIO (%d)\n", err);
		return err;
	}

	io_locks = devm_platform_ioremap_resource(pdev, SPINLOCK_BASE_ID + 1);
	if (IS_ERR(io_locks)) {
		err = PTR_ERR(io_locks);
		dev_err(&pdev->dev, "unable to request second MMIO (%d)\n", err);
		return err;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	err = devm_add_action_or_reset(&pdev->dev, sun8i_hwspinlock_disable, priv);
	if (err) {
		dev_err(&pdev->dev, "unable to add disable action\n");
		return err;
	}

	priv->ahb_clk = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(priv->ahb_clk)) {
		err = PTR_ERR(priv->ahb_clk);
		dev_err(&pdev->dev, "unable to get AHB clock (%d)\n", err);
		return err;
	}

	priv->reset = devm_reset_control_get_optional(&pdev->dev, "ahb");
	if (IS_ERR(priv->reset)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->reset),
				     "unable to get reset control\n");
	}

	err = reset_control_deassert(priv->reset);
	if (err) {
		dev_err(&pdev->dev, "deassert reset control failure (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(priv->ahb_clk);
	if (err) {
		dev_err(&pdev->dev, "unable to prepare AHB clock (%d)\n", err);
		return err;
	}

	/*
	 * bit 28 and 29 hold the amount of spinlock banks, but at the same time the datasheet
	 * says, bit 30 and 31 are reserved while the values can be 0 to 4, which is not reachable
	 * by two bits alone, so the reserved bits are also taken into account
	 */
	num_banks = readl(io_base + SPINLOCK_SYSSTATUS_REG) >> 28;
	switch (num_banks) {
	case 1 ... 4:
		/*
		 * 32, 64, 128 and 256 spinlocks are supported by the hardware implementation,
		 * though most of the SoCs support 32 spinlocks only
		 */
		priv->nlocks = 1 << (4 + num_banks);
		break;
	default:
		dev_err(&pdev->dev, "unsupported hwspinlock setup (%d)\n", num_banks);
		return -EINVAL;
	}

	priv->bank = devm_kzalloc(&pdev->dev, struct_size(priv->bank, lock, priv->nlocks),
				  GFP_KERNEL);
	if (!priv->bank)
		err = -ENOMEM;

	for (i = 0; i < priv->nlocks; ++i) {
		hwlock = &priv->bank->lock[i];
		hwlock->priv = io_locks + sizeof(u32) * i;
	}

	sun8i_hwspinlock_mod_debugfs_init(priv);
	platform_set_drvdata(pdev, priv);

	return devm_hwspin_lock_register(&pdev->dev, priv->bank, &sun8i_hwspinlock_mod_ops,
					 SPINLOCK_BASE_ID, priv->nlocks);
}

static const struct of_device_id sun8i_hwspinlock_mod_ids[] = {
	{ .compatible = "allwinner,sun8i-hwspinlock-mod", },
	{},
};
MODULE_DEVICE_TABLE(of, sun8i_hwspinlock_mod_ids);

static struct platform_driver sun8i_hwspinlock_mod_driver = {
	.probe	= sun8i_hwspinlock_mod_probe,
	.driver	= {
		.name		= DRIVER_NAME,
		.of_match_table	= sun8i_hwspinlock_mod_ids,
	},
};

static int __init sun8i_hwspinlock_mod_init(void)
{
	return platform_driver_register(&sun8i_hwspinlock_mod_driver);
}
module_init(sun8i_hwspinlock_mod_init);

static void __exit sun8i_hwspinlock_mod_exit(void)
{
	platform_driver_unregister(&sun8i_hwspinlock_mod_driver);
}
module_exit(sun8i_hwspinlock_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SUN8I hardware spinlock enhanced driver");
MODULE_AUTHOR("Wilken Gottwalt <wilken.gottwalt@posteo.net>");
