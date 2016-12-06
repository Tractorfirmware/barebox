/*
 * Copyright (C) 2012 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */

#include <common.h>
#include <net.h>
#include <init.h>
#include <environment.h>
#include <asm/armlinux.h>
#include <generated/mach-types.h>
#include <partition.h>
#include <fs.h>
#include <fcntl.h>
#include <io.h>
#include <envfs.h>
#include <mach/hardware.h>
#include <nand.h>
#include <linux/sizes.h>
#include <linux/mtd/nand.h>
#include <mach/board.h>
#include <mach/at91sam9_smc.h>
#include <gpio.h>
#include <mach/io.h>
#include <mach/iomux.h>
#include <mach/at91_pmc.h>
#include <mach/at91_rstc.h>
#include <mach/at91sam9x5_matrix.h>
#include <input/qt1070.h>
#include <readkey.h>
#include <linux/w1-gpio.h>
#include <w1_mac_address.h>
#include <spi/spi.h>

#include "hw_version.h"

struct w1_gpio_platform_data w1_pdata = {
	.pin = AT91_PIN_PB18,
	.ext_pullup_enable_pin = -EINVAL,
	.is_open_drain = 0,
};

static struct atmel_nand_data nand_pdata = {
	.ale		= 21,
	.cle		= 22,
	.det_pin	= -EINVAL,
	.rdy_pin	= AT91_PIN_PD5,
	.enable_pin	= AT91_PIN_PD4,
	.has_pmecc	= 1,
	.ecc_mode	= NAND_ECC_HW,
	.pmecc_sector_size = 512,
	.pmecc_corr_cap = 2,
#if defined(CONFIG_MTD_NAND_ATMEL_BUSWIDTH_16)
	.bus_width_16	= 1,
#endif
	.on_flash_bbt	= 1,
};

static struct sam9_smc_config cm_nand_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 1,
	.ncs_write_setup	= 0,
	.nwe_setup		= 1,

	.ncs_read_pulse		= 6,
	.nrd_pulse		= 4,
	.ncs_write_pulse	= 5,
	.nwe_pulse		= 3,

	.read_cycle		= 6,
	.write_cycle		= 5,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE,
	.tdf_cycles		= 1,
};

static void ek_add_device_nand(void)
{
	add_generic_device("at91sam9-smc",
			   DEVICE_ID_SINGLE, NULL,
			   AT91SAM9X5_BASE_SMC, 0x200,
			   IORESOURCE_MEM, NULL);

	/* setup bus-width (8 or 16) */
	if (nand_pdata.bus_width_16)
		cm_nand_smc_config.mode |= AT91_SMC_DBW_16;
	else
		cm_nand_smc_config.mode |= AT91_SMC_DBW_8;

	/* configure chip-select 3 (NAND) */
	sam9_smc_configure(0, 3, &cm_nand_smc_config);

	if (at91sam9x5ek_cm_is_vendor(VENDOR_COGENT)) {
		unsigned long csa;

		csa = at91_sys_read(AT91_MATRIX_EBICSA);
		csa |= AT91_MATRIX_EBI_VDDIOMSEL_1_8V;
		at91_sys_write(AT91_MATRIX_EBICSA, csa);
	}

	at91_add_device_nand(&nand_pdata);
}

static struct macb_platform_data macb_pdata = {
	.phy_interface = PHY_INTERFACE_MODE_RMII,
	.phy_addr = 0,
};

static void ek_add_device_eth(void)
{
	if (w1_local_mac_address_register(0, "tml", "w1-2d-0"))
		w1_local_mac_address_register(0, "tml", "w1-23-0");

	at91_add_device_eth(0, &macb_pdata);
}

#if defined(CONFIG_DRIVER_VIDEO_ATMEL_HLCD)
/*
 * LCD Controller
 */
static struct fb_videomode at91_tft_vga_modes[] = {
	{
		.name		= "LG",
		.refresh	= 60,
		.xres		= 800,		.yres		= 480,
		.pixclock	= KHZ2PICOS(33260),

		.left_margin	= 88,		.right_margin	= 168,
		.upper_margin	= 8,		.lower_margin	= 37,
		.hsync_len	= 128,		.vsync_len	= 2,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

/* Default output mode is TFT 24 bit */
#define AT91SAM9X5_DEFAULT_LCDCFG5	(LCDC_LCDCFG5_MODE_OUTPUT_24BPP)

/* Driver datas */
static struct atmel_lcdfb_platform_data ek_lcdc_data = {
	.lcdcon_is_backlight		= true,
	.default_bpp			= 16,
	.default_lcdcon2		= AT91SAM9X5_DEFAULT_LCDCFG5,
	.guard_time			= 9,
	.lcd_wiring_mode		= ATMEL_LCDC_WIRING_RGB,
	.mode_list			= at91_tft_vga_modes,
	.num_modes			= ARRAY_SIZE(at91_tft_vga_modes),
};

static void ek_add_device_lcdc(void)
{
	at91_add_device_lcdc(&ek_lcdc_data);
}

#else
static void ek_add_device_lcdc(void) {}
#endif

#if defined(CONFIG_USB_OHCI) || defined(CONFIG_USB_EHCI)
/*
 * USB HS Host port (common to OHCI & EHCI)
 */
static struct at91_usbh_data ek_usbh_hs_data = {
	.ports			= 2,
	.vbus_pin		= {AT91_PIN_PD19, AT91_PIN_PD20},
};

static void ek_add_device_usb(void)
{
	at91_add_device_usbh_ohci(&ek_usbh_hs_data);
	at91_add_device_usbh_ehci(&ek_usbh_hs_data);
}
#else
static void ek_add_device_usb(void) {}
#endif

static int at91sam9x5ek_devices_init(void)
{
	ek_add_device_nand();
	ek_add_device_eth();
	ek_add_device_usb();
	ek_add_device_lcdc();

	armlinux_set_architecture(CONFIG_MACH_AT91SAM9X5EK);

	devfs_add_partition("nand0", 0x00000, SZ_256K, DEVFS_PARTITION_FIXED, "at91bootstrap_raw");
	dev_add_bb_dev("at91bootstrap_raw", "at91bootstrap");
	devfs_add_partition("nand0", SZ_256K, SZ_256K + SZ_128K, DEVFS_PARTITION_FIXED, "self_raw");
	dev_add_bb_dev("self_raw", "self0");
	devfs_add_partition("nand0", SZ_512K + SZ_128K, SZ_128K, DEVFS_PARTITION_FIXED, "env_raw");
	dev_add_bb_dev("env_raw", "env0");
	devfs_add_partition("nand0", SZ_512K + SZ_256K, SZ_128K, DEVFS_PARTITION_FIXED, "env_raw1");
	dev_add_bb_dev("env_raw1", "env1");

	if (IS_ENABLED(CONFIG_DEFAULT_ENVIRONMENT_GENERIC))
		defaultenv_append_directory(defaultenv_at91sam9x5ek);

	return 0;
}
device_initcall(at91sam9x5ek_devices_init);

static int at91sam9x5ek_console_init(void)
{
	barebox_set_model("Atmel at91sam9x5-ek");
	barebox_set_hostname("at91sam9x5-ek");

	return 0;
}
console_initcall(at91sam9x5ek_console_init);
