# Linux SUNXI hardware spinlock driver
This is the main development repository for the sunxi hardware spinlock
driver sun6i_hwspinlock. Since the Allwinner sun6i platform most of the
Allwinner ARM based devices include an embedded companion core which can
run in parallel to the ARM cores. There is also an open firmware which
can be used with the OpenRisc based core (https://github.com/crust-firmware/crust).

The driver alone is kind of useless, but you need it in combination with
a firmware running in the companion core. This driver will enable the
synchronization between devices which are shared by the ARM cores and
this embedded core.

Currently the driver is in a testing phase and needs modifications in
u-boot and the free crust firmware to be fully useable. For testing this
driver you need to build a modified crust firmware from my crust fork
(hwspinlock branch https://github.com/wgottwalt/crust). This is for the
OpenRisc companion core only. There in the debug menu you need to enable
the hwspinlock test loop. After that you need to build an u-boot version
with curst/SCP support. A u-boot fork with crust support in the crust
branch is available here https://github.com/crust-firmware/u-boot.

The crust support does not include H2/H3 based devices yet, so if you
want to test this I suggest to use a H5 based device.

There are 4 drivers here. One is the real driver, the other 3 are there
for demonstrating that the hardware spinlock mechanism works. The test
kernel modules are used by loading them, which runs the test. Just
reload them to restart the test.

### sun6i_hwspinlock.c:
This is the driver proposed for mainline. The hwspinlock drivers depend
on some internal data structures which are not easily accessable by
external drivers, therfore you need to add the driver to the kernel to
be able to use it.

##### Kconfig:
```
config HWSPINLOCK_SUN6I
        tristate "SUN6I Hardware Spinlock device"
        depends on ARCH_SUNXI || COMPILE_TEST
        help
          Say y here to support the Allwinner hardware mutex device available
          in the sun6i compatible SoCs.

          If unsure, say N.
```

##### Makefile:
```
obj-$(CONFIG_HWSPINLOCK_SUN6I) += sun6i_hwspinlock.o
```

##### device tree (H3, H5, H6 dtsi, H6 is 0x03004000):
```
hwspinlock: hwspinlock@1c18000 {
	compatible = "allwinner,sun6i-a31-hwspinlock";
	reg = <0x01c18000 0x1000>;
	clocks = <&ccu CLK_BUS_SPINLOCK>;
	clock-names = "ahb";
	resets = <&ccu RST_BUS_SPINLOCK>;
	reset-names = "ahb";
	status = "okay";
};
```

### modified/sun6i_hwspinlock_mod.c:

Same rules apply for this one. This driver splits the 4k memory range into
2 ranges and keeps the SPINLOCK_STATUS register untouched. That register
can now be used by other modules for testing and debugging.

##### Kconfig:
```
config HWSPINLOCK_SUN6I_MOD
        tristate "SUN6I modified Hardware Spinlock device"
        depends on ARCH_SUNXI || COMPILE_TEST
        help
          If unsure, say N.
```

##### Makefile:
```
obj-$(CONFIG_HWSPINLOCK_SUN6I_MOD) += sun6i_hwspinlock_mod.o
```

##### device tree (H3, H5, H6 dtsi, H6 is 0x03004000):
```
hwspinlock-mod@1c18000 {
	compatible = "allwinner,sun6i-a31-hwspinlock-mod";
	reg = <0x01c18000 0x4 0x01c18100 0x400>;
	clocks = <&ccu CLK_BUS_SPINLOCK>;
	clock-names = "ahb";
	resets = <&ccu RST_BUS_SPINLOCK>;
	reset-names = "ahb";
	status = "okay";
};
```

### test/sun6i_hwspinlock_test.c:
This is a very simple test module to demonstrate, that the Linux hwspinlock
ABI works. It can be run with a normal u-boot build and with the modified
crust u-boot build. The crust enabled u-boot shows that the firmware is
able to take the hwspinlocks and that the Linux driver is detecting this.

It can be build on the target device by calling `make` in the directory
or by using a cross-compiler
`ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=... make`.

Never compile this into the kernel, only use it as a module. Though it
can be used with the original and the modified driver.

### test2/sun6i_hwspinlock_test2.c
This is a much more complex test module which needs the modified driver
and makes use of the HWSPINLOCK_STATUS register to bypass the Linux
hwspinlock ABI to show that hwspinlocks can be taken outside the kernel.
This driver is a platform driver and needs an additional device tree
entry.

##### device tree (H3, H5, H6 dtsi, H6 -> 0x03004010):
```
hwspinlock-stat@1c18010 {
	compatible = "allwinner,sun6i-a31-hwspinlock-stat";
	reg = <0x01c18010 0x4>;
	status = "okay";
};
```
