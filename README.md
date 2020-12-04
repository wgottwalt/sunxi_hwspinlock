# Linux SUNXI hardware spinlock driver
This is the main development repository for the sunxi hardware spinlock
driver sunxi_hwspinlock. Most of the sun8i and sun50i include an OpenRisc
core (AR100) which can run in parallel to the ARM cores. There is also
an open firmware which can be used with that arisc core
(https://github.com/crust-firmware/crust).

The driver alone is kind of useless, you need it in combination with a
firmware running in the OpenRisc core. This driver will enable the
synchronization between the devices the ARM cores and the OpenRisc core
have access to.

Currently the driver is in a testing phase and needs modifications in
u-boot and the free crust firmware to be fully useable. For testing this
driver you need to build a modified crust firmware from my repo
(hwspinlock branch https://github.com/wgottwalt/crust). There in the
debug menu you need to enable the hwspinlock test loop. After that
you need to build a u-boot version with curst/SCP support. A u-boot
fork with crust support in the crust branch is available here
https://github.com/crust-firmware/u-boot.

The crust support does not include H2/H3 based devices yet, so if you
want to test this I ssuggest to use a H5 based device.

There are 4 drivers here. One is the real driver, the other 3 are there
for demonstrating that the hardware spinlock mechanism works. The test
kernel modules are used by loading them, which runs the test, and unload
them after the test is done.

### sunxi_hwspinlock.c:
This is the driver proposed for mainline. The hwspinlock drivers depend
on some internal data structures which are not easily accessable by
external drivers, so you need to add this one to the kernel to be able
to use it.

##### Kconfig:
```
config HWSPINLOCK_SUNXI
        tristate "SUNXI Hardware Spinlock device"
        depends on ARCH_SUNXI || COMPILE_TEST
        help
          Say y here to support the Allwinner hardware mutex device available
          in the H2+, H3, H5 and H6 SoCs.

          If unsure, say N.
```

##### Makefile:
```
obj-$(CONFIG_HWSPINLOCK_SUNXI)          += sunxi_hwspinlock.o
```

##### device tree (H3, H5, H6 dtsi, H6 -> 0x03004000):
```
hwspinlock: hwspinlock@1c18000 {
	compatible = "allwinner,sun50i-hwspinlock";
	reg = <0x01c18000 0x1000>;
	clocks = <&ccu CLK_BUS_SPINLOCK>;
	clock-names = "ahb";
	resets = <&ccu RST_BUS_SPINLOCK>;
	reset-names = "ahb";
	status = "okay";
};
```

### modified_sunxi_hwspinlock/sunxi_hwspinlock_mod.c:

Same rules apply for this one. This driver splits the 4k memory range into
2 ranges and keeps the SPINLOCK_STATUS register untouched. That register
can now be used by other modules for testing and/or debugging.

##### Kconfig:
```
config HWSPINLOCK_SUNXI_MOD
        tristate "SUNXI modified Hardware Spinlock device"
        depends on ARCH_SUNXI || COMPILE_TEST
        help
          If unsure, say N.
```

##### Makefile:
```
obj-$(CONFIG_HWSPINLOCK_SUNXI_MOD)      += sunxi_hwspinlock_mod.o
```

##### device tree (H3, H5, H6 dtsi, H6 -> 0x03004000):
```
hwspinlock-mod@1c18000 {
	compatible = "allwinner,sun50i-hwspinlock-mod";
	reg = <0x01c18000 0x4 0x01c18100 0x400>;
	clocks = <&ccu CLK_BUS_SPINLOCK>;
	clock-names = "ahb";
	resets = <&ccu RST_BUS_SPINLOCK>;
	reset-names = "ahb";
	status = "okay";
};
```

### test_module/sunxi_hwspinlock_test.c:
This is a very simple test module to demonstrate, that the Linux hwlock
abi works. It can be run with a normal u-boot build and with the modified
crust u-boot build. In the seconds case it shows that the crust firmware
takes the hwlocks and that the Linux driver is detecting this.

It can be build on the target device by calling `make` in the directory
or by using a cross-compiler
`ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=... make`.

Never compile this into the kernel, only use it as a module. Though it
can be used with the original and the modified driver.

### test2_module/sunxi_hwspinlock_test2.c
This is a much more complex test module which needs the modified driver
and makes use of the HWSPINLOCK_STATUS register to bypass the Linux hwlock
abi to show that hwlocks can be taken outside the kernel. This driver is
platform driver and needs a device tree entry.

##### device tree (H3, H5, H6 dtsi, H6 -> 0x03004010):
```
hwspinlock-stat@1c18010 {
	compatible = "allwinner,sun50i-hwspinlock-stat";
	reg = <0x01c18010 0x4>;
	status = "okay";
};
```
