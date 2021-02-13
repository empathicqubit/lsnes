#include "framebuffer-pixfmt-lrgb.hpp"
#include "framebuffer.hpp"

namespace framebuffer
{
_pixfmt_lrgb::~_pixfmt_lrgb() throw()
{
}

static inline uint32_t convert_lowcolor(uint32_t word, uint8_t rshift, uint8_t gshift, uint8_t bshift)
{
	uint32_t l = ((word >> 15) & 0xF);
	uint32_t r = l * ((word >> 0) & 0x1F);
	uint32_t g = l * ((word >> 5) & 0x1F);
	uint32_t b = l * ((word >> 10) & 0x1F);
	uint32_t x = (((r << 8) - r + 232) / 465) << rshift;
	x += (((g << 8) - g + 232) / 465) << gshift;
	x += (((b << 8) - b + 232) / 465) << bshift;
	return x;
}

static inline uint64_t convert_hicolor(uint32_t word, uint8_t rshift, uint8_t gshift, uint8_t bshift)
{
	uint64_t l = ((word >> 15) & 0xF);
	uint64_t b = l * ((word >> 0) & 0x1F);
	uint64_t g = l * ((word >> 5) & 0x1F);
	uint64_t r = l * ((word >> 10) & 0x1F);
	uint64_t x = (((r << 16) - r + 232) / 465) << rshift;
	x += (((g << 16) - g + 232) / 465) << gshift;
	x += (((b << 16) - b + 232) / 465) << bshift;
	return x;
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
