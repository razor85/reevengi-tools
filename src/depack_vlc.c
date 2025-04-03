/*
    VLC file depacker

    Copyright (C) 2007	Patrice Mandin
    Copyright (C) 1997-2000 Psxdev project
        Daniel Balster
        Sergio Moreira
        Andrew Kieschnick
        Kazuki Sakamoto

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
#include <fcntl.h>

#include <SDL.h>

/*--- Defines ---*/

#define VLC_ID 0x3800

#define CODE1(a, b, c) (((a) << 10) | ((b) & 0x3ff) | ((c) << 16))
/* run, level, bit */
#define CODE(a, b, c)  CODE1(a, b, c + 1), CODE1(a, -b, c + 1)
#define CODE0(a, b, c) CODE1(a, b, c), CODE1(a, b, c)
#define CODE2(a, b, c) CODE1(a, b, c + 1), CODE1(a, b, c + 1)
#define VALOF(a)       ((short) ((a) << 6) >> 6)
#define BITOF(a)       ((a) >> 16)
#define EOB            0xfe00
#define ESCAPE_CODE    CODE1(63, 0, 6)
#define EOB_CODE       CODE1(63, 512, 2)

#define Show_Bits(N) (bitbuf >> (32 - (N)))

#define Flush_Buffer(N)                           \
    {                                             \
        bitbuf <<= (N);                           \
        incnt += (N);                             \
        while (incnt >= 0) {                      \
            bitbuf |= SDL_ReadLE16(src) << incnt; \
            incnt -= 16;                          \
        }                                         \
    }

/*
    DC code
    Y		U,V
0	100		00	 	0
1	00x		01x		-1,1
2	01xx		10xx		-3,-2,2,3
3	101xxx		110xxx		-7..-4,4..7
4	110xxxx		1110		-15..-8,8..15
5	1110xxxxx	11110		-31..-16,16..31
6	11110xxxxxx	111110		-63..-32,32..63
7	111110		1111110		-127..-64,64..127
8	1111110		11111110	-255..-128,128..255
    7+8		8+8
*/

/*
    This table based on MPEG2DEC by MPEG Software Simulation Group
*/

/* Table B-14, DCT coefficients	table zero,
 * codes	0100 ... 1xxx (used	for	all	other coefficients)
 */
static const Uint32 VLCtabnext[12 * 2] = { CODE(0, 2, 4), CODE(2, 1, 4), CODE2(1, 1, 3),
    CODE2(1, -1, 3), CODE0(63, 512, 2), CODE0(63, 512, 2), CODE0(63, 512, 2), CODE0(63, 512, 2), /*EOB*/
    CODE2(0, 1, 2), CODE2(0, 1, 2), CODE2(0, -1, 2), CODE2(0, -1, 2) };

/* Table B-14, DCT coefficients	table zero,
 * codes	000001xx ... 00111xxx
 */
static const Uint32 VLCtab0[60 * 2] = { CODE0(63, 0, 6), CODE0(63, 0, 6), CODE0(63, 0, 6),
    CODE0(63, 0, 6), /* ESCAPE */
    CODE2(2, 2, 7), CODE2(2, -2, 7), CODE2(9, 1, 7), CODE2(9, -1, 7), CODE2(0, 4, 7),
    CODE2(0, -4, 7), CODE2(8, 1, 7), CODE2(8, -1, 7), CODE2(7, 1, 6), CODE2(7, 1, 6), CODE2(7, -1, 6),
    CODE2(7, -1, 6), CODE2(6, 1, 6), CODE2(6, 1, 6), CODE2(6, -1, 6), CODE2(6, -1, 6),
    CODE2(1, 2, 6), CODE2(1, 2, 6), CODE2(1, -2, 6), CODE2(1, -2, 6), CODE2(5, 1, 6), CODE2(5, 1, 6),
    CODE2(5, -1, 6), CODE2(5, -1, 6), CODE(13, 1, 8), CODE(0, 6, 8), CODE(12, 1, 8), CODE(11, 1, 8),
    CODE(3, 2, 8), CODE(1, 3, 8), CODE(0, 5, 8), CODE(10, 1, 8), CODE2(0, 3, 5), CODE2(0, 3, 5),
    CODE2(0, 3, 5), CODE2(0, 3, 5), CODE2(0, -3, 5), CODE2(0, -3, 5), CODE2(0, -3, 5),
    CODE2(0, -3, 5), CODE2(4, 1, 5), CODE2(4, 1, 5), CODE2(4, 1, 5), CODE2(4, 1, 5), CODE2(4, -1, 5),
    CODE2(4, -1, 5), CODE2(4, -1, 5), CODE2(4, -1, 5), CODE2(3, 1, 5), CODE2(3, 1, 5), CODE2(3, 1, 5),
    CODE2(3, 1, 5), CODE2(3, -1, 5), CODE2(3, -1, 5), CODE2(3, -1, 5), CODE2(3, -1, 5) };

/* Table B-14, DCT coefficients	table zero,
 * codes	0000001000 ... 0000001111
 */
static const Uint32 VLCtab1[8 * 2] = { CODE(16, 1, 10), CODE(5, 2, 10), CODE(0, 7, 10),
    CODE(2, 3, 10), CODE(1, 4, 10), CODE(15, 1, 10), CODE(14, 1, 10), CODE(4, 2, 10) };

/* Table B-14/15, DCT coefficients table zero /	one,
 * codes	000000010000 ... 000000011111
 */
static const Uint32 VLCtab2[16 * 2] = { CODE(0, 11, 12), CODE(8, 2, 12), CODE(4, 3, 12),
    CODE(0, 10, 12), CODE(2, 4, 12), CODE(7, 2, 12), CODE(21, 1, 12), CODE(20, 1, 12),
    CODE(0, 9, 12), CODE(19, 1, 12), CODE(18, 1, 12), CODE(1, 5, 12), CODE(3, 3, 12),
    CODE(0, 8, 12), CODE(6, 2, 12), CODE(17, 1, 12) };

/* Table B-14/15, DCT coefficients table zero /	one,
 * codes	0000000010000 ... 0000000011111
 */
static const Uint32 VLCtab3[16 * 2] = { CODE(10, 2, 13), CODE(9, 2, 13), CODE(5, 3, 13),
    CODE(3, 4, 13), CODE(2, 5, 13), CODE(1, 7, 13), CODE(1, 6, 13), CODE(0, 15, 13),
    CODE(0, 14, 13), CODE(0, 13, 13), CODE(0, 12, 13), CODE(26, 1, 13), CODE(25, 1, 13),
    CODE(24, 1, 13), CODE(23, 1, 13), CODE(22, 1, 13) };

/* Table B-14/15, DCT coefficients table zero /	one,
 * codes	00000000010000 ... 00000000011111
 */
static const Uint32 VLCtab4[16 * 2] = { CODE(0, 31, 14), CODE(0, 30, 14), CODE(0, 29, 14),
    CODE(0, 28, 14), CODE(0, 27, 14), CODE(0, 26, 14), CODE(0, 25, 14), CODE(0, 24, 14),
    CODE(0, 23, 14), CODE(0, 22, 14), CODE(0, 21, 14), CODE(0, 20, 14), CODE(0, 19, 14),
    CODE(0, 18, 14), CODE(0, 17, 14), CODE(0, 16, 14) };

/* Table B-14/15, DCT coefficients table zero /	one,
 * codes	000000000010000	...	000000000011111
 */
static const Uint32 VLCtab5[16 * 2] = { CODE(0, 40, 15), CODE(0, 39, 15), CODE(0, 38, 15),
    CODE(0, 37, 15), CODE(0, 36, 15), CODE(0, 35, 15), CODE(0, 34, 15), CODE(0, 33, 15),
    CODE(0, 32, 15), CODE(1, 14, 15), CODE(1, 13, 15), CODE(1, 12, 15), CODE(1, 11, 15),
    CODE(1, 10, 15), CODE(1, 9, 15), CODE(1, 8, 15) };

/* Table B-14/15, DCT coefficients table zero /	one,
 * codes	0000000000010000 ... 0000000000011111
 */
static const Uint32 VLCtab6[16 * 2] = { CODE(1, 18, 16), CODE(1, 17, 16), CODE(1, 16, 16),
    CODE(1, 15, 16), CODE(6, 3, 16), CODE(16, 2, 16), CODE(15, 2, 16), CODE(14, 2, 16),
    CODE(13, 2, 16), CODE(12, 2, 16), CODE(11, 2, 16), CODE(31, 1, 16), CODE(30, 1, 16),
    CODE(29, 1, 16), CODE(28, 1, 16), CODE(27, 1, 16) };

/*
    DC code
    Y		U,V
0	100		00	 		0
1	00x		01x			-1,1
2	01xx		10xx			-3,-2,2,3
3	101xxx		110xxx			-7..-4,4..7
4	110xxxx		1110xxxx		-15..-8,8..15
5	1110xxxxx	11110xxxxx		-31..-16,16..31
6	11110xxxxxx	111110xxxxxx		-63..-32,32..63
7	111110xxxxxxx	1111110xxxxxxx		-127..-64,64..127
8	1111110xxxxxxxx	11111110xxxxxxxx	-255..-128,128..255
*/

static const Uint32 DC_Ytab0[48] = {
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),

    CODE1(0, -3, 4),
    CODE1(0, -3, 4),
    CODE1(0, -3, 4),
    CODE1(0, -3, 4),
    CODE1(0, -2, 4),
    CODE1(0, -2, 4),
    CODE1(0, -2, 4),
    CODE1(0, -2, 4),
    CODE1(0, 2, 4),
    CODE1(0, 2, 4),
    CODE1(0, 2, 4),
    CODE1(0, 2, 4),
    CODE1(0, 3, 4),
    CODE1(0, 3, 4),
    CODE1(0, 3, 4),
    CODE1(0, 3, 4),

    CODE1(0, 0, 3),
    CODE1(0, 0, 3),
    CODE1(0, 0, 3),
    CODE1(0, 0, 3),
    CODE1(0, 0, 3),
    CODE1(0, 0, 3),
    CODE1(0, 0, 3),
    CODE1(0, 0, 3),
    CODE1(0, -7, 6),
    CODE1(0, -6, 6),
    CODE1(0, -5, 6),
    CODE1(0, -4, 6),
    CODE1(0, 4, 6),
    CODE1(0, 5, 6),
    CODE1(0, 6, 6),
    CODE1(0, 7, 6),

};

static const Uint32 DC_UVtab0[56] = {
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),
    CODE1(0, 0, 2),

    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, -1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),
    CODE1(0, 1, 3),

    CODE1(0, -3, 4),
    CODE1(0, -3, 4),
    CODE1(0, -3, 4),
    CODE1(0, -3, 4),
    CODE1(0, -2, 4),
    CODE1(0, -2, 4),
    CODE1(0, -2, 4),
    CODE1(0, -2, 4),
    CODE1(0, 2, 4),
    CODE1(0, 2, 4),
    CODE1(0, 2, 4),
    CODE1(0, 2, 4),
    CODE1(0, 3, 4),
    CODE1(0, 3, 4),
    CODE1(0, 3, 4),
    CODE1(0, 3, 4),

    CODE1(0, -7, 6),
    CODE1(0, -6, 6),
    CODE1(0, -5, 6),
    CODE1(0, -4, 6),
    CODE1(0, 4, 6),
    CODE1(0, 5, 6),
    CODE1(0, 6, 6),
    CODE1(0, 7, 6),
};

/*--- Types ---*/

typedef struct {
    Uint16 length;
    Uint16 id;
    Uint16 quant;
    Uint16 version;
} vlc_header_t;

/*--- Variables ---*/

static Uint16* dstPointer;
static int dstBufLen;
static int dstOffset;

static vlc_header_t vlcHeader;

/*--- Functions ---*/

static void vlc_decode(SDL_RWops* src) {
    Uint16 tmp0[2];
    Uint32 bitbuf;
    int incnt, q_code, n, total_length;
    int last_dc[3];

    /* Init buffer */
    tmp0[0] = SDL_ReadLE16(src);
    tmp0[1] = SDL_ReadLE16(src);
    bitbuf = (tmp0[0] << 16) | tmp0[1];
    incnt = -16;

    q_code = vlcHeader.quant << 10;
    n = last_dc[0] = last_dc[1] = last_dc[2] = 0;
    total_length = dstBufLen >> 1 /*(vlcHeader.length+2+32) << 1*/;
    /*printf("%d , %d\n", dstOffset, total_length);*/
    while (dstOffset < total_length) {
        Uint32 code2;

        /* DC */
        if (vlcHeader.version == 2) {
            code2 = Show_Bits(10) | (10 << 16); /* DC code */
        } else {
            code2 = Show_Bits(6);
            if (n >= 2) {
                /* Y */
                if (code2 < 48) {
                    code2 = DC_Ytab0[code2];
                    code2 = (code2 & 0xffff0000) | ((last_dc[2] += VALOF(code2) * 4) & 0x3ff);
                } else {
                    int nbit, val;
                    int bit = 3;
                    while (Show_Bits(bit) & 1) {
                        bit++;
                    }
                    bit++;
                    nbit = bit * 2 - 1;
                    val = Show_Bits(nbit) & ((1 << bit) - 1);
                    if ((val & (1 << (bit - 1))) == 0) {
                        val -= (1 << bit) - 1;
                    }
                    val = (last_dc[2] += val * 4);
                    code2 = (nbit << 16) | (val & 0x3ff);
                }
            } else {
                /* U,V */
                if (code2 < 56) {
                    code2 = DC_UVtab0[code2];
                    code2 = (code2 & 0xffff0000) | ((last_dc[n] += VALOF(code2) * 4) & 0x3ff);
                } else {
                    int nbit, val;
                    int bit = 4;
                    while (Show_Bits(bit) & 1) {
                        bit++;
                    }
                    nbit = bit * 2;
                    val = Show_Bits(nbit) & ((1 << bit) - 1);
                    if ((val & (1 << (bit - 1))) == 0) {
                        val -= (1 << bit) - 1;
                    }
                    val = (last_dc[n] += val * 4);
                    code2 = (nbit << 16) | (val & 0x3ff);
                }
            }
            if (++n == 6) {
                n = 0;
            }
        }
        code2 |= q_code;

        /* AC */
        for (;;) {
#define code code2
#define SBIT 17
            /*printf("%d: 0x%04x\n", dstOffset, code2);*/
            if (dstOffset < total_length) {
                dstPointer[dstOffset++] = SDL_SwapLE16(code2);
            } else {
                fprintf(stderr, "vlc: writing out of range: %d\n", dstOffset * 2);
                /*break;*/
            }
            Flush_Buffer(BITOF(code2));
            code = Show_Bits(SBIT);
            if (code >= 1 << (SBIT - 2)) {
                code2 = VLCtabnext[(code >> 12) - 8];
                if (code2 == EOB_CODE) {
                    break;
                }
            } else if (code >= 1 << (SBIT - 6)) {
                code2 = VLCtab0[(code >> 8) - 8];
                if (code2 == ESCAPE_CODE) {
                    Flush_Buffer(6); /* ESCAPE len */
                    code2 = Show_Bits(16) | (16 << 16);
                }
            } else if (code >= 1 << (SBIT - 7)) {
                code2 = VLCtab1[(code >> 6) - 16];
            } else if (code >= 1 << (SBIT - 8)) {
                code2 = VLCtab2[(code >> 4) - 32];
            } else if (code >= 1 << (SBIT - 9)) {
                code2 = VLCtab3[(code >> 3) - 32];
            } else if (code >= 1 << (SBIT - 10)) {
                code2 = VLCtab4[(code >> 2) - 32];
            } else if (code >= 1 << (SBIT - 11)) {
                code2 = VLCtab5[(code >> 1) - 32];
            } else if (code >= 1 << (SBIT - 12)) {
                code2 = VLCtab6[(code >> 0) - 32];
            } else {
                do {
                    dstPointer[dstOffset++] = SDL_SwapLE16(EOB);
                } while (dstOffset < total_length);
                /*printf("vlc: end at %d bytes written\n", dstOffset*2);*/
                return;
            }
        }
        if (dstOffset < total_length) {
            dstPointer[dstOffset++] = SDL_SwapLE16(code2); /* EOB code */
        } else {
            fprintf(stderr, "vlc: writing out of range: %d\n", dstOffset * 2);
        }
        Flush_Buffer(2); /* EOB bitlen */
    }
    /*printf("vlc: end at %d bytes written\n", dstOffset*2);*/
}

void vlc_depack(SDL_RWops* src, Uint8** dstBufPtr, int* dstLength) {
    *dstBufPtr = NULL;
    *dstLength = 0;

    vlcHeader.length = SDL_ReadLE16(src);
    vlcHeader.id = SDL_ReadLE16(src);
    vlcHeader.quant = SDL_ReadLE16(src);
    vlcHeader.version = SDL_ReadLE16(src);

    if (vlcHeader.id != VLC_ID) {
        return;
    }

    printf("vlc: length=0x%04x, quant=%d\n", vlcHeader.length, vlcHeader.quant);

    dstBufLen = (vlcHeader.length + 2) * sizeof(Uint32) * 2;
    dstPointer = (Uint16*) malloc(dstBufLen);
    if (dstPointer == NULL) {
        return;
    }

    dstOffset = 0;

    dstPointer[dstOffset++] = SDL_SwapLE16(vlcHeader.length);
    dstPointer[dstOffset++] = SDL_SwapLE16(VLC_ID);

    vlc_decode(src);

    /*printf("vlc: final offset: 0x%08x\n", dstOffset);*/

    /* Return depacked buffer */
    *dstBufPtr = (Uint8*) dstPointer;
    *dstLength = dstBufLen;
}
