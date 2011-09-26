/*
 * ledtrig-cpu.c - LED trigger based on CPU activity
 *
 * Copyright 2011 Linus Walleij <linus.walleij@linaro.org>
 * Copyright 2011 Bryan Wu <bryan.wu@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/syscore_ops.h>
#include "leds.h"

static DEFINE_PER_CPU(struct led_trigger *, cpu_trig);
static DEFINE_PER_CPU(char [8], trig_name);

void ledtrig_cpu(enum cpu_led_event ledevt)
{
	struct led_trigger *trig = __get_cpu_var(cpu_trig);

	if (!trig)
		return;

	/* Locate the correct CPU LED */

	switch (ledevt) {
	case CPU_LED_IDLE_END:
	case CPU_LED_START:
		/* Will turn the LED on, max brightness */
		led_trigger_event(trig, LED_FULL);
		break;

	case CPU_LED_IDLE_START:
	case CPU_LED_STOP:
	case CPU_LED_HALTED:
		/* Will turn the LED off */
		led_trigger_event(trig, LED_OFF);
		break;

	default:
		/* Will leave the LED as it is */
		break;
	}
}
EXPORT_SYMBOL(ledtrig_cpu);

static int ledtrig_cpu_syscore_suspend(void)
{
	ledtrig_cpu(CPU_LED_STOP);
	return 0;
}

static void ledtrig_cpu_syscore_resume(void)
{
	ledtrig_cpu(CPU_LED_START);
}

static void ledtrig_cpu_syscore_shutdown(void)
{
	ledtrig_cpu(CPU_LED_HALTED);
}

static struct syscore_ops ledtrig_cpu_syscore_ops = {
	.shutdown	= ledtrig_cpu_syscore_shutdown,
	.suspend	= ledtrig_cpu_syscore_suspend,
	.resume		= ledtrig_cpu_syscore_resume,
};

static void __init ledtrig_cpu_register(void *info)
{
	int cpuid = smp_processor_id();
	struct led_trigger *trig;
	char *name = __get_cpu_var(trig_name);

	snprintf(name, 8, "cpu%d", cpuid);
	led_trigger_register_simple(name, &trig);

	pr_info("LED trigger %s indicate activity on CPU %d\n",
		trig->name, cpuid);

	__get_cpu_var(cpu_trig) = trig;
}

static void __exit ledtrig_cpu_unregister(void *info)
{
	struct led_trigger *trig = __get_cpu_var(cpu_trig);
	char *name = __get_cpu_var(trig_name);

	led_trigger_unregister_simple(trig);
	__get_cpu_var(cpu_trig) = NULL;
	memset(name, 0, 8);
}

static int __init ledtrig_cpu_init(void)
{
	on_each_cpu(ledtrig_cpu_register, NULL, 1);
	register_syscore_ops(&ledtrig_cpu_syscore_ops);

	return 0;
}
module_init(ledtrig_cpu_init);

static void __exit ledtrig_cpu_exit(void)
{
	on_each_cpu(ledtrig_cpu_unregister, NULL, 1);
	unregister_syscore_ops(&ledtrig_cpu_syscore_ops);
}
module_exit(ledtrig_cpu_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_AUTHOR("Bryan Wu <bryan.wu@canonical.com>");
MODULE_DESCRIPTION("CPU LED trigger");
MODULE_LICENSE("GPL");
