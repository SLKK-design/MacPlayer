/* CoverArt.cpp — see CoverArt.h */

#include <string.h>
#include <vector>
#include <Quickdraw.h>
#include "CoverArt.h"

using namespace std;

CoverGray ShrinkCover(const unsigned char* gray, int w, int h)
{
	CoverGray out;
	if (w < 2 || h < 2)
		return out;

	out.width = (short)(w / 2);
	out.height = (short)(h / 2);
	out.pixels.resize((size_t)out.width * out.height);
	unsigned char* d = (unsigned char*)&out.pixels[0];

	for (int y = 0; y < out.height; y++)
	{
		const unsigned char* r0 = gray + (long)(2 * y) * w;
		const unsigned char* r1 = r0 + w;
		for (int x = 0; x < out.width; x++, r0 += 2, r1 += 2)
			*d++ = (unsigned char)((r0[0] + r0[1] + r1[0] + r1[1] + 2) >> 2);
	}
	return out;
}

string RenderCover(const CoverGray& gray, short width, short height)
{
	if (width <= 0 || height <= 0)
		return string();
	short rowBytes = CoverRowBytes(width);
	string cover((size_t)rowBytes * height, '\0');
	if (gray.empty())
		return cover;

	unsigned char* bits = (unsigned char*)&cover[0];
	const unsigned char* pixels = (const unsigned char*)gray.pixels.data();
	long w = gray.width, h = gray.height;

	// Scale so the cover fills width x height: num/den source pixels per
	// output pixel, from the side that needs the larger scale. The other
	// side sticks out and is cropped evenly (x0, y0).
	long num = w, den = width;
	if ((long)width * h < (long)height * w)
	{
		num = h;
		den = height;
	}
	long x0 = (w - width * num / den) / 2;
	long y0 = (h - height * num / den) / 2;

	// Source column of every output column, computed once. One source pixel
	// per output pixel: the cover is scaled up (150 px to 248 or more).
	vector<short> xs(width);
	for (int x = 0; x < width; x++)
	{
		long sx = x0 + x * num / den;
		xs[x] = (short)(sx < w ? sx : w - 1);
	}

	// Atkinson error diffusion, as in MacPaint and HyperCard: every pixel
	// passes 1/8 of its error to six neighbours (two to the right, three
	// below, one two lines down) and drops the remaining quarter, which
	// keeps the picture crisp and high-contrast. Error rows for this line
	// and the next two, offset by one so x-1 fits.
	int stride = width + 3;
	vector<short> err(3 * stride, 0);

	for (int y = 0; y < height; y++)
	{
		long sy = y0 + y * num / den;
		const unsigned char* src = pixels + (sy < h ? sy : h - 1) * w;
		short* e0 = &err[(y % 3) * stride] + 1;
		short* e1 = &err[((y + 1) % 3) * stride] + 1;
		short* e2 = &err[((y + 2) % 3) * stride] + 1;
		unsigned char* out = bits + (long)y * rowBytes;
		unsigned char acc = 0;

		for (int x = 0; x < width; x++)
		{
			int v = src[xs[x]] + e0[x];
			int q;
			acc <<= 1;
			if (v > 127)
			{
				acc |= 1;             // inverted: bright -> black bit
				q = (v - 255) >> 3;
			}
			else
				q = v >> 3;

			e0[x + 1] += q;  e0[x + 2] += q;
			e1[x - 1] += q;  e1[x] += q;  e1[x + 1] += q;
			e2[x] += q;

			if ((x & 7) == 7)
				*out++ = acc;
		}
		if (width & 7)
			*out = (unsigned char)(acc << (8 - (width & 7)));

		// This line's row is reused for the line two below the next
		memset(e0 - 1, 0, stride * sizeof(short));
	}

	return cover;
}

void DrawCover(const string& bits, short width, short height, short left, short top)
{
	short rowBytes = CoverRowBytes(width);
	if (width <= 0 || height <= 0 || bits.size() != (size_t)rowBytes * height)
		return;

	BitMap bm;
	bm.baseAddr = (Ptr)bits.data();
	bm.rowBytes = rowBytes;
	MacSetRect(&bm.bounds, 0, 0, width, height);

	Rect dest;
	MacSetRect(&dest, left, top, left + width, top + height);

	GrafPtr port;
	GetPort(&port);
	CopyBits(&bm, &port->portBits, &bm.bounds, &dest, srcCopy, nil);
}
