#include "framebuffer-pixfmt-lrgb.hpp"
#include "framebuffer.hpp"

namespace framebuffer
{
_pixfmt_lrgb::~_pixfmt_lrgb() throw()
{
}

template<typename T> static inline T convert_xcolor(uint32_t word, uint8_t rshift, uint8_t gshift,
	uint8_t bshift, T A, T B)
{
	const int sh = 6 * sizeof(T);
	T l = A * ((word >> 15) & 0xF);
	T r = (word >> 0) & 0x1F;
	T g = (word >> 5) & 0x1F;
	T b = (word >> 10) & 0x1F;
	T x = (l*r+B) >> sh << rshift;
	x += (l*g+B) >> sh << gshift;
	x += (l*b+B) >> sh << bshift;
	return x;
}

static inline uint32_t convert_lowcolor(uint32_t word, uint8_t rshift, uint8_t gshift, uint8_t bshift)
{
	return convert_xcolor<uint32_t>(word, rshift, gshift, bshift, 9200409, 8370567);
}

static inline uint64_t convert_hicolor(uint32_t word, uint8_t rshift, uint8_t gshift, uint8_t bshift)
{
	return convert_xcolor<uint64_t>(word, rshift, gshift, bshift, 39669812040285683ULL,
		140434827090047ULL);
}

void _pixfmt_lrgb::decode(uint32_t* target, const uint8_t* src, size_t width)
	throw()
{
	const uint32_t* _src = reinterpret_cast<const uint32_t*>(src);
	for(size_t i = 0; i < width; i++) {
		target[i] = convert_lowcolor(_src[i], 16, 8, 0);
	}
}

void _pixfmt_lrgb::decode(uint32_t* target, const uint8_t* src, size_t width,
	const auxpalette<false>& auxp) throw()
{
	const uint32_t* _src = reinterpret_cast<const uint32_t*>(src);
	for(size_t i = 0; i < width; i++)
		target[i] = auxp.pcache[_src[i] & 0x7FFFF];
}

void _pixfmt_lrgb::decode(uint64_t* target, const uint8_t* src, size_t width,
	const auxpalette<true>& auxp) throw()
{
	const uint32_t* _src = reinterpret_cast<const uint32_t*>(src);
	for(size_t i = 0; i < width; i++)
		target[i] = auxp.pcache[_src[i] & 0x7FFFF];
}

void _pixfmt_lrgb::set_palette(auxpalette<false>& auxp, uint8_t rshift, uint8_t gshift,
	uint8_t bshift)
{
	auxp.pcache.resize(0x80000);
	for(size_t i = 0; i < 0x80000; i++) {
		auxp.pcache[i] = convert_lowcolor(i, rshift, gshift, bshift);
	}
	auxp.rshift = rshift;
	auxp.gshift = gshift;
	auxp.bshift = bshift;
}

void _pixfmt_lrgb::set_palette(auxpalette<true>& auxp, uint8_t rshift, uint8_t gshift,
	uint8_t bshift)
{
	auxp.pcache.resize(0x80000);
	for(size_t i = 0; i < 0x80000; i++) {
		auxp.pcache[i] = convert_hicolor(i, rshift, gshift, bshift);
	}
	auxp.rshift = rshift;
	auxp.gshift = gshift;
	auxp.bshift = bshift;
}

uint8_t _pixfmt_lrgb::get_bpp() throw()
{
	return 4;
}

uint8_t _pixfmt_lrgb::get_ss_bpp() throw()
{
	return 3;
}

uint32_t _pixfmt_lrgb::get_magic() throw()
{
	return 0;
}

_pixfmt_lrgb pixfmt_lrgb;
}
