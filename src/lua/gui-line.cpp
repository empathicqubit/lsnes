#include "core/instance.hpp"
#include "lua/internal.hpp"
#include "library/framebuffer.hpp"
#include "library/lua-framebuffer.hpp"

namespace
{
	struct render_object_line : public framebuffer::object
	{
		render_object_line(int32_t _x1, int32_t _x2, int32_t _y1, int32_t _y2, framebuffer::color _color)
			throw()
			: x1(_x1), y1(_y1), x2(_x2), y2(_y2), color(_color) {}
		~render_object_line() throw() {}
		template<bool X> void op(struct framebuffer::fb<X>& scr) throw()
		{
			size_t swidth = scr.get_width();
			size_t sheight = scr.get_height();
			int32_t _x1 = x1 + scr.get_origin_x();
			int32_t _x2 = x2 + scr.get_origin_x();
			int32_t _y1 = y1 + scr.get_origin_y();
			int32_t _y2 = y2 + scr.get_origin_y();
			int32_t xdiff = _x2 - _x1;
			int32_t ydiff = _y2 - _y1;
			if(xdiff < 0)
				xdiff = -xdiff;
			if(ydiff < 0)
				ydiff = -ydiff;
			if(xdiff >= ydiff) {
				//X-major line.
				if(x2 < x1) {
					//Swap points so that x1 < x2.
					std::swap(_x1, _x2);
					std::swap(_y1, _y2);
				}
				//The slope of the line is (y2 - y1) / (x2 - x1) = +-ydiff / xdiff
				int32_t y = _y1;
				int32_t ysub = 0;
				for(int32_t x = _x1; x <= _x2; x++) {
					if(x < 0 || static_cast<uint32_t>(x) >= swidth)
						goto nodraw1;
					if(y < 0 || static_cast<uint32_t>(y) >= sheight)
						goto nodraw1;
					color.apply(scr.rowptr(y)[x]);
nodraw1:
					ysub += ydiff;
					if(ysub >= xdiff) {
						ysub -= xdiff;
						if(_y2 > _y1)
							y++;
						else
							y--;
					}
				}
			} else {
				//Y-major line.
				if(_y2 < _y1) {
					//Swap points so that y1 < y2.
					std::swap(_x1, _x2);
					std::swap(_y1, _y2);
				}
				//The slope of the line is (x2 - x1) / (y2 - y1) = +-xdiff / ydiff
				int32_t x = _x1;
				int32_t xsub = 0;
				for(int32_t y = _y1; y <= _y2; y++) {
					if(x < 0 || static_cast<uint32_t>(x) >= swidth)
						goto nodraw2;
					if(y < 0 || static_cast<uint32_t>(y) >= sheight)
						goto nodraw2;
					color.apply(scr.rowptr(y)[x]);
nodraw2:
					xsub += xdiff;
					if(xsub >= ydiff) {
						xsub -= ydiff;
						if(_x2 > _x1)
							x++;
						else
							x--;
					}
				}
			}
		}
		void operator()(struct framebuffer::fb<true>& scr) throw()  { op(scr); }
		void operator()(struct framebuffer::fb<false>& scr) throw() { op(scr); }
		void clone(framebuffer::queue& q) const { q.clone_helper(this); }
	private:
		int32_t x1;
		int32_t y1;
		int32_t x2;
		int32_t y2;
		framebuffer::color color;
	};

	int line(lua::state& L, lua::parameters& P)
	{
		auto& core = CORE();
		int32_t x1, y1, x2, y2;
		framebuffer::color pcolor;

		if(!core.lua2->render_ctx) return 0;

		P(x1, y1, x2, y2, P.optional(pcolor, 0xFFFFFFU));

		core.lua2->render_ctx->queue->create_add<render_object_line>(x1, x2, y1, y2, pcolor);
		return 0;
	}

	lua::functions LUA_line_fns(lua_func_misc, "gui", {
		{"line", line},
	});
}
