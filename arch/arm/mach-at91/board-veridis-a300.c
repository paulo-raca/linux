/*
 * linux/arch/arm/mach-at91/board-sam9260ek.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2006 Atmel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/at73c213.h>
#include <linux/clk.h>
#include <linux/i2c/at24.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/gpio_keys.h>
#include <linux/charlcd-gpio.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/at91sam9_smc.h>
#include <mach/at91_shdwc.h>
#include <mach/system_rev.h>

#include "sam9_smc.h"
#include "generic.h"

/*
 * Serial ports
 */

static void __init a300_init_early(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91_initialize(18432000);

	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 on ttyS1. (Rx & Tx only) */
	at91_register_uart(AT91SAM9260_ID_US0, 1, 0);

	/* USART1 on ttyS2. (Rx & Tx only) */
	at91_register_uart(AT91SAM9260_ID_US1, 2, 0);

#ifdef CONFIG_MACH_VERIDIS_A300B //Revision "B" adds 2 extra UARTS
	/* USART2 on ttyS3. (Rx & Tx only) */
	#ifndef CONFIG_MACH_VERIDIS_A300_WIEGANDO_ON_USART2
		at91_register_uart(AT91SAM9260_ID_US2, 3, 0);
	#endif

	//USART 3 is reserved for RS-485
	//USART 4 pins are not available =(
	
	/* USART5 on ttyS4. (Rx & Tx only) */
	#ifndef CONFIG_MACH_VERIDIS_A300_WIEGANDO_ON_USART5
		at91_register_uart(AT91SAM9260_ID_US5, 4, 0);
	#endif	
#endif
	
	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}


/*
 * USB Host port
 */
static struct at91_usbh_data __initdata a300_usbh_data = {
	.ports		= 2,
};


/*
 * MACB Ethernet device
 */
static struct at91_eth_data __initdata a300_macb_data = {
	.phy_irq_pin	= AT91_PIN_PA7,
	.is_rmii	= 1,
};


/*
 * NAND flash
 */
static struct mtd_partition __initdata a300_nand_partition[] = {
	{
		.name	= "uboot",
		.offset	= 0,
		.size	= SZ_1M,
	},
	{
		.name	= "kernel",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= 3*SZ_1M,
	},
	{
		.name	= "rootfs",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct mtd_partition * __init nand_partitions(int size, int *num_partitions)
{
	*num_partitions = ARRAY_SIZE(a300_nand_partition);
	return a300_nand_partition;
}

static struct atmel_nand_data __initdata a300_nand_data = {
	.ale		= 21,
	.cle		= 22,
//	.det_pin	= ... not connected
	.rdy_pin	= AT91_PIN_PC13,
	.enable_pin	= AT91_PIN_PC14,
	.partition_info	= nand_partitions,
};

static struct sam9_smc_config __initdata a300_nand_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 1,
	.ncs_write_setup	= 0,
	.nwe_setup		= 1,

	.ncs_read_pulse		= 3,
	.nrd_pulse		= 3,
	.ncs_write_pulse	= 3,
	.nwe_pulse		= 3,

	.read_cycle		= 5,
	.write_cycle		= 5,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE,
	.tdf_cycles		= 2,
};

static void __init a300_add_device_nand(void)
{
	a300_nand_data.bus_width_16 = board_have_nand_16bit();
	/* setup bus-width (8 or 16) */
	if (a300_nand_data.bus_width_16)
		a300_nand_smc_config.mode |= AT91_SMC_DBW_16;
	else
		a300_nand_smc_config.mode |= AT91_SMC_DBW_8;

	/* configure chip-select 3 (NAND) */
	sam9_smc_configure(3, &a300_nand_smc_config);

	at91_add_device_nand(&a300_nand_data);
}


/*
 * MCI (SD/MMC)
 */
static struct at91_mmc_data __initdata a300_mmc_data = {
	.slot_b		= 1,
	.wire4		= 1,
//	.det_pin	= ... not connected
//	.wp_pin		= ... not connected
//	.vcc_pin	= ... not connected
};


/*
 * LEDs
 */
#ifdef CONFIG_MACH_VERIDIS_A300A
static struct gpio_led a300_leds[] = {
	/* ======== LEDs ======== */
	{
		.name     = "led:green:A",
		.gpio     = AT91_PIN_PA9,
		.default_trigger  = "pulse"
	},
	{
		.name     = "led:green:B",
		.gpio     = AT91_PIN_PA6,
		.active_low		= 1,
		.default_trigger  = "pulse"
	},
	{
		.name     = "led:green:alive",
		.gpio     = AT91_PIN_PC11,
		.default_trigger  = "heartbeat"
	},
	
	/* ======== Relays ======== */
	{
		.name     = "relay:none:A",
		.gpio     = AT91_PIN_PB3,
		.default_trigger  = "pulse"
	},{
		.name     = "relay:none:B",
		.gpio     = AT91_PIN_PB8,
		.default_trigger  = "pulse"
	}, {
		.name     = "relay:none:C",
		.gpio     = AT91_PIN_PB9,
		.default_trigger  = "pulse"
	},

	/* ======== MISC Digital outputs ======== */
	{ 
		.name     = "lcd:green:backlight",
		.gpio     = AT91_PIN_PB10,
		.default_trigger  = "pulse"
	},{
		.name     = "buzzer:none:buzzer",
		.gpio     = AT91_PIN_PB11,
		.default_trigger  = "pulse"
	}
};
#else //MACH_VERIDIS_A300B
static struct gpio_led a300_leds[] = {
	/* ======== LEDs ======== */
	{
		.name     = "led:red:A",
		.gpio     = AT91_PIN_PA9,
		.default_trigger  = "cpu"
	},
	{
		.name     = "led:green:B",
		.gpio     = AT91_PIN_PA6,
		.active_low		= 1,
		.default_trigger  = "pulse"
	},
	{
		.name     = "led:blue:alive",
		.gpio     = AT91_PIN_PC11,
		.default_trigger  = "heartbeat"
	},

	/* ======== Pictogram ======== */
	{ 
		.name     = "picto:green:left",
		.gpio     = AT91_PIN_PC2,
		.default_trigger  = "none",
		.active_low = 1
	},
	{
		.name     = "picto:red:stop",
		.gpio     = AT91_PIN_PC0,
		.default_trigger  = "none",
		.active_low = 1
	},
	{
		.name     = "picto:green:right",
		.gpio     = AT91_PIN_PC1,
		.default_trigger  = "none",
		.active_low = 1
	},
	
	/* ======== Relays ======== */
	{
		.name     = "relay:none:A",
		.gpio     = AT91_PIN_PB3,
		.default_trigger  = "pulse"
	},{
		.name     = "relay:none:B",
		.gpio     = AT91_PIN_PB16,
		.default_trigger  = "pulse"
	}, {
		.name     = "relay:none:C",
		.gpio     = AT91_PIN_PB17,
		.default_trigger  = "pulse"
	},

	/* ======== MISC Digital outputs ======== */
	{ 
		.name     = "charlcd0:green:backlight",
		.gpio     = AT91_PIN_PB10,
		.default_trigger  = "pulse"
	},{
		.name     = "buzzer:none:buzzer",
		.gpio     = AT91_PIN_PB11,
		.default_trigger  = "pulse"
	}
};
#endif


/*
 * GPIO Buttons
 */
static struct gpio_keys_button a300_buttons[] = {
	{
		.gpio              = AT91_PIN_PB18,
		.code              = BTN_1,
		.desc              = "Digital Input 1",
		.active_low        = 1,
		.debounce_interval = 10, /* ms */
	},
	{
		.gpio              = AT91_PIN_PB19,
		.code              = BTN_2,
		.desc              = "Digital Input 2",
		.active_low        = 1,
		.debounce_interval = 10, /* ms */
	},
	{
		.gpio              = AT91_PIN_PB20,
		.code              = BTN_3,
		.desc              = "Digital Input 3",
		.active_low        = 1,
		.debounce_interval = 10, /* ms */
	},
	{
		.gpio              = AT91_PIN_PB21,
		.code              = BTN_4,
		.desc              = "Digital Input 4",
		.active_low        = 1,
		.debounce_interval = 10, /* ms */
	}
};

static struct gpio_keys_platform_data a300_button_data = {
	.buttons	= a300_buttons,
	.nbuttons	= ARRAY_SIZE(a300_buttons),
};

static struct platform_device a300_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &a300_button_data,
	}
};

static void __init a300_add_device_buttons(void)
{
	int i;
	for (i=0; i<ARRAY_SIZE(a300_buttons); i++) {
		at91_set_gpio_input(a300_buttons[i].gpio, 1);	/* btn3 */
		at91_set_gpio_input(a300_buttons[i].gpio, 1);	/* btn4 */
	}
	platform_device_register(&a300_button_device);
}

/*
 * GPIO Keypad
 */
static const uint32_t a300keypad_keymap[] = {
	KEY(0, 0, KEY_1),
	KEY(0, 1, KEY_2),
	KEY(0, 2, KEY_3),
	KEY(1, 0, KEY_4),
	KEY(1, 1, KEY_5),
	KEY(1, 2, KEY_6),
	KEY(2, 0, KEY_7),
	KEY(2, 1, KEY_8),
	KEY(2, 2, KEY_9),
	KEY(3, 0, KEY_ESC),
	KEY(3, 1, KEY_0),
	KEY(3, 2, KEY_ENTER)
};

static struct matrix_keymap_data a300keypad_keymap_data = {
	.keymap   = a300keypad_keymap,
	.keymap_size  = ARRAY_SIZE(a300keypad_keymap),
};

static const int a300keypad_row_gpios[] =
		{ AT91_PIN_PB28, AT91_PIN_PB29, AT91_PIN_PB30, AT91_PIN_PB31};
static const int a300keypad_col_gpios[] =
		{ AT91_PIN_PC3, AT91_PIN_PC9, AT91_PIN_PC10 };

static struct matrix_keypad_platform_data a300keypad_pdata = {
	.keymap_data    = &a300keypad_keymap_data,
	.row_gpios    = a300keypad_row_gpios,
	.col_gpios    = a300keypad_col_gpios,
	.num_row_gpios    = ARRAY_SIZE(a300keypad_row_gpios),
	.num_col_gpios    = ARRAY_SIZE(a300keypad_col_gpios),
	.col_scan_delay_us  = 50,
	.debounce_ms    = 50,
	.wakeup     = 1,
	.active_low = 1,
	.no_autorepeat = 1
};

static struct platform_device a300keypad_device = {
	.name   = "matrix-keypad",
	.id   = -1,
	.dev    = {
		.platform_data = &a300keypad_pdata,
	},
};

static void __init a300_add_device_keypad(void) {
	int i;
	for (i=0; i<a300keypad_pdata.num_row_gpios; i++) {
		at91_set_gpio_input(a300keypad_pdata.row_gpios[i], 1);
	}
	for (i=0; i<a300keypad_pdata.num_col_gpios; i++) {
		at91_set_gpio_output(a300keypad_pdata.col_gpios[i], 1);
	}
	platform_device_register(&a300keypad_device);
}


/*
 * GPIO Keypad
 */

struct charlcd_gpio_pin a300lcd_pin_DB_4 = { .gpio=AT91_PIN_PC4, .label="DB4"  };
struct charlcd_gpio_pin a300lcd_pin_DB_5 = { .gpio=AT91_PIN_PC5, .label="DB5"  };
struct charlcd_gpio_pin a300lcd_pin_DB_6 = { .gpio=AT91_PIN_PC6, .label="DB6"  };
struct charlcd_gpio_pin a300lcd_pin_DB_7 = { .gpio=AT91_PIN_PC7, .label="DB7"  };

struct charlcd_gpio_pin a300lcd_pin_RS     = { .gpio=AT91_PIN_PB0, .label="RS" };
struct charlcd_gpio_pin a300lcd_pin_RW     = { .gpio=AT91_PIN_PB1, .label="RW" };
struct charlcd_gpio_pin a300lcd_pin_EN     = { .gpio=AT91_PIN_PB2, .label="^EN",  .active_low=1  };


struct charlcd_gpio a300lcd = {
	.EN = &a300lcd_pin_EN,
	.RS = &a300lcd_pin_RS,
	.RW = &a300lcd_pin_RW,
	.DATA = {
		NULL,
		NULL,
		NULL,
		NULL,
		&a300lcd_pin_DB_4,
		&a300lcd_pin_DB_5,
		&a300lcd_pin_DB_6,
		&a300lcd_pin_DB_7
	}
};

static struct platform_device a300charlcd_device = {
	.name   = "charlcd-gpio",
	.id   = -1,
	.dev    = {
		.platform_data = &a300lcd
	},
};

static void a300_add_device_charlcd(void) {
	at91_set_gpio_output(a300lcd_pin_RS  .gpio, 0);
	at91_set_gpio_output(a300lcd_pin_RW  .gpio, 0);
	at91_set_gpio_output(a300lcd_pin_EN  .gpio, 1);

	at91_set_gpio_input(a300lcd_pin_DB_4 .gpio, 0);
	at91_set_gpio_input(a300lcd_pin_DB_5 .gpio, 0);
	at91_set_gpio_input(a300lcd_pin_DB_6 .gpio, 0);
	at91_set_gpio_input(a300lcd_pin_DB_7 .gpio, 0);

	platform_device_register(&a300charlcd_device);
}



static void __init a300_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* USB Host */
	at91_add_device_usbh(&a300_usbh_data);
	/* NAND */
	a300_add_device_nand();
	/* Ethernet */
	at91_add_device_eth(&a300_macb_data);
	/* MMC */
	at91_add_device_mmc(0, &a300_mmc_data);
	/* LEDs */
	at91_gpio_leds(a300_leds, ARRAY_SIZE(a300_leds));
	/* Push Buttons */
	a300_add_device_buttons();
	/* Matrix keypad */
	a300_add_device_keypad();
	/* Character LCD */
	a300_add_device_charlcd();
}


MACHINE_START(VERIDIS_A300, "Veridis A300")
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_early	= a300_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= a300_board_init,
MACHINE_END
