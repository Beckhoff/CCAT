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

#ifndef _CCAT_COMPAT_H_
#define _CCAT_COMPAT_H_
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32)
#define pr_warn pr_info

#define netdev_info(DEV, ...) pr_info(__VA_ARGS__)
#define netdev_err netdev_info

static inline void *dma_zalloc_coherent(struct device *dev, size_t size,
					dma_addr_t * dma_handle, gfp_t flag)
{
	void *result =
	    dma_alloc_coherent(dev, size, dma_handle, flag | __GFP_ZERO);
	if (result)
		memset(result, 0, size);
	return result;
}

static inline void usleep_range(unsigned long min, unsigned long max)
{
	msleep(min / 1000);
}
#endif
#endif /* #ifndef _CCAT_COMPAT_H_ */
