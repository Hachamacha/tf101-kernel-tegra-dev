/*
 * arch/arm/mach-tegra/board-tf101.c
 *
 * Copyright (c) 2010-2011, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/i2c/panjit_ts.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/i2c-tegra.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/usb/f_accessory.h>
#include <linux/memblock.h>
#include <linux/tegra_uart.h>
#include <linux/highmem.h>
#include <linux/console.h>
#include <linux/i2c/fm34_voice_processor.h>
//#include <linux/atmel_mxt1386.h>
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT
#include <linux/i2c/atmel_mxt_ts.h>
#endif
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MT_T9
#include <linux/i2c/atmel_maxtouch.h>
#endif
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MT_T9_EP102
#include <linux/i2c/atmel_maxtouch_ep102.h>
#endif

#include <linux/mfd/tps6586x.h>

#include <sound/wm8903.h>

#include <mach/clk.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/iomap.h>
#include <mach/io.h>
#include <mach/i2s.h>
#include <mach/audio.h>
#include <mach/tegra_wm8903_pdata.h>
#include <mach/usb_phy.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "board.h"
#include "clock.h"
#include "board-tf101.h"
#include "devices.h"
#include "gpio-names.h"
#include "fuse.h"
#include "wakeups-t2.h"
#include "pm.h"

/* NVidia bootloader tags */
#define ATAG_NVIDIA		0x41000801

#define ATAG_NVIDIA_RM			0x1
#define ATAG_NVIDIA_DISPLAY		0x2
#define ATAG_NVIDIA_FRAMEBUFFER		0x3
#define ATAG_NVIDIA_CHIPSHMOO		0x4
#define ATAG_NVIDIA_CHIPSHMOOPHYS	0x5
#define ATAG_NVIDIA_PRESERVED_MEM_0	0x10000
#define ATAG_NVIDIA_PRESERVED_MEM_N	2
#define ATAG_NVIDIA_FORCE_32		0x7fffffff

struct tag_tegra {
	__u32 bootarg_key;
	__u32 bootarg_len;
	char bootarg[1];
};

static int __init parse_tag_nvidia(const struct tag *tag)
{
	struct tag_tegra *ttag = (struct tag_tegra *)&tag->u;

	printk("parse_tag_nvidia: %p key=%u len=%u\n", ttag,
		   ttag->bootarg_key, ttag->bootarg_len);

	return 0;
}
__tagtable(ATAG_NVIDIA, parse_tag_nvidia);

/* Hardware variant, given by the bootloader */
uint8_t tf101_hw = 0;

static int __init hw_setup(char *opt)
{
	if (!opt)
		return 0;

	tf101_hw = simple_strtoul(opt, NULL, 0);
	return 0;
}
__setup("hw=", hw_setup);

static struct tegra_utmi_config utmi_phy_config[] = {
	[0] = {
			.hssync_start_delay = 9,
			.idle_wait_delay = 17,
			.elastic_limit = 16,
			.term_range_adj = 6,
			.xcvr_setup = 15,
			.xcvr_setup_offset = 0,
			.xcvr_use_fuses = 1,
			.xcvr_lsfslew = 2,
			.xcvr_lsrslew = 2,
	},
	[1] = {
			.hssync_start_delay = 9,
			.idle_wait_delay = 17,
			.elastic_limit = 16,
			.term_range_adj = 6,
			.xcvr_setup = 8,
			.xcvr_setup_offset = 0,
			.xcvr_use_fuses = 1,
			.xcvr_lsfslew = 2,
			.xcvr_lsrslew = 2,
	},
};

static struct tegra_ulpi_config ulpi_phy_config = {
	.reset_gpio = TEGRA_GPIO_PG2,
	.clk = "cdev2",
};

static struct resource tf101_bcm4329_rfkill_resources[] = {
	{
		.name   = "bcm4329_nshutdown_gpio",
		.start  = TEGRA_GPIO_PU0,
		.end    = TEGRA_GPIO_PU0,
		.flags  = IORESOURCE_IO,
	},
};

static struct platform_device tf101_bcm4329_rfkill_device = {
	.name = "bcm4329_rfkill",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(tf101_bcm4329_rfkill_resources),
	.resource       = tf101_bcm4329_rfkill_resources,
};

static struct resource tf101_bluesleep_resources[] = {
	[0] = {
		.name = "gpio_host_wake",
			.start  = TEGRA_GPIO_PU6,
			.end    = TEGRA_GPIO_PU6,
			.flags  = IORESOURCE_IO,
	},
	[1] = {
		.name = "gpio_ext_wake",
			.start  = TEGRA_GPIO_PU1,
			.end    = TEGRA_GPIO_PU1,
			.flags  = IORESOURCE_IO,
	},
	[2] = {
		.name = "host_wake",
			.start  = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PU6),
			.end    = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PU6),
			.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

#ifdef CONFIG_BT_BLUESLEEP
static struct platform_device tf101_bluesleep_device = {
	.name           = "bluesleep",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(tf101_bluesleep_resources),
	.resource       = tf101_bluesleep_resources,
};

static void __init tf101_setup_bluesleep(void)
{
	platform_device_register(&tf101_bluesleep_device);
	tegra_gpio_enable(TEGRA_GPIO_PU6);
	tegra_gpio_enable(TEGRA_GPIO_PU1);
	return;
}
#endif

static __initdata struct tegra_clk_init_table tf101_clk_init_table[] = {
	/* name         parent        rate       enabled */
	{ "pll_a",      NULL,          56448000, true },
	{ "pll_a_out0", "pll_a",       11289600, true },

	{ "pll_p_out2", "pll_p",      108000000, true },

	{ "pll_x",      NULL,        1000000000, true },
	{ "pll_u",      NULL,         480000000, true },

	{ "sclk",       "pll_p_out2", 108000000, true },
	{ "hclk",       "sclk",       108000000, true },
	{ "pclk",       "hclk",       108000000, true },

	{ "cclk",       "pll_p",      216000000, true },

	{ "apbdma",     NULL,                 0, true },

	{ "i2s1",       "pll_a_out0",  11289600, false }, /* i2s.0 */
	{ "i2s2",       "pll_a_out0",  11289600, false }, /* i2s.1 */
	{ "audio",      "pll_a_out0",  11289600, false },
	{ "audio_2x",   "audio",       22579200, false },
	{ "spdif_out",  "pll_a_out0",   5644800, false },

//	{ "uarta",      "pll_p",      216000000, false },
//	{ "uartb",      "pll_p",      216000000, false },
	{ "uartc",      "pll_m",      600000000, false },
	{ "uartd",      "pll_p",      216000000, true },
//	{ "uarte",      "pll_p",      216000000, false },

	{ "sdmmc1",     "pll_p",       48000000, false },
	{ "sdmmc2",     "pll_p",       48000000, false },
	{ "sdmmc3",     "pll_p",       48000000, false },
	{ "sdmmc4",     "pll_p",       48000000, false },

	{ "pwm",        "clk_m",       12000000, true },

	{ "kbc",        "clk_32k",        32768, true },
	{ "blink",      "clk_32k",        32768, false },

	{ NULL,         NULL,                 0, 0 },
};

static struct tegra_ulpi_config tf101_ehci2_ulpi_phy_config = {
	.reset_gpio = TEGRA_GPIO_PV1,
	.clk = "cdev2",
};

static struct tegra_ehci_platform_data tf101_ehci2_ulpi_platform_data = {
	.operating_mode = TEGRA_USB_HOST,
	.power_down_on_bus_suspend = 1,
	.phy_config = &tf101_ehci2_ulpi_phy_config,
	.phy_type = TEGRA_USB_PHY_TYPE_LINK_ULPI,
	.default_enable = true,
};

static struct tegra_i2c_platform_data tf101_i2c1_platform_data = {
	.adapter_nr		= 0,
	.bus_count		= 1,
	.bus_clk_rate	= { 400000, 0 },
	.slave_addr		= 0x00FC,
	.scl_gpio		= {TEGRA_GPIO_PC4, 0},
	.sda_gpio		= {TEGRA_GPIO_PC5, 0},
	.arb_recovery	= arb_lost_recovery,
};

static const struct tegra_pingroup_config i2c2_ddc = {
	.pingroup	= TEGRA_PINGROUP_DDC,
	.func		= TEGRA_MUX_I2C2,
};

static const struct tegra_pingroup_config i2c2_gen2 = {
	.pingroup	= TEGRA_PINGROUP_PTA,
	.func		= TEGRA_MUX_I2C2,
};

static struct tegra_i2c_platform_data tf101_i2c2_platform_data = {
	.adapter_nr	= 1,
	.bus_count	= 2,
	.bus_clk_rate	= { 93750, 100000 },
	.bus_mux	= { &i2c2_ddc, &i2c2_gen2 },
	.bus_mux_len	= { 1, 1 },
	.slave_addr = 0x00FC,
	.scl_gpio		= { 0, TEGRA_GPIO_PT5 },
	.sda_gpio		= { 0, TEGRA_GPIO_PT6 },
	.arb_recovery	= arb_lost_recovery,
};

static struct tegra_i2c_platform_data tf101_i2c3_platform_data = {
	.adapter_nr	= 3,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.slave_addr = 0x00FC,
	.scl_gpio		= {TEGRA_GPIO_PBB2, 0},
	.sda_gpio		= {TEGRA_GPIO_PBB3, 0},
	.arb_recovery	= arb_lost_recovery,
};

static struct tegra_i2c_platform_data tf101_dvc_platform_data = {
	.adapter_nr	= 4,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.is_dvc		= true,
	.scl_gpio		= {TEGRA_GPIO_PZ6, 0},
	.sda_gpio		= {TEGRA_GPIO_PZ7, 0},
	.arb_recovery	= arb_lost_recovery,
};

static struct wm8903_platform_data tf101_wm8903_pdata = {
	.irq_active_low = 0,
	.micdet_cfg = 0x83,
	.micdet_delay = 100,
	.gpio_base = TF101_GPIO_WM8903(0),
	.gpio_cfg = {
		WM8903_GPIO_NO_CONFIG,
		WM8903_GPIO_NO_CONFIG,
		0,
		WM8903_GPn_FN_MICBIAS_CURRENT_DETECT << WM8903_GP4_FN_SHIFT,
		WM8903_GPIO_NO_CONFIG,
	},
};

static struct i2c_board_info __initdata wm8903_board_info = {
	I2C_BOARD_INFO("wm8903", 0x1a),
	.platform_data = &tf101_wm8903_pdata,
	//.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_CDC_IRQ),
};

static void tf101_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &tf101_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &tf101_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &tf101_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &tf101_dvc_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);

	i2c_register_board_info(0, &wm8903_board_info, 1);
}
static struct platform_device *tf101_uart_devices[] __initdata = {
	&tegra_uartb_device,
	&tegra_uartc_device,
	&tegra_uartd_device,
};

static struct uart_clk_parent uart_parent_clk[] = {
	[0] = {.name = "pll_p"},
	[1] = {.name = "pll_m"},
	[2] = {.name = "clk_m"},
};

static struct tegra_uart_platform_data tf101_uart_pdata;

static void __init uart_debug_init(void)
{
	unsigned long rate;
	struct clk *c;

	/* UARTD is the debug port. */
	pr_info("Selecting UARTD as the debug console\n");
	tf101_uart_devices[2] = &debug_uartd_device;
	debug_uart_port_base = ((struct plat_serial8250_port *)(
			debug_uartd_device.dev.platform_data))->mapbase;
	debug_uart_clk = clk_get_sys("serial8250.0", "uartd");

	/* Clock enable for the debug channel */
	if (!IS_ERR_OR_NULL(debug_uart_clk)) {
		rate = ((struct plat_serial8250_port *)(
			debug_uartd_device.dev.platform_data))->uartclk;
		pr_info("The debug console clock name is %s\n",
						debug_uart_clk->name);
		c = tegra_get_clock_by_name("pll_p");
		if (IS_ERR_OR_NULL(c))
			pr_err("Not getting the parent clock pll_p\n");
		else
			clk_set_parent(debug_uart_clk, c);

		clk_enable(debug_uart_clk);
		clk_set_rate(debug_uart_clk, rate);
	} else {
		pr_err("Not getting the clock %s for debug console\n",
					debug_uart_clk->name);
	}
}

static void __init tf101_uart_init(void)
{
	int i;
	struct clk *c;

	for (i = 0; i < ARRAY_SIZE(uart_parent_clk); ++i) {
		c = tegra_get_clock_by_name(uart_parent_clk[i].name);
		if (IS_ERR_OR_NULL(c)) {
			pr_err("Not able to get the clock for %s\n",
						uart_parent_clk[i].name);
			continue;
		}
		uart_parent_clk[i].parent_clk = c;
		uart_parent_clk[i].fixed_clk_rate = clk_get_rate(c);
	}
	tf101_uart_pdata.parent_clk_list = uart_parent_clk;
	tf101_uart_pdata.parent_clk_count = ARRAY_SIZE(uart_parent_clk);
	tegra_uartb_device.dev.platform_data = &tf101_uart_pdata;
	tegra_uartc_device.dev.platform_data = &tf101_uart_pdata;
	tegra_uartd_device.dev.platform_data = &tf101_uart_pdata;

	/* Register low speed only if it is selected */
	if (!is_tegra_debug_uartport_hs())
		uart_debug_init();

	platform_add_devices(tf101_uart_devices,
				ARRAY_SIZE(tf101_uart_devices));
}

#ifdef CONFIG_KEYBOARD_GPIO
#define GPIO_KEY(_id, _gpio, _iswake)		\
	{					\
		.code = _id,			\
		.gpio = TEGRA_GPIO_##_gpio,	\
		.active_low = 1,		\
		.desc = #_id,			\
		.type = EV_KEY,			\
		.wakeup = _iswake,		\
		.debounce_interval = 10,	\
	}
#define GPIO_SW(_id, _gpio, _iswake)		\
	{					\
		.code = _id,			\
		.gpio = TEGRA_GPIO_##_gpio,	\
		.active_low = 0,		\
		.desc = #_id,			\
		.type = EV_SW,			\
		.wakeup = _iswake,		\
		.debounce_interval = 10,	\
	}

static struct gpio_keys_button tf101_keys[] = {
	[0] = GPIO_KEY(KEY_VOLUMEUP, PQ5, 0),
	[1] = GPIO_KEY(KEY_VOLUMEDOWN, PQ4, 0),
	[2] = GPIO_KEY(KEY_POWER, PV2, 1),
	[3] = GPIO_SW(SW_LID, PS4, 1),
};

#define PMC_WAKE_STATUS 0x14

static int tf101_wakeup_key(void)
{
	unsigned long status =
		readl(IO_ADDRESS(TEGRA_PMC_BASE) + PMC_WAKE_STATUS);

	return status & TEGRA_WAKE_GPIO_PV2 ? KEY_POWER : KEY_RESERVED;
}

static struct gpio_keys_platform_data tf101_keys_platform_data = {
	.buttons	= tf101_keys,
	.nbuttons	= ARRAY_SIZE(tf101_keys),
	.wakeup_key	= tf101_wakeup_key,
};

static struct platform_device tf101_keys_device = {
	.name	= "gpio-keys",
	.id	= 0,
	.dev	= {
		.platform_data	= &tf101_keys_platform_data,
	},
};

static void tf101_keys_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tf101_keys); i++)
		tegra_gpio_enable(tf101_keys[i].gpio);
}
#endif

static struct tegra_wm8903_platform_data tf101_audio_pdata = {
	.gpio_spkr_en		= TEGRA_GPIO_SPKR_EN,
	.gpio_hp_det		= TEGRA_GPIO_HP_DET,
	.gpio_hp_mute		= -1,
	.gpio_int_mic_en	= TEGRA_GPIO_INT_MIC_EN,
	.gpio_ext_mic_en	= TEGRA_GPIO_EXT_MIC_EN,
};

static struct platform_device tf101_audio_device = {
	.name	= "tegra-snd-wm8903",
	.id	= 0,
	.dev	= {
		.platform_data  = &tf101_audio_pdata,
	},
};

static struct platform_device *tf101_devices[] __initdata = {
	&tegra_pmu_device,
	&tegra_gart_device,
	&tegra_aes_device,
#ifdef CONFIG_KEYBOARD_GPIO
	&tf101_keys_device,
#endif
	&tegra_wdt_device,
	&tegra_avp_device,
	&tegra_i2s_device1,
	&tegra_i2s_device2,
	&tegra_spdif_device,
	&tegra_das_device,
	&spdif_dit_device,
	&bluetooth_dit_device,
	&tf101_bcm4329_rfkill_device,
	&tegra_pcm_device,
	&tf101_audio_device,
};


#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT
static struct mxt_platform_data atmel_mxt_info = {
	.x_line		= 27,
	.y_line		= 42,
	.x_size		= 768,
	.y_size		= 1366,
	.blen		= 0x20,
	.threshold	= 0x3C,
	.voltage	= 3300000,
	.orient		= MXT_ROTATED_90,
	.irqflags	= IRQF_TRIGGER_FALLING,
};

static struct i2c_board_info __initdata i2c_info[] = {
	{
	 I2C_BOARD_INFO("atmel_mxt_ts", 0x5A),
	 .irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	 .platform_data = &atmel_mxt_info,
	 },
};
#endif

static u8 read_chg(void)
{
	return gpio_get_value(TEGRA_GPIO_PV6);
}

static u8 valid_interrupt(void)
{
	return !read_chg();
}


#ifdef CONFIG_TOUCHSCREEN_ATMEL_MT_T9
static struct mxt_platform_data Atmel_mxt_info = {
	/* Maximum number of simultaneous touches to report. */
	.numtouch = 10,
	// TODO: no need for any hw-specific things at init/exit?
	.init_platform_hw = NULL,
	.exit_platform_hw = NULL,
	.max_x = 1279, 
	.max_y = 799,
	.valid_interrupt = &valid_interrupt,
	.read_chg = &read_chg,
};

static struct i2c_board_info __initdata i2c_info[] = {
	{
	 I2C_BOARD_INFO("maXTouch", MXT_I2C_ADDRESS),
	 .irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	 .platform_data = &Atmel_mxt_info,
	 },
};
#endif

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MT_T9_EP102
static struct mxt_platform_data_ep102 Atmel_mxt_info_ep102 = {
	/* Maximum number of simultaneous touches to report. */
	.numtouch = 10,
	// TODO: no need for any hw-specific things at init/exit?
	.init_platform_hw = NULL,
	.exit_platform_hw = NULL,
	.max_x = 1279,
	.max_y = 799,	
	.valid_interrupt = &valid_interrupt,
	.read_chg = &read_chg,
};

static struct i2c_board_info __initdata i2c_info_ep102[] = {
	{
	 I2C_BOARD_INFO("maXTouch_ep102", MXT_I2C_ADDRESS_EP102),
	 .irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	 .platform_data = &Atmel_mxt_info_ep102,
	 },
};
#endif
static int __init ventana_touch_init_atmel(void)
{
	tegra_gpio_enable(TEGRA_GPIO_PV6);
	tegra_gpio_enable(TEGRA_GPIO_PQ7);

	gpio_request(TEGRA_GPIO_PV6, "atmel-irq");
	gpio_direction_input(TEGRA_GPIO_PV6);

	gpio_request(TEGRA_GPIO_PQ7, "atmel-reset");
	gpio_direction_output(TEGRA_GPIO_PQ7, 0);
	msleep(1);
	gpio_set_value(TEGRA_GPIO_PQ7, 1);
	msleep(100);

	i2c_register_board_info(0, i2c_info, 1);
	
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MT_T9_EP102
	printk("Touch : %s\n",__FUNCTION__);
	i2c_register_board_info(0, i2c_info_ep102, 1);
#endif
	  
	return 0;
}

static struct panjit_i2c_ts_platform_data panjit_data = {
	.gpio_reset = TEGRA_GPIO_PQ7,
};

static struct i2c_board_info __initdata ventana_i2c_bus1_touch_info[] = {
	{
		I2C_BOARD_INFO("panjit_touch", 0x3),
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
		.platform_data = &panjit_data,
	},
};

static int __init ventana_touch_init_panjit(void)
{
	tegra_gpio_enable(TEGRA_GPIO_PV6);

	tegra_gpio_enable(TEGRA_GPIO_PQ7);
	i2c_register_board_info(0, ventana_i2c_bus1_touch_info, 1);

	return 0;
}


static struct tegra_ehci_platform_data tegra_ehci_pdata[] = {
	[0] = {
			.phy_config = &utmi_phy_config[0],
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 1,
			.default_enable = true,
	},
	[1] = {
			.phy_config = &ulpi_phy_config,
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 1,
			.phy_type = TEGRA_USB_PHY_TYPE_LINK_ULPI,
			.default_enable = true,
	},
	[2] = {
			.phy_config = &utmi_phy_config[1],
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 1,
			.hotplug = 1,
			.default_enable = true,
	},
};

static struct usb_phy_plat_data tegra_usb_phy_pdata[] = {
	[0] = {
			.instance = 0,
			.vbus_gpio = -1,
	},
	[1] = {
			.instance = 1,
			.vbus_gpio = -1,
	},
	[2] = {
			.instance = 2,
			.vbus_gpio = -1,
	},
};

static struct tegra_usb_otg_data tegra_otg_pdata = {
	.ehci_device = &tegra_ehci1_device,
	.ehci_pdata = &tegra_ehci_pdata[0],
};

static struct fm34_platform_data tf101_fm34_pdata = {
	.gpio_reset	= TEGRA_GPIO_PH2,
	.gpio_en	= TEGRA_GPIO_PH3,
};

static const struct i2c_board_info tf101_dsp_board_info[] = {
	{
		I2C_BOARD_INFO("fm34", 0x60),
		.platform_data = &tf101_fm34_pdata,
	},
};

static void __init tf101_dsp_init(void)
{
	i2c_register_board_info(0, tf101_dsp_board_info,
			ARRAY_SIZE(tf101_dsp_board_info));
}

static int __init tf101_gps_init(void)
{
	struct clk *clk32 = clk_get_sys(NULL, "blink");
	if (!IS_ERR(clk32)) {
		clk_set_rate(clk32,clk32->parent->rate);
		clk_enable(clk32);
	}

	tegra_gpio_enable(TEGRA_GPIO_PZ3);
	return 0;
}

#ifdef CONFIG_USB_SUPPORT
static void tf101_usb_init(void)
{
	//tegra_usb_phy_init(tegra_usb_phy_pdata, ARRAY_SIZE(tegra_usb_phy_pdata));
        tegra_usb_phy_init(ARRAY_SIZE(tegra_usb_phy_pdata));

	/* OTG should be the first to be registered */
	tegra_otg_device.dev.platform_data = &tegra_otg_pdata;
	platform_device_register(&tegra_otg_device);

	platform_device_register(&tegra_udc_device);

	tegra_ehci2_device.dev.platform_data
		= &tf101_ehci2_ulpi_platform_data;
	platform_device_register(&tegra_ehci2_device);

	tegra_ehci3_device.dev.platform_data=&tegra_ehci_pdata[2];
	platform_device_register(&tegra_ehci3_device);
}
#else
static void tf101_usb_init(void) {}
#endif

unsigned int ventana_hw;	
	//The project name depending on different kinds of devices
unsigned char ventana_projectname[30];
unsigned char ventana_pcbid[VENTANA_PCBID_OUTPUT_LENGTH];
	
	unsigned int ASUSGetProjectID()
{
	unsigned int ret = 0;
	ret = HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, PROJECT, ventana_hw);
	switch (ret) {
	case 0: //TF101(EP101)
	case 1: //SL101(EP102)
		ret += 101;
		break;
	default:
		pr_err("[MISC]: Illegal project identification.\n");
	}

	return ret;
}

EXPORT_SYMBOL(ASUSGetProjectID);

unsigned int ASUS3GAvailable()
{
	unsigned int ret = 0;

	if (!HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, PROJECT, ventana_hw)) {
		//Only TF101 has 3G SKU definition
		return HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, SKU, ventana_hw);
	}

	return ret;
}

EXPORT_SYMBOL(ASUS3GAvailable);

unsigned int ASUSWiMAXAvailable()
{
	unsigned int ret = 0;

	if (ASUS3GAvailable()) return ret;	/* 3G module is equipped */

	if (!HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, PROJECT, ventana_hw)) {
		//Only TF101 has WiMAX SKU definition
		return HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, WIMAX, ventana_hw);
	}

	return ret;
}

EXPORT_SYMBOL(ASUSWiMAXAvailable);

unsigned int ASUSCheckWLANVendor(unsigned int vendor)
{
	unsigned int ret = 0;

	if (HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, PROJECT, ventana_hw) < 2) {
		//All valid projects (TF101/SL101) have BT/WLAN module
		//definition
		switch (vendor) {
		case BT_WLAN_VENDOR_MURATA:
			ret = HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, VENDOR,
				ventana_hw) ? 0 : 1;
			break;
		case BT_WLAN_VENDOR_AZW:
			ret = HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, VENDOR,
				ventana_hw) ? 1 : 0;
			break;
		default:
			pr_err("[MISC]: Check WLAN with undefined vendor.\n");
		}
        }

	return ret;
}
EXPORT_SYMBOL(ASUSCheckWLANVendor);

unsigned int ASUSCheckTouchVendor(unsigned int vendor)
{
	unsigned int ret = 0;

	if (HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, PROJECT, ventana_hw) < 2) {
		//All valid projects (TF101/SL101) have touch panel module
		//definition
		switch (vendor) {
		case TOUCH_VENDOR_SINTEK:
			ret = HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, TOUCH,
				ventana_hw) ? 0 : 1;
			break;
		case TOUCH_VENDOR_WINTEK:
			ret = HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, TOUCH,
				ventana_hw) ? 1 : 0;
			break;
		default:
			pr_err("[MISC]: Check TOUCH with undefined vendor.\n");
		}
        }

	return ret;
}
EXPORT_SYMBOL(ASUSCheckTouchVendor);

static void ASUSGetProjectName()
{
	unsigned int ret = 0;
	ret = HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, PROJECT, ventana_hw);

	if(ret == 0) { //TF101
		if(ASUS3GAvailable()) {
			strcpy(ventana_projectname,"TF101G");
		}
		else {
			if(ASUSWiMAXAvailable())
				strcpy(ventana_projectname,"TF101-WiMAX");
			else
				strcpy(ventana_projectname,"TF101");
		}
	}
	else if(ret == 1) { //SL101
		strcpy(ventana_projectname,"SL101");
	}
	else {
		pr_err("[MISC]: Illegal project identification.\n");
		strcpy(ventana_projectname,"unknown project name");
	}
}

static void ASUSGetPcbid()
{
	unsigned int pcbid;

	unsigned int ret = 0;
	ret = HW_DRF_VAL(TEGRA_DEVKIT, MISC_HW, PROJECT, ventana_hw);
	if(ret == 0 || ret ==1) { //TF101 or SL101
	       pcbid = ventana_hw >> 4;
	       int i = VENTANA_PCBID_OUTPUT_LENGTH - 2;

	       while (i+1) {
	               ventana_pcbid[i--] = (1 & pcbid) ? '1' : '0';
	               pcbid >>= 1;
	       }
	       ventana_pcbid[VENTANA_PCBID_OUTPUT_LENGTH-1] = 'b';
	}
	else
	       strcpy(ventana_pcbid,"unknown");
}

	
	
	
	
	
	
	
	
static void __init tegra_tf101_init(void)
{
	console_suspend_enabled = 0;

	tegra_clk_init_from_table(tf101_clk_init_table);
	tf101_pinmux_init();
	tf101_i2c_init();
	tf101_uart_init();

	platform_add_devices(tf101_devices, ARRAY_SIZE(tf101_devices));

	tf101_sdhci_init();
	//tf101_charge_init();
	tf101_regulator_init();
	tf101_charger_init();
//	tf101_touch_init_atmel();
#if defined(CONFIG_TOUCHSCREEN_ATMEL_MT_T9) || defined(CONFIG_TOUCHSCREEN_ATMEL_MT_T9_EP102)
       pr_info("Initializing Atmel touch driver\n");
       ventana_touch_init_atmel();
#elif defined(CONFIG_TOUCHSCREEN_PANJIT_I2C)
       pr_info("Initializing Panjit touch driver\n");
       ventana_touch_init_panjit();
#endif
	

#ifdef CONFIG_KEYBOARD_GPIO
	tf101_keys_init();
#endif
	tf101_dsp_init();

	tf101_usb_init();
	tf101_gps_init();
	tf101_panel_init();
	tf101_sensors_init();
	tf101_emc_init();

#ifdef CONFIG_BT_BLUESLEEP
	tf101_setup_bluesleep();
#endif
}

int __init tegra_tf101_protected_aperture_init(void)
{
	tegra_protected_aperture_init(tegra_grhost_aperture);
	return 0;
}
late_initcall(tegra_tf101_protected_aperture_init);

void __init tegra_tf101_reserve(void)
{
	if (memblock_reserve(0x0, 4096) < 0)
		pr_warn("Cannot reserve first 4K of memory for safety\n");

	tegra_reserve(SZ_256M, SZ_8M + SZ_1M, SZ_16M);
}

MACHINE_START(VENTANA, "ventana")
	.boot_params    = 0x00000100,
	.map_io         = tegra_map_common_io,
	.reserve        = tegra_tf101_reserve,
	.init_early	= tegra_init_early,
	.init_irq	= tegra_init_irq,
	.timer          = &tegra_timer,
	.init_machine	= tegra_tf101_init,
MACHINE_END
