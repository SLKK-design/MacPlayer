#ifndef __COVER_ART__
#define __COVER_ART__

#include <string>

/* Album covers. A cover is kept as its brightness (the JPEG's Y channel) at
   half the JPEG's size: the reduced JPEG decode holds no more detail than
   that (stbi__idct_luma_4x4 in stb_image.h), and at 150x150 it is what the
   cover cache stores (22 KB). For the screen it is dithered with Atkinson
   error diffusion into a 1-bit bitmap of the size the window has room for
   (in full screen not square: the cover is cropped to fill it), once per
   cover and size, and drawn with a single CopyBits.

   The cover used to be kept as raw RGB (270 KB, also on disk) and drawn
   with one FillRect per cell. On an SE/30 that took 30-50 s per cover. */
struct CoverGray
{
	short width, height;
	std::string pixels;  // width * height bytes, 0 = black

	CoverGray() : width(0), height(0) {}
	bool empty() const { return pixels.empty(); }
	void clear() { width = height = 0; pixels.clear(); }
};

/* Halve decoded brightness (w x h, 1 byte per pixel), averaging 2x2 pixels */
CoverGray ShrinkCover(const unsigned char* gray, int w, int h);

/* Row bytes of a cover bitmap width pixels wide */
inline short CoverRowBytes(short width) { return (short)(((width + 15) / 16) * 2); }

/* Scale gray to cover width x height, cropping what sticks out evenly on
   both sides, and dither it to 1 bit; bright pixels become black bits, as
   MacPlayer always drew them. Returns height rows of CoverRowBytes(width)
   bytes. */
std::string RenderCover(const CoverGray& gray, short width, short height);

/* Draw a RenderCover bitmap into the current port, top-left at (left, top) */
void DrawCover(const std::string& bits, short width, short height, short left, short top);

#endif
