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
 
// vi: set noexpandtab:

#ifndef _CCAT_SRAM_H_
#define _CCAT_SRAM_H_

#include <linux/fs.h>

extern ssize_t ccat_sram_read(struct file *const f, char __user * buf,
			      size_t len, loff_t * off);

extern ssize_t ccat_sram_write(struct file *const f, const char __user * buf,
			       size_t len, loff_t * off);

#endif /* #ifndef _CCAT_SRAM_H_ */

