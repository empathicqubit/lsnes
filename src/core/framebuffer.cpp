#include "cmdhelp/framebuffer.hpp"
#include "core/command.hpp"
#include "core/dispatch.hpp"
#include "core/emustatus.hpp"
#include "core/framebuffer.hpp"
#include "core/instance.hpp"
#include "core/memorywatch.hpp"
#include "core/messages.hpp"
#include "core/misc.hpp"
#include "core/moviedata.hpp"
#include "core/rom.hpp"
#include "core/settings.hpp"
#include "core/subtitles.hpp"
#include "fonts/wrapper.hpp"
#include "library/framebuffer.hpp"
#include "library/framebuffer-pixfmt-lrgb.hpp"
#include "library/minmax.hpp"
#include "library/triplebuffer.hpp"
#include "lua/lua.hpp"

namespace
{
	struct render_list_entry
	{
		uint32_t codepoint;
		uint32_t x;
		uint32_t y;
		uint32_t scale;
	};

	const struct render_list_entry rl_corrupt[] = {
		{'S', 88, 56, 7},
		{'Y', 144, 56, 7},
		{'S', 200, 56, 7},
		{'T', 256, 56, 7},
		{'E', 312, 56, 7},
		{'M', 368, 56, 7},
		{'S', 116, 168, 7},
		{'T', 172, 168, 7},
		{'A', 224, 168, 7},
		{'T', 280, 168, 7},
		{'E', 336, 168, 7},
		{'C', 60, 280, 7},
		{'O', 116, 280, 7},
		{'R', 172, 280, 7},
		{'R', 228, 280, 7},
		{'U', 284, 280, 7},
		{'P', 340, 280, 7},
		{'T', 396, 280, 7},
		{0, 0, 0, 0}
	};

	void draw_special_screen(uint32_t* target, const struct render_list_entry* rlist)
	{
		while(rlist->scale) {
			auto g = main_font.get_glyph(rlist->codepoint);
			for(uint32_t j = 0; j < g.get_height(); j++) {
				for(uint32_t i = 0; i < g.get_width(); i++) {
					if(g.read_pixel(i, j)) {
						uint32_t basex = rlist->x + rlist->scale * i;
						uint32_t basey = rlist->y + rlist->scale * j;
						for(uint32_t j2 = 0; j2 < rlist->scale; j2++)
							for(uint32_t i2 = 0; i2 < rlist->scale; i2++)
								target[(basey + j2) * 512 + (basex + i2)] = 0x7FFFF;
					}
				}
			}
			rlist++;
		}
	}

	void draw_corrupt(uint32_t* target)
	{
		for(unsigned i = 0; i < 512 * 448; i++)
			target[i] = 0x7FC00;
		draw_special_screen(target, rl_corrupt);
	}

	settingvar::supervariable<settingvar::model_int<0, 8191>> SET_dtb(lsnes_setgrp, "top-border",
		"UI‣Top padding", 0);
	settingvar::supervariable<settingvar::model_int<0, 8191>> SET_dbb(lsnes_setgrp, "bottom-border",
		"UI‣Bottom padding", 0);
	settingvar::supervariable<settingvar::model_int<0, 8191>> SET_dlb(lsnes_setgrp, "left-border",
		"UI‣Left padding", 0);
	settingvar::supervariable<settingvar::model_int<0, 8191>> SET_drb(lsnes_setgrp, "right-border",
		"UI‣Right padding", 0);
}

framebuffer::raw emu_framebuffer::screen_corrupt;

emu_framebuffer::emu_framebuffer(subtitle_commentary& _subtitles, settingvar::group& _settings, memwatch_set& _mwatch,
	keyboard::keyboard& _keyboard, emulator_dispatch& _dispatch, lua_state& _lua2, loaded_rom& _rom,
	status_updater& _supdater, command::group& _cmd, input_queue& _iqueue)
	: buffering(buffer1, buffer2, buffer3), subtitles(_subtitles), settings(_settings), mwatch(_mwatch),
	keyboard(_keyboard), edispatch(_dispatch), lua2(_lua2), rom(_rom), supdater(_supdater), cmd(_cmd),
	iqueue(_iqueue), screenshot(cmd, CFRAMEBUF::ss, [this](command::arg_filename a) { this->do_screenshot(a); })
{
	last_redraw_no_lua = false;
}

void emu_framebuffer::do_screenshot(command::arg_filename file)
{
	std::string fn = file;
	take_screenshot(fn);
	messages << "Saved PNG screenshot to '" << fn << "'" << std::endl;
}

void emu_framebuffer::take_screenshot(const std::string& file)
{
	render_info& ri = buffering.get_read();
	ri.fbuf.save_png(file);
	buffering.put_read();
}


void emu_framebuffer::init_special_screens()
{
	std::vector<uint32_t> buf;
	buf.resize(512*448);

	framebuffer::info inf;
	inf.type = &framebuffer::pixfmt_lrgb;
	inf.mem = reinterpret_cast<char*>(&buf[0]);
	inf.physwidth = 512;
	inf.physheight = 448;
	inf.physstride = 2048;
	inf.width = 512;
	inf.height = 448;
	inf.stride = 2048;
	inf.offset_x = 0;
	inf.offset_y = 0;

	draw_corrupt(&buf[0]);
	screen_corrupt = framebuffer::raw(inf);
}

void emu_framebuffer::redraw_framebuffer(framebuffer::raw& todraw, bool no_lua, bool spontaneous)
{
	uint32_t hscl, vscl;
	auto g = rom.get_scale_factors(todraw.get_width(), todraw.get_height());
	hscl = g.first;
	vscl = g.second;
	render_info& ri = buffering.get_write();
	ri.rq.clear();
	struct lua::render_context lrc;
	lrc.left_gap = 0;
	lrc.right_gap = 0;
	lrc.bottom_gap = 0;
	lrc.top_gap = 0;
	lrc.queue = &ri.rq;
	lrc.width = todraw.get_width() * hscl;
	lrc.height = todraw.get_height() * vscl;
	if(!no_lua) {
		lua2.callback_do_paint(&lrc, spontaneous);
		subtitles.render(lrc);
	}
	ri.fbuf = todraw;
	ri.hscl = hscl;
	ri.vscl = vscl;
	ri.lgap = max(lrc.left_gap, (unsigned)SET_dlb(settings));
	ri.rgap = max(lrc.right_gap, (unsigned)SET_drb(settings));
	ri.tgap = max(lrc.top_gap, (unsigned)SET_dtb(settings));
	ri.bgap = max(lrc.bottom_gap, (unsigned)SET_dbb(settings));
	mwatch.watch(ri.rq);
	buffering.put_write();
	edispatch.screen_update();
	last_redraw_no_lua = no_lua;
	supdater.update();
}

void emu_framebuffer::redraw_framebuffer()
{
	framebuffer::raw copy;
	buffering.read_last_write_synchronous([&copy](render_info& ri) { copy = ri.fbuf; });
	//Redraws are never spontaneous
	redraw_framebuffer(copy, last_redraw_no_lua, false);
}

void emu_framebuffer::render_framebuffer()
{
	render_info& ri = buffering.get_read();
	main_screen.reallocate(ri.fbuf.get_width() * ri.hscl + ri.lgap + ri.rgap, ri.fbuf.get_height() * ri.vscl +
		ri.tgap + ri.bgap);
	main_screen.set_origin(ri.lgap, ri.tgap);
	main_screen.copy_from(ri.fbuf, ri.hscl, ri.vscl);
	ri.rq.run(main_screen);
	//We would want divide by 2, but we'll do it ourselves in order to do mouse.
	keyboard::mouse_calibration xcal;
	keyboard::mouse_calibration ycal;
	xcal.offset = ri.lgap;
	ycal.offset = ri.tgap;
	auto kbd = &keyboard;
	iqueue.run_async([kbd, xcal, ycal]() {
		keyboard::key* mouse_x = kbd->try_lookup_key("mouse_x");
		keyboard::key* mouse_y = kbd->try_lookup_key("mouse_y");
		if(mouse_x && mouse_x->get_type() == keyboard::KBD_KEYTYPE_MOUSE)
			mouse_x->cast_mouse()->set_calibration(xcal);
		if(mouse_y && mouse_y->get_type() == keyboard::KBD_KEYTYPE_MOUSE)
			mouse_y->cast_mouse()->set_calibration(ycal);
	}, [](std::exception& e){});
	buffering.put_read();
}

std::pair<uint32_t, uint32_t> emu_framebuffer::get_framebuffer_size()
{
	uint32_t v, h;
	render_info& ri = buffering.get_read();
	v = ri.fbuf.get_width();
	h = ri.fbuf.get_height();
	buffering.put_read();
	return std::make_pair(h, v);
}

framebuffer::raw emu_framebuffer::get_framebuffer()
{
	render_info& ri = buffering.get_read();
	framebuffer::raw copy = ri.fbuf;
	buffering.put_read();
	return copy;
}

void emu_framebuffer::render_kill_request(void* obj)
{
	buffer1.rq.kill_request(obj);
	buffer2.rq.kill_request(obj);
	buffer3.rq.kill_request(obj);
}

framebuffer::raw& emu_framebuffer::render_get_latest_screen()
{
	return buffering.get_read().fbuf;
}

void emu_framebuffer::render_get_latest_screen_end()
{
	buffering.put_read();
}
