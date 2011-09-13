#include "mainloop.hpp"
#include <iomanip>
#include "framerate.hpp"
#include "memorywatch.hpp"
#include "lua.hpp"
#include "rrdata.hpp"
#include "rom.hpp"
#include "movie.hpp"
#include "moviefile.hpp"
#include "render.hpp"
#include "keymapper.hpp"
#include "window.hpp"
#include "settings.hpp"
#include "rom.hpp"
#include "movie.hpp"
#include "window.hpp"
#include <cassert>
#include <sstream>
#include "memorymanip.hpp"
#include "keymapper.hpp"
#include "render.hpp"
#include "videodumper2.hpp"
#include <iostream>
#include "lsnes.hpp"
#include <sys/time.h>
#include <snes/snes.hpp>
#include <ui-libsnes/libsnes.hpp>
#include "framerate.hpp"

#define LOAD_STATE_RW 0
#define LOAD_STATE_RO 1
#define LOAD_STATE_PRESERVE 2
#define LOAD_STATE_MOVIE 3
#define LOAD_STATE_DEFAULT 4
#define SAVE_STATE 0
#define SAVE_MOVIE 1
#define SPECIAL_FRAME_START 0
#define SPECIAL_FRAME_VIDEO 1
#define SPECIAL_SAVEPOINT 2
#define SPECIAL_NONE 3

#define BUTTON_LEFT 0		//Gamepad
#define BUTTON_RIGHT 1		//Gamepad
#define BUTTON_UP 2		//Gamepad
#define BUTTON_DOWN 3		//Gamepad
#define BUTTON_A 4		//Gamepad
#define BUTTON_B 5		//Gamepad
#define BUTTON_X 6		//Gamepad
#define BUTTON_Y 7		//Gamepad
#define BUTTON_L 8		//Gamepad & Mouse
#define BUTTON_R 9		//Gamepad & Mouse
#define BUTTON_SELECT 10	//Gamepad
#define BUTTON_START 11		//Gamepad & Justifier
#define BUTTON_TRIGGER 12	//Superscope.
#define BUTTON_CURSOR 13	//Superscope & Justifier
#define BUTTON_PAUSE 14		//Superscope
#define BUTTON_TURBO 15		//Superscope

void update_movie_state();
void draw_nosignal(uint16_t* target);
void draw_corrupt(uint16_t* target);


namespace
{
	enum advance_mode
	{
		ADVANCE_QUIT,			//Quit the emulator.
		ADVANCE_AUTO,			//Normal (possibly slowed down play).
		ADVANCE_FRAME,			//Frame advance.
		ADVANCE_SUBFRAME,		//Subframe advance.
		ADVANCE_SKIPLAG,		//Skip lag (oneshot, reverts to normal).
		ADVANCE_SKIPLAG_PENDING,	//Activate skip lag mode at next frame.
		ADVANCE_PAUSE,			//Unconditional pause.
	};

	//Analog input physical controller IDs and types.
	int analog[4] = {-1, -1, -1};
	bool analog_is_mouse[4] = {false, false, false};
	//Memory watches.
	std::map<std::string, std::string> memory_watches;
	//Previous mouse mask.
	int prev_mouse_mask = 0;
	//Flags related to repeating advance.
	bool advanced_once;
	bool cancel_advance;
	//Our ROM.
	struct loaded_rom* our_rom;
	//Our movie file.
	struct moviefile our_movie;
	//Handle to the graphics system.
	window* win;
	//The SNES screen.
	struct screen scr;
	//Emulator advance mode. Detemines pauses at start of frame / subframe, etc..
	enum advance_mode amode;
	//Mode and filename of pending load, one of LOAD_* constants.
	int loadmode;
	std::string pending_load;
	//Queued saves (all savestates).
	std::set<std::string> queued_saves;
	bool stepping_into_save;
	//Current controls.
	controls_t curcontrols;
	controls_t autoheld_controls;
	//Emulator status area.
	std::map<std::string, std::string>* status;
	//Pending reset cycles. -1 if no reset pending, otherwise, cycle count for reset.
	long pending_reset_cycles = -1;
	//Set by every video refresh.
	bool video_refresh_done;
	//Special subframe location. One of SPECIAL_* constants.
	int location_special;
	//Types of connected controllers.
	enum porttype_t porttype1 = PT_GAMEPAD;
	enum porttype_t porttype2 = PT_NONE;
	//System corrupt flag.
	bool system_corrupt;
	//Current screen, no signal screen and corrupt screen.
	lcscreen framebuffer;
	lcscreen nosignal_screen;
	lcscreen corrupt_screen;
	//Few settings.
	numeric_setting advance_timeout_first("advance-timeout", 0, 999999999, 500);
	numeric_setting savecompression("savecompression", 0, 9, 7);

	void send_analog_input(int32_t x, int32_t y, unsigned index)
	{
		if(analog_is_mouse[index]) {
			x -= 256;
			y -= (framebuffer.height / 2);
		} else {
			x /= 2;
			y /= 2;
		}
		if(analog[index] < 0) {
			out(win) << "No analog controller in slot #" << (index + 1) << std::endl;
			return;
		}
		curcontrols(analog[index] >> 2, analog[index] & 3, 0) = x;
		curcontrols(analog[index] >> 2, analog[index] & 3, 1) = y;
	}

	void redraw_framebuffer()
	{
		uint32_t hscl = 1, vscl = 1;
		if(framebuffer.width < 512)
			hscl = 2;
		if(framebuffer.height < 400)
			vscl = 2;
		render_queue rq;
		struct lua_render_context lrc;
		lrc.left_gap = 0;
		lrc.right_gap = 0;
		lrc.bottom_gap = 0;
		lrc.top_gap = 0;
		lrc.queue = &rq;
		lrc.width = framebuffer.width * hscl;
		lrc.height = framebuffer.height * vscl;
		lrc.rshift = scr.active_rshift;
		lrc.gshift = scr.active_gshift;
		lrc.bshift = scr.active_bshift;
		lua_callback_do_paint(&lrc, win);
		scr.reallocate(framebuffer.width * hscl + lrc.left_gap + lrc.right_gap, framebuffer.height * vscl +
			lrc.top_gap + lrc.bottom_gap, lrc.left_gap, lrc.top_gap);
		scr.copy_from(framebuffer, hscl, vscl);
		//We would want divide by 2, but we'll do it ourselves in order to do mouse.
		win->set_window_compensation(lrc.left_gap, lrc.top_gap, 1, 1);
		rq.run(scr);
		win->notify_screen_update();
	}

	void fill_special_frames()
	{
		uint16_t buf[512*448];
		draw_nosignal(buf);
		nosignal_screen = lcscreen(buf, 512, 448);
		draw_corrupt(buf);
		corrupt_screen = lcscreen(buf, 512, 448);
	}
}

class firmware_path_setting : public setting
{
public:
	firmware_path_setting() : setting("firmwarepath") { _firmwarepath = "./"; default_firmware = true; }
	void blank() throw(std::bad_alloc, std::runtime_error)
	{
		_firmwarepath = "./";
		default_firmware = true;
	}

	bool is_set() throw()
	{
		return !default_firmware;
	}

	void set(const std::string& value) throw(std::bad_alloc, std::runtime_error)
	{
		_firmwarepath = value;
		default_firmware = false;
	}

	std::string get() throw(std::bad_alloc)
	{
		return _firmwarepath;
	}

	operator std::string() throw(std::bad_alloc)
	{
		return _firmwarepath;
	}
private:
	std::string _firmwarepath;
	bool default_firmware;
} firmwarepath_setting;

class mymovielogic : public movie_logic
{
public:
	mymovielogic() : movie_logic(dummy_movie) {}

	controls_t update_controls(bool subframe) throw(std::bad_alloc, std::runtime_error)
	{
		if(lua_requests_subframe_paint)
			redraw_framebuffer();

		if(subframe) {
			if(amode == ADVANCE_SUBFRAME) {
				if(!cancel_advance && !advanced_once) {
					win->wait_msec(advance_timeout_first);
					advanced_once = true;
				}
				if(cancel_advance) {
					amode = ADVANCE_PAUSE;
					cancel_advance = false;
				}
				win->paused(amode == ADVANCE_PAUSE);
			} else if(amode == ADVANCE_FRAME) {
				;
			} else {
				win->paused(amode == ADVANCE_SKIPLAG || amode == ADVANCE_PAUSE);
				cancel_advance = false;
			}
			if(amode == ADVANCE_SKIPLAG)
				amode = ADVANCE_AUTO;
			location_special = SPECIAL_NONE;
			update_movie_state();
		} else {
			if(amode == ADVANCE_SKIPLAG_PENDING)
				amode = ADVANCE_SKIPLAG;
			if(amode == ADVANCE_FRAME || amode == ADVANCE_SUBFRAME) {
				if(!cancel_advance) {
					win->wait_msec(advanced_once ? to_wait_frame(get_ticks_msec()) :
						advance_timeout_first);
					advanced_once = true;
				}
				if(cancel_advance) {
					amode = ADVANCE_PAUSE;
					cancel_advance = false;
				}
				win->paused(amode == ADVANCE_PAUSE);
			} else {
				win->paused((amode == ADVANCE_PAUSE));
				cancel_advance = false;
			}
			location_special = SPECIAL_FRAME_START;
			update_movie_state();
		}
		win->notify_screen_update();
		win->poll_inputs();
		if(!subframe && pending_reset_cycles >= 0) {
			curcontrols(CONTROL_SYSTEM_RESET) = 1;
			curcontrols(CONTROL_SYSTEM_RESET_CYCLES_HI) = pending_reset_cycles / 10000;
			curcontrols(CONTROL_SYSTEM_RESET_CYCLES_LO) = pending_reset_cycles % 10000;
		} else if(!subframe) {
			curcontrols(CONTROL_SYSTEM_RESET) = 0;
			curcontrols(CONTROL_SYSTEM_RESET_CYCLES_HI) = 0;
			curcontrols(CONTROL_SYSTEM_RESET_CYCLES_LO) = 0;
		}
		controls_t tmp = curcontrols ^ autoheld_controls;
		lua_callback_do_input(tmp, subframe, win);
		return tmp;
	}
private:
	movie dummy_movie;
};

namespace
{
	mymovielogic movb;

	//Lookup physical controller id based on UI controller id and given types (-1 if invalid).
	int lookup_physical_controller(unsigned ui_id)
	{
		bool p1multitap = (porttype1 == PT_MULTITAP);
		unsigned p1devs = port_types[porttype1].devices;
		unsigned p2devs = port_types[porttype2].devices;
		if(ui_id >= p1devs + p2devs)
			return -1;
		if(!p1multitap)
			if(ui_id < p1devs)
				return ui_id;
			else
				return 4 + ui_id - p1devs;
		else
			if(ui_id == 0)
				return 0;
			else if(ui_id < 5)
				return ui_id + 3;
			else
				return ui_id - 4;
	}

	//Look up controller type given UI controller id (note: Non-present controllers give PT_NONE, not the type
	//of port, multitap controllers give PT_GAMEPAD, not PT_MULTITAP, and justifiers give PT_JUSTIFIER, not
	//PT_JUSTIFIERS).
	enum devicetype_t lookup_controller_type(unsigned ui_id)
	{
		int x = lookup_physical_controller(ui_id);
		if(x < 0)
			return DT_NONE;
		enum porttype_t rawtype = (x & 4) ? porttype2 : porttype1;
		if((x & 3) < port_types[rawtype].devices)
			return port_types[rawtype].dtype;
		else
			return DT_NONE;
	}

	void set_analog_controllers()
	{
		unsigned index = 0;
		for(unsigned i = 0; i < 8; i++) {
			enum devicetype_t t = lookup_controller_type(i);
			analog_is_mouse[index] = (t == DT_MOUSE);
			if(t == DT_MOUSE || t == DT_SUPERSCOPE || t == DT_JUSTIFIER) {
				analog[index++] = lookup_physical_controller(i);
			} else
				analog[index] = -1;
		}
		for(; index < 3; index++)
			analog[index] = -1;
	}

	std::map<std::string, std::pair<unsigned, unsigned>> buttonmap;

	const char* buttonnames[] = {
		"left", "right", "up", "down", "A", "B", "X", "Y", "L", "R", "select", "start", "trigger", "cursor",
		"pause", "turbo"
	};

	void init_buttonmap()
	{
		static int done = 0;
		if(done)
			return;
		for(unsigned i = 0; i < 8; i++)
			for(unsigned j = 0; j < sizeof(buttonnames) / sizeof(buttonnames[0]); j++) {
				std::ostringstream x;
				x << (i + 1) << buttonnames[j];
				buttonmap[x.str()] = std::make_pair(i, j);
			}
		done = 1;
	}
	
	//Do button action.
	void do_button_action(unsigned ui_id, unsigned button, short newstate, bool do_xor = false)
	{
		enum devicetype_t p = lookup_controller_type(ui_id);
		int x = lookup_physical_controller(ui_id);
		int bid = -1;
		switch(p) {
		case DT_NONE:
			out(win) << "No such controller #" << (ui_id + 1) << std::endl;
			return;
		case DT_GAMEPAD:
			switch(button) {
			case BUTTON_UP: 	bid = SNES_DEVICE_ID_JOYPAD_UP; break;
			case BUTTON_DOWN:	bid = SNES_DEVICE_ID_JOYPAD_DOWN; break;
			case BUTTON_LEFT:	bid = SNES_DEVICE_ID_JOYPAD_LEFT; break;
			case BUTTON_RIGHT:	bid = SNES_DEVICE_ID_JOYPAD_RIGHT; break;
			case BUTTON_A:		bid = SNES_DEVICE_ID_JOYPAD_A; break;
			case BUTTON_B:		bid = SNES_DEVICE_ID_JOYPAD_B; break;
			case BUTTON_X:		bid = SNES_DEVICE_ID_JOYPAD_X; break;
			case BUTTON_Y:		bid = SNES_DEVICE_ID_JOYPAD_Y; break;
			case BUTTON_L:		bid = SNES_DEVICE_ID_JOYPAD_L; break;
			case BUTTON_R:		bid = SNES_DEVICE_ID_JOYPAD_R; break;
			case BUTTON_SELECT:	bid = SNES_DEVICE_ID_JOYPAD_SELECT; break;
			case BUTTON_START:	bid = SNES_DEVICE_ID_JOYPAD_START; break;
			default:
				out(win) << "Invalid button for gamepad" << std::endl;
				return;
			};
			break;
		case DT_MOUSE:
			switch(button) {
			case BUTTON_L:		bid = SNES_DEVICE_ID_MOUSE_LEFT; break;
			case BUTTON_R:		bid = SNES_DEVICE_ID_MOUSE_RIGHT; break;
			default:
				out(win) << "Invalid button for mouse" << std::endl;
				return;
			};
			break;
		case DT_JUSTIFIER:
			switch(button) {
			case BUTTON_START:	bid = SNES_DEVICE_ID_JUSTIFIER_START; break;
			case BUTTON_TRIGGER:	bid = SNES_DEVICE_ID_JUSTIFIER_TRIGGER; break;
			default:
				out(win) << "Invalid button for justifier" << std::endl;
				return;
			};
			break;
		case DT_SUPERSCOPE:
			switch(button) {
			case BUTTON_TRIGGER:	bid = SNES_DEVICE_ID_SUPER_SCOPE_TRIGGER; break;
			case BUTTON_CURSOR:	bid = SNES_DEVICE_ID_SUPER_SCOPE_CURSOR; break;
			case BUTTON_PAUSE:	bid = SNES_DEVICE_ID_SUPER_SCOPE_PAUSE; break;
			case BUTTON_TURBO:	bid = SNES_DEVICE_ID_SUPER_SCOPE_TURBO; break;
			default:
				out(win) << "Invalid button for superscope" << std::endl;
				return;
			};
			break;
		};
		if(do_xor)
			autoheld_controls((x & 4) ? 1 : 0, x & 3, bid) ^= newstate;
		else
			curcontrols((x & 4) ? 1 : 0, x & 3, bid) = newstate;
	}

	//Recognize and react to [+-]controller commands.
	bool do_button_action(std::string cmd)
	{
		init_buttonmap();
		if(cmd.length() < 12)
			return false;
		std::string prefix = cmd.substr(0, 11);
		if(prefix != "+controller" && prefix != "-controller" && prefix != "controllerh")
			return false;
		std::string button = cmd.substr(11);
		if(!buttonmap.count(button))
			return false;
		auto i = buttonmap[button];
		do_button_action(i.first, i.second, (cmd[0] != '-') ? 1 : 0, (cmd[0] == 'c'));
		return true;
	}

	//Save state.
	void do_save_state(const std::string& filename) throw(std::bad_alloc,
		std::runtime_error)
	{
		lua_callback_pre_save(filename, true, win);
		try {
			uint64_t origtime = get_ticks_msec();
			our_movie.is_savestate = true;
			our_movie.sram = save_sram();
			our_movie.savestate = save_core_state();
			framebuffer.save(our_movie.screenshot);
			auto s = movb.get_movie().save_state();
			our_movie.movie_state.resize(s.size());
			memcpy(&our_movie.movie_state[0], &s[0], s.size());
			our_movie.input = movb.get_movie().save();
			our_movie.save(filename, savecompression);
			uint64_t took = get_ticks_msec() - origtime;
			out(win) << "Saved state '" << filename << "' in " << took << "ms." << std::endl;
			lua_callback_post_save(filename, true, win);
		} catch(std::bad_alloc& e) {
			OOM_panic(win);
		} catch(std::exception& e) {
			win->message(std::string("Save failed: ") + e.what());
			lua_callback_err_save(filename, win);
		}
	}

	//Save movie.
	void do_save_movie(const std::string& filename) throw(std::bad_alloc, std::runtime_error)
	{
		lua_callback_pre_save(filename, false, win);
		try {
			uint64_t origtime = get_ticks_msec();
			our_movie.is_savestate = false;
			our_movie.input = movb.get_movie().save();
			our_movie.save(filename, savecompression);
			uint64_t took = get_ticks_msec() - origtime;
			out(win) << "Saved movie '" << filename << "' in " << took << "ms." << std::endl;
			lua_callback_post_save(filename, false, win);
		} catch(std::bad_alloc& e) {
			OOM_panic(win);
		} catch(std::exception& e) {
			win->message(std::string("Save failed: ") + e.what());
			lua_callback_err_save(filename, win);
		}
	}

	void warn_hash_mismatch(const std::string& mhash, const loaded_slot& slot, const std::string& name)
	{
		if(mhash != slot.sha256) {
			out(win) << "WARNING: " << name << " hash mismatch!" << std::endl
				<< "\tMovie:   " << mhash << std::endl
				<< "\tOur ROM: " << slot.sha256 << std::endl;
		}
	}

	void set_dev(bool port, porttype_t t, bool set_core = true)
	{
		//return;
		switch(set_core ? t : PT_INVALID) {
		case PT_NONE:
			snes_set_controller_port_device(port, SNES_DEVICE_NONE);
			break;
		case PT_GAMEPAD:
			snes_set_controller_port_device(port, SNES_DEVICE_JOYPAD);
			break;
		case PT_MULTITAP:
			snes_set_controller_port_device(port, SNES_DEVICE_MULTITAP);
			break;
		case PT_MOUSE:
			snes_set_controller_port_device(port, SNES_DEVICE_MOUSE);
			break;
		case PT_SUPERSCOPE:
			snes_set_controller_port_device(port, SNES_DEVICE_SUPER_SCOPE);
			break;
		case PT_JUSTIFIER:
			snes_set_controller_port_device(port, SNES_DEVICE_JUSTIFIER);
			break;
		case PT_JUSTIFIERS:
			snes_set_controller_port_device(port, SNES_DEVICE_JUSTIFIERS);
			break;
		case PT_INVALID:
			;
		};
		if(port)
			porttype2 = t;
		else
			porttype1 = t;
		set_analog_controllers();
	}

	//Load state from loaded movie file (does not catch errors).
	void do_load_state(struct moviefile& _movie, int lmode)
	{
		bool will_load_state = _movie.is_savestate && lmode != LOAD_STATE_MOVIE;
		if(gtype::toromtype(_movie.gametype) != our_rom->rtype)
			throw std::runtime_error("ROM types of movie and loaded ROM don't match");
		if(gtype::toromregion(_movie.gametype) != our_rom->orig_region && our_rom->orig_region != REGION_AUTO)
			throw std::runtime_error("NTSC/PAL select of movie and loaded ROM don't match");

		if(_movie.coreversion != bsnes_core_version) {
			if(will_load_state) {
				std::ostringstream x;
				x << "ERROR: Emulator core version mismatch!" << std::endl
					<< "\tThis version: " << bsnes_core_version << std::endl
					<< "\tFile is from: " << _movie.coreversion << std::endl;
				throw std::runtime_error(x.str());	
			} else
				out(win) << "WARNING: Emulator core version mismatch!" << std::endl
					<< "\tThis version: " << bsnes_core_version << std::endl
					<< "\tFile is from: " << _movie.coreversion << std::endl;
		}
		warn_hash_mismatch(_movie.rom_sha256, our_rom->rom, "ROM #1");
		warn_hash_mismatch(_movie.romxml_sha256, our_rom->rom_xml, "XML #1");
		warn_hash_mismatch(_movie.slota_sha256, our_rom->slota, "ROM #2");
		warn_hash_mismatch(_movie.slotaxml_sha256, our_rom->slota_xml, "XML #2");
		warn_hash_mismatch(_movie.slotb_sha256, our_rom->slotb, "ROM #3");
		warn_hash_mismatch(_movie.slotbxml_sha256, our_rom->slotb_xml, "XML #3");

		SNES::config.random = false;
		SNES::config.expansion_port = SNES::System::ExpansionPortDevice::None;

		movie newmovie;
		if(lmode == LOAD_STATE_PRESERVE)
			newmovie = movb.get_movie();
		else
			newmovie.load(_movie.rerecords, _movie.projectid, _movie.input);
		
		if(will_load_state) {
			std::vector<unsigned char> tmp;
			tmp.resize(_movie.movie_state.size());
			memcpy(&tmp[0], &_movie.movie_state[0], tmp.size());
			newmovie.restore_state(tmp, true);
		}
		
		//Negative return.
		rrdata::read_base(_movie.projectid);
		rrdata::add_internal();
		try {
			our_rom->region = gtype::toromregion(_movie.gametype);
			our_rom->load();

			if(_movie.is_savestate && lmode != LOAD_STATE_MOVIE) {
				//Load the savestate and movie state.
				set_dev(false, _movie.port1);
				set_dev(true, _movie.port2);
				load_core_state(_movie.savestate);
				framebuffer.load(_movie.screenshot);
			} else {
				load_sram(_movie.movie_sram, win);
				set_dev(false, _movie.port1);
				set_dev(true, _movie.port2);
				framebuffer = nosignal_screen;
			}
		} catch(std::bad_alloc& e) {
			OOM_panic(win);
		} catch(std::exception& e) {
			system_corrupt = true;
			throw;
		}
		
		//Okay, copy the movie data.
		our_movie = _movie;
		if(!our_movie.is_savestate || lmode == LOAD_STATE_MOVIE) {
			our_movie.is_savestate = false;
			our_movie.host_memory.clear();
		}
		movb.get_movie() = newmovie;
		//Activate RW mode if needed.
		if(lmode == LOAD_STATE_RW)
			movb.get_movie().readonly_mode(false);
		if(lmode == LOAD_STATE_DEFAULT && !(movb.get_movie().get_frame_count()))
			movb.get_movie().readonly_mode(false);
		out(win) << "ROM Type ";
		switch(our_rom->rtype) {
		case ROMTYPE_SNES:
			out(win) << "SNES";
			break;
		case ROMTYPE_BSX:
			out(win) << "BS-X";
			break;
		case ROMTYPE_BSXSLOTTED:
			out(win) << "BS-X slotted";
			break;
		case ROMTYPE_SUFAMITURBO:
			out(win) << "Sufami Turbo";
			break;
		case ROMTYPE_SGB:
			out(win) << "Super Game Boy";
			break;
		default:
			out(win) << "Unknown";
			break;
		}
		out(win) << " region ";
		switch(our_rom->region) {
		case REGION_PAL:
			out(win) << "PAL";
			break;
		case REGION_NTSC:
			out(win) << "NTSC";
			break;
		default:
			out(win) << "Unknown";
			break;
		}
		out(win) << std::endl;
		uint64_t mlength = _movie.get_movie_length();
		{
			mlength += 999999;
			std::ostringstream x;
			if(mlength > 3600000000000) {
				x << mlength / 3600000000000 << ":";
				mlength %= 3600000000000;
			}
			x << std::setfill('0') << std::setw(2) << mlength / 60000000000 << ":";
			mlength %= 60000000000;
			x << std::setfill('0') << std::setw(2) << mlength / 1000000000 << ".";
			mlength %= 1000000000;
			x << std::setfill('0') << std::setw(3) << mlength / 1000000;
			out(win) << "Rerecords " << _movie.rerecords << " length " << x.str() << " ("
				<< _movie.get_frame_count() << " frames)" << std::endl;
		}
		
		if(_movie.gamename != "")
			out(win) << "Game Name: " << _movie.gamename << std::endl;
		for(size_t i = 0; i < _movie.authors.size(); i++)
			out(win) << "Author: " << _movie.authors[i].first << "(" << _movie.authors[i].second << ")"
				<< std::endl;
	}

	//Load state
	void do_load_state(const std::string& filename, int lmode)
	{
		uint64_t origtime = get_ticks_msec();
		lua_callback_pre_load(filename, win);
		struct moviefile mfile;
		try {
			mfile = moviefile(filename);
		} catch(std::bad_alloc& e) {
			OOM_panic(win);
		} catch(std::exception& e) {
			win->message("Can't read movie/savestate '" + filename + "': " + e.what());
			lua_callback_err_load(filename, win);
			return;
		}
		try {
			do_load_state(mfile, lmode);
			uint64_t took = get_ticks_msec() - origtime;
			out(win) << "Loaded '" << filename << "' in " << took << "ms." << std::endl;
			lua_callback_post_load(filename, our_movie.is_savestate, win);
		} catch(std::bad_alloc& e) {
			OOM_panic(win);
		} catch(std::exception& e) {
			win->message("Can't load movie/savestate '" + filename + "': " + e.what());
			lua_callback_err_load(filename, win);
			return;
		}
	}

	//Do pending load (automatically unpauses).
	void mark_pending_load(const std::string& filename, int lmode)
	{
		loadmode = lmode;
		pending_load = filename;
		amode = ADVANCE_AUTO;
		win->cancel_wait();
		win->paused(false);
	}

	//Mark pending save (movies save immediately).
	void mark_pending_save(const std::string& filename, int smode)
	{
		if(smode == SAVE_MOVIE) {
			//Just do this immediately.
			do_save_movie(filename);
			return;
		}
		queued_saves.insert(filename);
		win->message("Pending save on '" + filename + "'");
	}
}

std::vector<char>& get_host_memory()
{
	return our_movie.host_memory;
}

movie& get_movie()
{
	return movb.get_movie();
}

void update_movie_state()
{
	auto& _status = win->get_emustatus();
	{
		std::ostringstream x;
		x << movb.get_movie().get_current_frame() << "(";
		if(location_special == SPECIAL_FRAME_START)
			x << "0";
		else if(location_special == SPECIAL_SAVEPOINT)
			x << "S";
		else if(location_special == SPECIAL_FRAME_VIDEO)
			x << "V";
		else
			x << movb.get_movie().next_poll_number();
		x << ";" << movb.get_movie().get_lag_frames() << ")/" << movb.get_movie().get_frame_count();
		_status["Frame"] = x.str();
	}
	{
		std::ostringstream x;
		if(movb.get_movie().readonly_mode())
			x << "PLAY ";
		else
			x << "REC ";
		if(dump_in_progress())
			x << "CAP ";
		_status["Flags"] = x.str();
	}
	for(auto i = memory_watches.begin(); i != memory_watches.end(); i++) {
		try {
			_status["M[" + i->first + "]"] = evaluate_watch(i->second);
		} catch(...) {
		}
	}
	controls_t c;
	if(movb.get_movie().readonly_mode())
		c = movb.get_movie().get_controls();
	else
		c = curcontrols ^ autoheld_controls;
	for(unsigned i = 0; i < 8; i++) {
		unsigned pindex = lookup_physical_controller(i);
		unsigned port = pindex >> 2;
		unsigned dev = pindex & 3;
		auto ctype = lookup_controller_type(i);
		std::ostringstream x;
		switch(ctype) {
		case DT_GAMEPAD:
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_LEFT) ? "l" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_RIGHT) ? "r" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_UP) ? "u" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_DOWN) ? "d" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_A) ? "A" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_B) ? "B" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_X) ? "X" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_Y) ? "Y" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_L) ? "L" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_R) ? "R" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_START) ? "S" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JOYPAD_SELECT) ? "s" : " ");
			break;
		case DT_MOUSE:
			x << c(port, dev, SNES_DEVICE_ID_MOUSE_X) << " ";
			x << c(port, dev, SNES_DEVICE_ID_MOUSE_Y) << " ";
			x << (c(port, dev, SNES_DEVICE_ID_MOUSE_LEFT) ? "L" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_MOUSE_RIGHT) ? "R" : " ");
			break;
		case DT_SUPERSCOPE:
			x << c(port, dev, SNES_DEVICE_ID_SUPER_SCOPE_X) << " ";
			x << c(port, dev, SNES_DEVICE_ID_SUPER_SCOPE_Y) << " ";
			x << (c(port, dev, SNES_DEVICE_ID_SUPER_SCOPE_TRIGGER) ? "T" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_SUPER_SCOPE_CURSOR) ? "C" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_SUPER_SCOPE_TURBO) ? "t" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_SUPER_SCOPE_PAUSE) ? "P" : " ");
			break;
		case DT_JUSTIFIER:
			x << c(port, dev, SNES_DEVICE_ID_JUSTIFIER_X) << " ";
			x << c(port, dev, SNES_DEVICE_ID_JUSTIFIER_Y) << " ";
			x << (c(port, dev, SNES_DEVICE_ID_JUSTIFIER_START) ? "T" : " ");
			x << (c(port, dev, SNES_DEVICE_ID_JUSTIFIER_TRIGGER) ? "S" : " ");
			break;
		case DT_NONE:
			continue;
		}
		char y[3] = {'P', 0, 0};
		y[1] = 49 + i;
		_status[std::string(y)] = x.str();
	}
}


class my_interface : public SNES::Interface
{
	string path(SNES::Cartridge::Slot slot, const string &hint)
	{
		return static_cast<std::string>(firmwarepath_setting).c_str();
	}

	void video_refresh(const uint16_t *data, bool hires, bool interlace, bool overscan)
	{
		if(stepping_into_save)
			win->message("Got video refresh in runtosave, expect desyncs!");
		video_refresh_done = true;
		bool region = (SNES::system.region() == SNES::System::Region::PAL);
		//std::cerr << "Frame: hires     flag is " << (hires ? "  " : "un") << "set." << std::endl;
		//std::cerr << "Frame: interlace flag is " << (interlace ? "  " : "un") << "set." << std::endl;
		//std::cerr << "Frame: overscan  flag is " << (overscan ? "  " : "un") << "set." << std::endl;
		//std::cerr << "Frame: region    flag is " << (region ? "  " : "un") << "set." << std::endl;
		lcscreen ls(data, hires, interlace, overscan, region);
		framebuffer = ls;
		location_special = SPECIAL_FRAME_VIDEO;
		update_movie_state();
		redraw_framebuffer();

		struct lua_render_context lrc;
		render_queue rq;
		lrc.left_gap = 0;
		lrc.right_gap = 0;
		lrc.bottom_gap = 0;
		lrc.top_gap = 0;
		lrc.queue = &rq;
		lrc.width = framebuffer.width;
		lrc.height = framebuffer.height;
		video_fill_shifts(lrc.rshift, lrc.gshift, lrc.bshift);
		lua_callback_do_video(&lrc, win);
		dump_frame(framebuffer, &rq, lrc.left_gap, lrc.right_gap, lrc.top_gap, lrc.bottom_gap, region, win);
	}
	
	void audio_sample(int16_t l_sample, int16_t r_sample)
	{
		uint16_t _l = l_sample;
		uint16_t _r = r_sample;
		win->play_audio_sample(_l + 32768, _r + 32768);
		dump_audio_sample(_l, _r, win);
	}

	void audio_sample(uint16_t l_sample, uint16_t r_sample)
	{
		//Yes, this interface is broken. The samples are signed but are passed as unsigned!
		win->play_audio_sample(l_sample + 32768, r_sample + 32768);
		dump_audio_sample(l_sample, r_sample, win);
	}

	int16_t input_poll(bool port, SNES::Input::Device device, unsigned index, unsigned id)
	{
		int16_t x;
		x = movb.input_poll(port, index, id);
		//if(id == SNES_DEVICE_ID_JOYPAD_START)
		//	std::cerr << "bsnes polling for start on (" << port << "," << index << ")=" << x << std::endl;
		return x;
	}
};

class mycommandhandler : public aliasexpand_commandhandler
{
public:
	void docommand2(std::string& cmd, window* win) throw(std::bad_alloc, std::runtime_error)
	{
		if(is_cmd_prefix(cmd, "quit-emulator")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			std::string tail = t.tail();
			if(tail == "/y" || win->modal_message("Really quit?", true)) {
				amode = ADVANCE_QUIT;
				win->paused(false);
				win->cancel_wait();
			}
		} else if(is_cmd_prefix(cmd, "pause-emulator")) {
			if(amode != ADVANCE_AUTO) {
				amode = ADVANCE_AUTO;
				win->paused(false);
				win->cancel_wait();
				win->message("Unpaused");
			} else {
				win->cancel_wait();
				cancel_advance = false;
				amode = ADVANCE_PAUSE;
				win->message("Paused");
			}
		} else if(is_cmd_prefix(cmd, "+advance-frame")) {
			amode = ADVANCE_FRAME;
			cancel_advance = false;
			advanced_once = false;
			win->cancel_wait();
			win->paused(false);
		} else if(is_cmd_prefix(cmd, "-advance-frame")) {
			cancel_advance = true;
			win->cancel_wait();
			win->paused(false);
		} else if(is_cmd_prefix(cmd, "+advance-poll")) {
			amode = ADVANCE_SUBFRAME;
			cancel_advance = false;
			advanced_once = false;
			win->cancel_wait();
			win->paused(false);
		} else if(is_cmd_prefix(cmd, "-advance-poll")) {
			cancel_advance = true;
			win->cancel_wait();
			win->paused(false);
		} else if(is_cmd_prefix(cmd, "advance-skiplag")) {
			amode = ADVANCE_SKIPLAG;
			win->cancel_wait();
			win->paused(false);
		} else if(is_cmd_prefix(cmd, "reset")) {
			pending_reset_cycles = 0;
		} else if(is_cmd_prefix(cmd, "load-state")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			mark_pending_load(t.tail(), LOAD_STATE_RW);
		} else if(is_cmd_prefix(cmd, "load-readonly")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			mark_pending_load(t.tail(), LOAD_STATE_RO);
		} else if(is_cmd_prefix(cmd, "load-preserve")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			mark_pending_load(t.tail(), LOAD_STATE_PRESERVE);
		} else if(is_cmd_prefix(cmd, "load-movie")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			mark_pending_load(t.tail(), LOAD_STATE_MOVIE);
		} else if(is_cmd_prefix(cmd, "save-state")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			mark_pending_save(t.tail(), SAVE_STATE);
		} else if(is_cmd_prefix(cmd, "save-movie")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			mark_pending_save(t.tail(), SAVE_MOVIE);
		} else if(is_cmd_prefix(cmd, "set-rwmode")) {
			movb.get_movie().readonly_mode(false);
			lua_callback_do_readwrite(win);
			update_movie_state();
			win->notify_screen_update();
		} else if(is_cmd_prefix(cmd, "set-gamename")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			our_movie.gamename = t.tail();
			out(win) << "Game name changed to '" << our_movie.gamename << "'" << std::endl;
		} else if(is_cmd_prefix(cmd, "get-gamename")) {
			out(win) << "Game name is '" << our_movie.gamename << "'" << std::endl;
		} else if(is_cmd_prefix(cmd, "print-authors")) {
			size_t idx = 0;
			for(auto i = our_movie.authors.begin(); i != our_movie.authors.end(); i++) {
				out(win) << (idx++) << ": " << i->first << "|" << i->second << std::endl;
			}
			out(win) << "End of authors list" << std::endl;
		} else if(is_cmd_prefix(cmd, "repaint")) {
			redraw_framebuffer();
		} else if(is_cmd_prefix(cmd, "add-author")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			fieldsplitter f(t.tail());
			std::string full = f;
			std::string nick = f;
			if(full == "" && nick == "") {
				out(win) << "syntax: add-author <author>" << std::endl;
				return;
			}
			our_movie.authors.push_back(std::make_pair(full, nick));
			out(win) << (our_movie.authors.size() - 1) << ": " << full << "|" << nick << std::endl;
		} else if(is_cmd_prefix(cmd, "remove-author")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			uint64_t index;
			try {
				index = parse_value<uint64_t>(t.tail());
			} catch(std::exception& e) {
				out(win) << "syntax: remove-author <authornum>" << std::endl;
				return;
			}
			if(index >= our_movie.authors.size()) {
				out(win) << "No such author" << std::endl;
				return;
			}
			our_movie.authors.erase(our_movie.authors.begin() + index);
		} else if(is_cmd_prefix(cmd, "edit-author")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			uint64_t index;
			try {
				index = parse_value<uint64_t>(t);
			} catch(std::exception& e) {
				out(win) << "syntax: edit-author <authornum> <author>" << std::endl;
				return;
			}
			if(index >= our_movie.authors.size()) {
				out(win) << "No such author" << std::endl;
				return;
			}
			fieldsplitter f(t.tail());
			std::string full = f;
			std::string nick = f;
			if(full == "" && nick == "") {
				out(win) << "syntax: edit-author <authornum> <author>" << std::endl;
				return;
			}
			our_movie.authors[index] = std::make_pair(full, nick);;
		} else if(is_cmd_prefix(cmd, "add-watch")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			std::string name = t;
			if(name == "" || t.tail() == "") {
				out(win) << "syntax: add-watch <name> <expr>" << std::endl;
				return;
			}
			std::cerr << "Add watch: '" << name << "'" << std::endl;
			memory_watches[name] = t.tail();
			update_movie_state();
		} else if(is_cmd_prefix(cmd, "remove-watch")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			std::string name = t;
			if(name == "" || t.tail() != "") {
				out(win) << "syntax: remove-watch <name>" << std::endl;
				return;
			}
			std::cerr << "Erase watch: '" << name << "'" << std::endl;
			memory_watches.erase(name);
			auto& _status = win->get_emustatus();
			_status.erase("M[" + name + "]");
			update_movie_state();
		} else if(is_cmd_prefix(cmd, "test-1")) {
			framebuffer = nosignal_screen;
			redraw_framebuffer();
		} else if(is_cmd_prefix(cmd, "test-2")) {
			framebuffer = corrupt_screen;
			redraw_framebuffer();
		} else if(is_cmd_prefix(cmd, "test-3")) {
			while(1);
		} else if(is_cmd_prefix(cmd, "take-screenshot")) {
			try {
				tokensplitter t(cmd);
				std::string dummy = t;
				framebuffer.save_png(t.tail());
				out(win) << "Saved PNG screenshot" << std::endl;
			} catch(std::bad_alloc& e) {
				OOM_panic(win);
			} catch(std::exception& e) {
				out(win) << "Can't save PNG: " << e.what() << std::endl;
			}
		} else if(is_cmd_prefix(cmd, "mouse_button")) {
			tokensplitter t(cmd);
			std::string dummy = t;
			std::string x = t;
			std::string y = t;
			std::string b = t;
			int _x = atoi(x.c_str());
			int _y = atoi(y.c_str());
			int _b = atoi(b.c_str());
			if(_b & ~prev_mouse_mask & 1)
				send_analog_input(_x, _y, 0);
			if(_b & ~prev_mouse_mask & 2)
				send_analog_input(_x, _y, 1);
			if(_b & ~prev_mouse_mask & 4)
				send_analog_input(_x, _y, 2);
			prev_mouse_mask = _b;
		} else if(do_button_action(cmd)) {
			update_movie_state();
			win->notify_screen_update();
			return;
		}
		else if(cmd == "")
			;
		else
			win->message("Unrecognized command: " + cmd);
	}
};

namespace
{
	//If there is a pending load, perform it.
	bool handle_load()
	{
		if(pending_load != "") {
			do_load_state(pending_load, loadmode);
			redraw_framebuffer();
			pending_load = "";
			pending_reset_cycles = -1;
			amode = ADVANCE_AUTO;
			win->cancel_wait();
			win->paused(false);
			if(!system_corrupt) {
				location_special = SPECIAL_SAVEPOINT;
				update_movie_state();
				win->notify_screen_update();
				win->poll_inputs();
			}
			return true;
		}
		return false;
	}

	//If there are pending saves, perform them.
	void handle_saves()
	{
		if(!queued_saves.empty()) {
			stepping_into_save = true;
			SNES::system.runtosave();
			stepping_into_save = false;
			for(auto i = queued_saves.begin(); i != queued_saves.end(); i++)
				do_save_state(*i);
		}
		queued_saves.clear();
	}

	//Do (delayed) reset. Return true if proper, false if forced at frame boundary.
	bool handle_reset(long cycles)
	{
		if(cycles == 0) {
			win->message("SNES reset");
			SNES::system.reset();
			framebuffer = nosignal_screen;
			lua_callback_do_reset(win);
			redraw_framebuffer();
		} else if(cycles > 0) {
			video_refresh_done = false;
			long cycles_executed = 0;
			out(win) << "Executing delayed reset... This can take some time!" << std::endl;
			while(cycles_executed < cycles && !video_refresh_done) {
				SNES::cpu.op_step();
				cycles_executed++;
			}
			if(!video_refresh_done)
				out(win) << "SNES reset (delayed " << cycles_executed << ")" << std::endl;
			else
				out(win) << "SNES reset (forced at " << cycles_executed << ")" << std::endl;
			SNES::system.reset();
			framebuffer = nosignal_screen;
			lua_callback_do_reset(win);
			redraw_framebuffer();
			if(video_refresh_done) {
				to_wait_frame(get_ticks_msec());
				return false;
			}
		}
		return true;
	}

	bool handle_corrupt()
	{
		if(!system_corrupt)
			return false;
		while(system_corrupt) {
			framebuffer = corrupt_screen;
			redraw_framebuffer();
			win->cancel_wait();
			win->paused(true);
			win->poll_inputs();
			handle_load();
			if(amode == ADVANCE_QUIT)
				return true;
		}
		return true;
	}

	void print_controller_mappings()
	{
		for(unsigned i = 0; i < 8; i++) {
			std::string type = "unknown";
			if(lookup_controller_type(i) == DT_NONE)
				type = "disconnected";
			if(lookup_controller_type(i) == DT_GAMEPAD)
				type = "gamepad";
			if(lookup_controller_type(i) == DT_MOUSE)
				type = "mouse";
			if(lookup_controller_type(i) == DT_SUPERSCOPE)
				type = "superscope";
			if(lookup_controller_type(i) == DT_JUSTIFIER)
				type = "justifier";
			out(win) << "Physical controller mapping: Logical " << (i + 1) << " is physical " <<
				lookup_physical_controller(i) << " (" << type << ")" << std::endl;
		}
	}
}

void main_loop(window* _win, struct loaded_rom& rom, struct moviefile& initial) throw(std::bad_alloc,
	std::runtime_error)
{
	//Basic initialization.
	win = _win;
	our_rom = &rom;
	my_interface intrf;
	auto old_inteface = SNES::system.interface;
	SNES::system.interface = &intrf;
	mycommandhandler handler;
	win->set_commandhandler(handler);
	status = &win->get_emustatus();
	fill_special_frames();

	//Load our given movie.
	bool first_round = false;
	bool just_did_loadstate = false;
	try {
		do_load_state(initial, LOAD_STATE_DEFAULT);
		first_round = our_movie.is_savestate;
		just_did_loadstate = first_round;
	} catch(std::bad_alloc& e) {
		OOM_panic(win);
	} catch(std::exception& e) {
		win->message(std::string("FATAL: Can't load initial state: ") + e.what());
		win->fatal_error();
		return;
	}

	lua_set_commandhandler(handler);
	lua_callback_startup(win);

	//print_controller_mappings();

	win->set_main_surface(scr);
	redraw_framebuffer();
	win->paused(false);
	amode = ADVANCE_PAUSE;
	while(amode != ADVANCE_QUIT) {
		if(handle_corrupt()) {
			first_round = our_movie.is_savestate;
			just_did_loadstate = true;
			continue;
		}
		long resetcycles = -1;
		ack_frame_tick(get_ticks_msec());
		if(amode == ADVANCE_SKIPLAG_PENDING)
			amode = ADVANCE_SKIPLAG;

		if(!first_round) {
			resetcycles = movb.new_frame_starting(amode == ADVANCE_SKIPLAG);
			if(amode == ADVANCE_QUIT)
				break;
			bool delayed_reset = (resetcycles > 0);
			pending_reset_cycles = -1;
			if(!handle_reset(resetcycles)) {
				continue;
			}
			if(!delayed_reset) {
				handle_saves();
			}
			if(handle_load()) {
				first_round = our_movie.is_savestate;
				amode = ADVANCE_PAUSE;
				just_did_loadstate = first_round;
				continue;
			}
		}
		if(just_did_loadstate) {
			if(amode == ADVANCE_QUIT)
				break;
			amode = ADVANCE_PAUSE;
			redraw_framebuffer();
			win->cancel_wait();
			win->paused(true);
			win->poll_inputs();
			just_did_loadstate = false;
		}
		SNES::system.run();
		if(amode == ADVANCE_AUTO)
			win->wait_msec(to_wait_frame(get_ticks_msec()));
		first_round = false;
	}
	end_vid_dump();
	SNES::system.interface = old_inteface;
}
