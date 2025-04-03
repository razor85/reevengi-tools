/*
  ADT file depacker

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
#    include "config.h"
#endif

#include <SDL.h>

#include "depack_adt.h"
#include "file_functions.h"
#include "param.h"

/*--- Defines ---*/

#define MAGIC_TIM   0x10
#define TIM_TYPE_4  8
#define TIM_TYPE_8  9
#define TIM_TYPE_16 2

#define ADT_DEPACKED_RAW 0 /* raw 16 bits image, saved as bmp */
#define ADT_DEPACKED_TIM 1 /* tim image, saved as is */
#define ADT_DEPACKED_UNK 2 /* other type, saved as raw */

/*--- Variables ---*/

/* Keep ADT raw images depacked as is
 * Some RE2 PC versions do not organize them as 256x256 block+64x128 blocks
 */
static int noreorg = 0;

/*--- Functions prototypes ---*/

int convert_image(const char* filename);

/*--- Functions ---*/

int main(int argc, char** argv) {
    int retval;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-noreorg] /path/to/filename.adt\n", argv[0]);
        return 1;
    }

    if (param_check("-noreorg", argc, argv) >= 0) {
        noreorg = 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Can not initialize SDL: %s\n", SDL_GetError());
        return 1;
    }
    atexit(SDL_Quit);

    retval = convert_image(argv[argc - 1]);

    SDL_Quit();
    return retval;
}

int convert_image(const char* filename) {
    Uint8* dstBuffer = NULL;
    int dstBufLen = 0;
    int retval = 1;

    FILE* src = fopen(filename, "rb");
    if (!src) {
        printf("Can not open %s for reading\n", filename);
        return retval;
    }

    adt_depack(src, &dstBuffer, &dstBufLen);
    fclose(src);

    printf("Read %d bytes from blocks\n", dstBufLen);

    Uint32* tmpBufferPtr = (Uint32*) dstBuffer;
    Uint32 missingBytes = dstBufLen;
    while (missingBytes >= 2) {
        /*
        if (dstBufLen == 320 * 256 * 2) {
          Uint32* tmp = (Uint32*)dstBuffer;
          Uint32 offset = SDL_SwapLE32(tmp[0]);

          if (offset < dstBufLen) {
            // Search header for TIM image
            offset >>= 2;
            if (SDL_SwapLE32(tmp[offset]) != MAGIC_TIM) {
              img_type = ADT_DEPACKED_RAW;
            }
            else if ((SDL_SwapLE32(tmp[offset + 1]) != TIM_TYPE_4)
              && (SDL_SwapLE32(tmp[offset + 1]) != TIM_TYPE_8)
              && (SDL_SwapLE32(tmp[offset + 1]) != TIM_TYPE_16))
            {
              img_type = ADT_DEPACKED_RAW;
            }
          }
        }
        else {
          img_type = ADT_DEPACKED_TIM;
        }
        */

        int img_type = ADT_DEPACKED_UNK;
        if (SDL_SwapLE32(tmpBufferPtr[0]) == MAGIC_TIM &&
            (SDL_SwapLE32(tmpBufferPtr[1]) == TIM_TYPE_4 || SDL_SwapLE32(tmpBufferPtr[1]) == TIM_TYPE_8 ||
                SDL_SwapLE32(tmpBufferPtr[1]) == TIM_TYPE_16)) {
            img_type = ADT_DEPACKED_TIM;
        } else {
            img_type = ADT_DEPACKED_RAW;
        }

        switch (img_type) {
        case ADT_DEPACKED_RAW: {
            /* Raw image, save as BMP */
            printf("Saving depacked raw at %d\n", (Uint32) tmpBufferPtr - (Uint32) dstBuffer);
            SDL_Surface* image = adt_surface((Uint16*) tmpBufferPtr, noreorg ^ 1);
            if (image) {
                save_bmp(filename, image);
                SDL_FreeSurface(image);

                retval = 0;
                tmpBufferPtr += ((256 * 256 * 2) + (128 * 128 * 2)) / 4;
                missingBytes -= ((256 * 256 * 2) + (128 * 128 * 2));
            }
        } break;
        case ADT_DEPACKED_TIM: {
            /* Tim image */
            printf("Saving tim at %d\n", (Uint32) tmpBufferPtr - (Uint32) dstBuffer);
            save_tim(filename, (Uint8*) tmpBufferPtr, missingBytes);
            missingBytes = 0;
            retval = 0;
        } break;
        case ADT_DEPACKED_UNK: {
            /* Unknown, save raw data */
            save_raw(filename, (Uint8*) tmpBufferPtr, missingBytes);
            missingBytes = 0;
            retval = 0;
        } break;
        default:
            printf("Invalid image type %d\n", img_type);
            retval = 1;
            break;
        }
    }

    free(dstBuffer);

    return retval;
}
