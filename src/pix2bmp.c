/*
	PIX file depacker

	Copyright (C) 2009	Patrice Mandin

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <SDL.h>

#include "file_functions.h"

void save_bmp(const char *src_filename, SDL_Surface *image)
{
	int dst_namelength = strlen(src_filename)+1;
	char *dst_filename;
	char *posname, *posext;

	dst_filename = (char *) malloc(dst_namelength);
	if (!dst_filename) {
		fprintf(stderr, "Can not allocate %d bytes\n", dst_namelength);
		return;
	}

	posname = strrchr(src_filename, '/');
	if (posname) {
		++posname;	/* Go after / */
	} else {
		posname = strrchr(src_filename, '\\');
		if (posname) {
			++posname;	/* Go after \\ */
		} else {
			/* No directory in source filename */
			posname = (char *) src_filename;
		}
	}
	sprintf(dst_filename, "%s", posname);

	posext = strrchr(dst_filename, '.');
	if (!posext) {
		strcat(dst_filename, ".bmp");
	} else {
		++posext;
		strcpy(posext, "bmp");
	}

	printf("Saving to %s\n", dst_filename);
	SDL_SaveBMP(image, dst_filename);

	free(dst_filename);
}

void convert_endianness(Uint16 *src, int length)
{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	int i;

	for (i=0; i<length>>1; i++) {
		Uint16 v = *src;
		*src++ = SDL_SwapLE16(v);
	}
#endif
}

void convert_alpha(Uint16 *src, int length)
{
	int i, r,g,b,a;

	for (i=0; i<length>>1; i++) {
		Uint16 v = *src;
		r = v & 31;
		g = (v>>5) & 31;
		b = (v>>10) & 31;
		a = (v>>15) & 1;
		if (r+g+b == 0) {
			a = (a ? 1 : 0);
		} else {
			a = 1;
		}
		if (a) {
			*src++ = v | 0x8000;
		} else {
			*src++ = v & 0x7fff;
		}
	}
}

int convert_image(const char *filename)
{
	SDL_RWops *src;
	Uint16 *dstBuffer;
	int dstBufLen;
	int width = 0, height = 0, bpp = 8;
	SDL_Surface *image;

	src = SDL_RWFromFile(filename, "rb");
	if (!src) {
		fprintf(stderr, "Can not open %s for reading\n", filename);
		return 1;
	}
	dstBufLen=SDL_RWseek(src, 0, SEEK_END);
	SDL_RWseek(src, 0, SEEK_SET);

	dstBuffer = (Uint16 *) malloc(dstBufLen);
	if (dstBuffer) {
		SDL_RWread(src, dstBuffer, dstBufLen, 1);
	}
	SDL_RWclose(src);

	if (!dstBuffer) {
		return 1;
	}

	convert_endianness(dstBuffer, dstBufLen);

	convert_alpha(dstBuffer, dstBufLen);

	/* Try to guess width, height */
	switch(dstBufLen) {
		case 2400:
			width=40;
			height=60;
			break;
		case 7168:
			width=128;
			height=56;
			break;
		case 21600:
			width=40;
			height=540;
			break;
		case 32768:
			width=128;
			height=128;
			break;
		case 86400:
			width=40;
			height=2160;
			break;
		case 153600:
			width = 320;
			height = 240;
			bpp = 16;
			break;
	}

	if ((width==0) || (height==0)) {
		fprintf(stderr, "Unknown dimensions for length %d\n", dstBufLen);
		free(dstBuffer);
		return 1;
	}

	image = SDL_CreateRGBSurfaceFrom(dstBuffer, width, height, bpp, (bpp==8 ? width : width<<1),
		31,31<<5,31<<10,1<<15);
	if (image) {
		save_bmp(filename, image);

		SDL_FreeSurface(image);
	}

	free(dstBuffer);
	return 0;
}

int main(int argc, char **argv)
{
	int retval;

	if (argc<2) {
		fprintf(stderr, "Usage: %s /path/to/filename.pix\n", argv[0]);
		return 1;
	}

	if (SDL_Init(SDL_INIT_VIDEO)<0) {
		fprintf(stderr, "Can not initialize SDL: %s\n", SDL_GetError());
		return 1;
	}
	atexit(SDL_Quit);

	retval = convert_image(argv[1]);

	SDL_Quit();
	return retval;
}
