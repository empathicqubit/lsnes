#ifndef _library__lua_framebuffer__hpp__included__
#define _library__lua_framebuffer__hpp__included__

#include "lua-base.hpp"
#include "framebuffer.hpp"

namespace lua
{
struct render_context
{
	uint32_t left_gap;
	uint32_t right_gap;
	uint32_t top_gap;
	uint32_t bottom_gap;
	struct framebuffer::queue* queue;
	uint32_t width;
	uint32_t height;
};

framebuffer::color get_fb_color(lua::state& L, int index, const std::string& fname);
framebuffer::color get_fb_color(lua::state& L, int index, const std::string& fname, int64_t dflt);
}

#endif
