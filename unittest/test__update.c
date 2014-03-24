/**
    Simple program to test the CCAT Update function of the
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

#include <stdio.h>
#include <stdlib.h>

void usage(void)
{
	printf("usage: test__update.bin <rbf>\n");
	printf("   rbf: path to a *.rbf used for comparsion\n");
}

struct buffer {
	char *data;
	size_t size;
};

void buffer_fill(struct buffer *const buf, size_t length, FILE *const f)
{
	buf->size = length;
	buf->data = malloc(buf->size);
	if(buf->data) {
		char *pos = buf->data;
		size_t bytes_left = buf->size;
		fseek(f, 0, SEEK_SET);
		while(!feof(f) && bytes_left > 0) {
			bytes_left -= fread(pos, 1, bytes_left, f);
		}
	}
	fclose(f);
}

void buffer_new(struct buffer* buf, const char *const filename)
{
	FILE *const f = fopen(filename, "rb");
	if(f) {
		fseek(f, 0, SEEK_END);
		buffer_fill(buf, ftell(f), f);
	}
}

int main(int argc, const char* argv[])
{
	if(argc < 2) {
		usage();
		return -1;
	}
	struct buffer rbf, ccat;
	buffer_new(&rbf, argv[1]);
	FILE *const f = fopen("/dev/ccat_update", "r");
	if(rbf.data && f) {
		buffer_fill(&ccat, rbf.size, f);
		int result = memcmp(rbf.data, ccat.data, rbf.size);
		free(ccat.data);
		free(rbf.data);
		return result;
	}
	printf("open ccat file failed\n");
	return -1;
}
