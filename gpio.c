/**
    Network Driver for Beckhoff CCAT communication controller
    Copyright (C) 2014  Beckhoff Automation GmbH
    Author: Patrick Bruenn <p.bruenn@beckhoff.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include "module.h"
#include "gpio.h"

/** TODO implement in LED driver
	#define TC_RED 0x01
	#define TC_GREEN 0x02
	#define TC_BLUE 0x04
	#define FB1_RED 0x08
	#define FB1_GREEN 0x10
	#define FB1_BLUE 0x20
	#define FB2_RED 0x40
	#define FB2_GREEN 0x80
	#define FB2_BLUE 0x100
 */

static int set_bit_in_register(struct mutex *lock, void __iomem * ioaddr,
			       unsigned nr, int val)
{
	volatile unsigned long old;

	mutex_lock(lock);
	old = ioread32(ioaddr);
	val ? set_bit(nr, &old) : clear_bit(nr, &old);
	if (val)
		set_bit(nr, &old);
	else
		clear_bit(nr, &old);
	iowrite32(old, ioaddr);
	mutex_unlock(lock);
	return 0;
}

static int ccat_gpio_get_direction(struct gpio_chip *chip, unsigned nr)
{
	struct ccat_gpio *gdev = container_of(chip, struct ccat_gpio, chip);
	const size_t byte_offset = 4 * (nr / 32) + 0x8;
	const u32 mask = 1 << (nr % 32);

	return !(mask & ioread32(gdev->ioaddr + byte_offset));
}

static int ccat_gpio_direction_input(struct gpio_chip *chip, unsigned nr)
{
	struct ccat_gpio *gdev = container_of(chip, struct ccat_gpio, chip);

	return set_bit_in_register(&gdev->lock, gdev->ioaddr + 0x8, nr, 0);
}

static int ccat_gpio_direction_output(struct gpio_chip *chip, unsigned nr,
				      int val)
{
	struct ccat_gpio *gdev = container_of(chip, struct ccat_gpio, chip);

	return set_bit_in_register(&gdev->lock, gdev->ioaddr + 0x8, nr, 1);
}

static int ccat_gpio_get(struct gpio_chip *chip, unsigned nr)
{
	struct ccat_gpio *gdev = container_of(chip, struct ccat_gpio, chip);
	const size_t byte_off = 4 * (nr / 32);
	const int mask = 1 << (nr % 32);
	int dir_off;
	int value;

	/** omit direction changes before value was read */
	mutex_lock(&gdev->lock);
	dir_off = 0x10 * ccat_gpio_get_direction(chip, nr);
	value = !(mask & ioread32(gdev->ioaddr + byte_off + dir_off));
	mutex_unlock(&gdev->lock);
	return value;
}

static void ccat_gpio_set(struct gpio_chip *chip, unsigned nr, int val)
{
	struct ccat_gpio *gdev = container_of(chip, struct ccat_gpio, chip);

	set_bit_in_register(&gdev->lock, gdev->ioaddr, nr, val);
}

static const struct gpio_chip ccat_gpio_chip = {
	.label = "ccat_gpio",
	.owner = THIS_MODULE,
	.get_direction = ccat_gpio_get_direction,
	.direction_input = ccat_gpio_direction_input,
	.get = ccat_gpio_get,
	.direction_output = ccat_gpio_direction_output,
	.set = ccat_gpio_set,
	.dbg_show = NULL,
	.base = -1,
	.can_sleep = false
};

struct ccat_gpio *ccat_gpio_init(const struct ccat_device *const ccatdev,
				 void __iomem * const addr)
{
	struct ccat_gpio *const gpio = kzalloc(sizeof(*gpio), GFP_KERNEL);

	gpio->ioaddr = ccatdev->bar[0].ioaddr + ioread32(addr + 0x8);
	memcpy(&gpio->chip, &ccat_gpio_chip, sizeof(gpio->chip));
	memcpy_fromio(&gpio->info, addr, sizeof(gpio->info));
	gpio->chip.ngpio = gpio->info.num_gpios;
	mutex_init(&gpio->lock);

	if (gpiochip_add(&gpio->chip)) {
		kfree(gpio);
		return NULL;
	}
	//TODO remove this debug code
	iowrite32(0x1FF, gpio->ioaddr);
	return gpio;
}

void ccat_gpio_remove(struct ccat_gpio *gpio)
{
	gpiochip_remove(&gpio->chip);
}
