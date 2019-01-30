#include "lua-base.hpp"
#include "framebuffer.hpp"
#include "lua-framebuffer.hpp"

namespace lua
{
framebuffer::color get_fb_color(lua::state& L, int index, const std::string& fname)
{
	if(L.type(index) == LUA_TSTRING)
		return framebuffer::color(L.get_string(index, fname.c_str()));
	else if(L.type(index) == LUA_TNUMBER)
		return framebuffer::color(L.get_numeric_argument<int64_t>(index, fname.c_str()));
	else
		(stringfmt() << "Expected argument #" << index << " to " << fname
			<< " be string or number").throwex();
	return 0; //NOTREACHED
}

framebuffer::color get_fb_color(lua::state& L, int index, const std::string& fname, int64_t dflt)
{
	if(L.type(index) == LUA_TSTRING)
		return framebuffer::color(L.get_string(index, fname.c_str()));
	else if(L.type(index) == LUA_TNUMBER)
		return framebuffer::color(L.get_numeric_argument<int64_t>(index, fname.c_str()));
	else if(L.type(index) == LUA_TNIL || L.type(index) == LUA_TNONE)
		return framebuffer::color(dflt);
	else
		(stringfmt() << "Expected argument #" << index << " to " << fname
			<< " be string, number or nil").throwex();
	return 0; //NOTREACHED
}
}
