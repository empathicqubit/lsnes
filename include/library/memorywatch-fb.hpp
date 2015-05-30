#ifndef _library__memorywatch_fb__hpp__included__
#define _library__memorywatch_fb__hpp__included__

#include "framebuffer.hpp"
#include "memorywatch.hpp"
#include "mathexpr.hpp"

namespace framebuffer
{
	class font2;
}

namespace memorywatch
{
struct output_fb : public item_printer
{
	output_fb();
	~output_fb();
	void set_rqueue(framebuffer::queue& rqueue);
	void set_dtor_cb(std::function<void(output_fb&)> cb);
	void show(const text& iname, const text& val);
	void reset();
	bool cond_enable;
	GC::pointer<mathexpr::mathexpr> enabled;
	GC::pointer<mathexpr::mathexpr> pos_x;
	GC::pointer<mathexpr::mathexpr> pos_y;
	bool alt_origin_x;
	bool alt_origin_y;
	bool cliprange_x;
	bool cliprange_y;
	framebuffer::font2* font;
	framebuffer::color fg;
	framebuffer::color bg;
	framebuffer::color halo;
	//State variables.
	framebuffer::queue* queue;
	std::function<void(output_fb&)> dtor_cb;
};
}

#endif
