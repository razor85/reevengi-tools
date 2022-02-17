/*
	BSS file depacker

	Copyright (C) 2022	Romulo Leitao
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

#include "depack_bsssld.h"
#include "depack_vlc.h"
#include "depack_mdec.h"
#include "file_functions.h"

int convert_image(const char *filename)
{
	SDL_RWops *src;
	int retval = 1;

	src = SDL_RWFromFile(filename, "rb");
	if (!src) {
		fprintf(stderr, "Can not open %s for reading\n", filename);
		return retval;
	}

	SDL_RWseek(src, 0, RW_SEEK_END);
	const size_t fileSize = SDL_RWtell(src);
	const size_t fileInterval = 0x10000;

	size_t currentInterval = 0;
	size_t filenameSuffix = 0;

	const size_t newFilenameLength = strlen(filename) + 15;
	char* newFilename = (char*) malloc(newFilenameLength);
	if (newFilename == NULL) {
		fprintf(stderr, "Failed to allocate new filename\n");
		return 1;
	}

	memset(newFilename, 0, newFilenameLength);
	sprintf(newFilename, "%s", filename);

	char* extensionPosition = strrchr(newFilename, '.');

	while (currentInterval < fileSize) {
		SDL_RWseek(src, currentInterval, RW_SEEK_SET);
		uint16_t length = SDL_ReadLE16(src);
		uint16_t id = SDL_ReadLE16(src);
		uint16_t quant = SDL_ReadLE16(src);
		uint16_t version = SDL_ReadLE16(src);

		printf("ID %x - VERSION %x\n", id, version);
		if (id != 0x3800 || version != 0x0003)
			break;
		
		SDL_RWseek(src, currentInterval, RW_SEEK_SET);
		currentInterval += fileInterval;
		printf("Next interval %d out of %d\n", currentInterval, fileSize);

		uint8_t* dstBuffer = NULL;
		int dstBufLen = 0;

		vlc_depack(src, &dstBuffer, &dstBufLen);
		printf("Reading TIM starting from %d\n", SDL_RWtell(src));

		const uint16_t timPrefix = SDL_ReadLE16(src);
		const int hasTim = ((timPrefix == 0x0000 && SDL_ReadLE16(src) == 0xFFFF) || (timPrefix == 0xFFFF));
		if (hasTim) {
			SDL_RWseek(src, -6, RW_SEEK_CUR);

			const uint32_t timLength = SDL_ReadLE32(src);
			printf("TIM Length: %d\n", timLength);
			if (SDL_ReadLE16(src) != 0xFFFF) {
				printf("Expected 0xFFFF separator at %d\n", SDL_RWtell(src));
				return 1;
			}

			SDL_RWseek(src, -6, RW_SEEK_CUR);
			const size_t restOfFileSize = fileSize - SDL_RWtell(src);

			void* timReadBuffer = malloc(restOfFileSize);
			if (timReadBuffer == NULL) {
				fprintf(stderr, "Failed to allocate new rest of file with %d bytes\n", restOfFileSize);
				return 1;
			}

			Uint8* timDstBuffer = NULL;
			int* timDstBufferLength = 0;

			SDL_RWread(src, timReadBuffer, restOfFileSize, 1);
			bsssld_depack_re2((Uint8*)timReadBuffer, restOfFileSize, &timDstBuffer, &timDstBufferLength);

			char tmpFilenameSuffix[15] = { 0 };
			sprintf(tmpFilenameSuffix, "0%d.TIM", filenameSuffix);
			strcpy(extensionPosition, tmpFilenameSuffix);

			SDL_RWops* timFile = SDL_RWFromFile(newFilename, "wb");
			SDL_RWwrite(timFile, timDstBuffer, timDstBufferLength, 1);
			SDL_RWclose(timFile);
		}

		if (dstBuffer && dstBufLen) {
			SDL_RWops *mdec_src;

			mdec_src = SDL_RWFromMem(dstBuffer, dstBufLen);
			if (mdec_src) {
				Uint8 *dstMdecBuf;
				int dstMdecLen;

				mdec_depack(mdec_src, &dstMdecBuf, &dstMdecLen, 320,240);
				SDL_RWclose(mdec_src);

				if (dstMdecBuf && dstMdecLen) {
					SDL_Surface *image = mdec_surface(dstMdecBuf,320,240,0);
					if (image) {
						char tmpFilenameSuffix[15] = { 0 };
						sprintf(tmpFilenameSuffix, "0%d.BMP", filenameSuffix);
						strcpy(extensionPosition, tmpFilenameSuffix);

						save_bmp(newFilename, image);
						SDL_FreeSurface(image);
						
						retval = 0;
						filenameSuffix++;
					}

					free(dstMdecBuf);
				}
			}

			free(dstBuffer);
		}

		printf("---------------------------------\n");
	}

	SDL_RWclose(src);

	return retval;
}

int main(int argc, char **argv)
{
	int retval;

	if (argc<2) {
		fprintf(stderr, "Usage: %s /path/to/filename.bss\n", argv[0]);
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
