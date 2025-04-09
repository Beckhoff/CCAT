// SPDX-License-Identifier: MIT
/**
    Systemtime Driver for Beckhoff CCAT communication controller
    Copyright (C) 2016 - 2018 Beckhoff Automation GmbH & Co. KG
    Author: Steffen Dirkwinkel <s.dirkwinkel@beckhoff.com>
*/

#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/version.h>
#include "module.h"

#define CCAT_SYSTEMTIME_RATING 140

/**
 * struct ccat_systemtime - CCAT Systemtime function
 * @ioaddr: PCI base address of the CCAT Update function
 */
struct ccat_systemtime {
	void __iomem *ioaddr;
	struct clocksource clock;
};

static u64 ccat_systemtime_get(struct clocksource *clk)
{
	struct ccat_systemtime *systemtime =
	    container_of(clk, struct ccat_systemtime, clock);
	return readq(systemtime->ioaddr);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
/**
 * function ccat_systemtime_get_cycles - returns cycles for clocksource
 * return microseconds instead of nanoseconds to make ntp speed
 * adjustment work
 */
static cycle_t ccat_systemtime_get_cycles(struct clocksource *clk)
{
	return (cycle_t) ccat_systemtime_get(clk);
}
#endif

static int ccat_systemtime_probe(struct platform_device *pdev)
{
	struct ccat_function *const func = pdev->dev.platform_data;
	struct ccat_systemtime *const systemtime =
	    devm_kzalloc(&pdev->dev, sizeof(*systemtime), GFP_KERNEL);

	if (!systemtime)
		return -ENOMEM;

	systemtime->ioaddr = func->ccat->bar_0 + func->info.addr;
	func->private_data = systemtime;

	systemtime->clock.name = "ccat";
	systemtime->clock.rating = CCAT_SYSTEMTIME_RATING;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	systemtime->clock.read = ccat_systemtime_get;
#else
	systemtime->clock.read = ccat_systemtime_get_cycles;
#endif
	systemtime->clock.mask = CLOCKSOURCE_MASK(32);
	systemtime->clock.mult = 1;
	systemtime->clock.shift = 0;
	systemtime->clock.owner = THIS_MODULE;
	systemtime->clock.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	return clocksource_register_hz(&systemtime->clock, NSEC_PER_SEC);
}

static REMOVE_RESULT ccat_systemtime_remove(struct platform_device *pdev)
{
	struct ccat_function *const func = pdev->dev.platform_data;
	struct ccat_systemtime *const systemtime = func->private_data;

	clocksource_unregister(&systemtime->clock);
	return REMOVE_OK;
};

static struct platform_driver systemtime_driver = {
	.driver = {.name = "ccat_systemtime"},
	.probe = ccat_systemtime_probe,
	.remove = ccat_systemtime_remove,
};

module_platform_driver(systemtime_driver);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Steffen Dirkwinkel <s.dirkwinkel@beckhoff.com>");
MODULE_LICENSE("GPL and additional rights");
MODULE_VERSION(DRV_VERSION);
