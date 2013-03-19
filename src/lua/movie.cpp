#include "lua/internal.hpp"
#include "lua/unsaferewind.hpp"
#include "core/movie.hpp"
#include "core/rrdata.hpp"
#include "core/moviedata.hpp"
#include "core/mainloop.hpp"

extern unsigned extended_mode;

namespace
{
	function_ptr_luafun mcurframe("movie.currentframe", [](lua_State* LS, const std::string& fname) -> int {
		auto& m = get_movie();
		lua_pushnumber(LS, m.get_current_frame());
		return 1;
	});

	function_ptr_luafun mfc("movie.framecount", [](lua_State* LS, const std::string& fname) -> int {
		auto& m = get_movie();
		lua_pushnumber(LS, m.get_frame_count());
		return 1;
	});

	function_ptr_luafun mrrs("movie.rerecords", [](lua_State* LS, const std::string& fname) -> int {
		lua_pushnumber(LS, rrdata::count());
		return 1;
	});

	function_ptr_luafun mro("movie.readonly", [](lua_State* LS, const std::string& fname) -> int {
		auto& m = get_movie();
		lua_pushboolean(LS, m.readonly_mode() ? 1 : 0);
		return 1;
	});

	function_ptr_luafun mrw("movie.readwrite", [](lua_State* LS, const std::string& fname) -> int {
		auto& m = get_movie();
		m.readonly_mode(false);
		m.set_reload_mode(false);
		return 0;
	});

	function_ptr_luafun mgm("movie.getmode", [](lua_State* LS, const std::string& fname) -> int {
		auto& m = get_movie();
		if(m.readonly_mode())
			if(m.get_reload_mode())
				lua_pushstring(LS, "reload");
			else
				lua_pushstring(LS, "readonly");
		else
			lua_pushstring(LS, "readwrite");
		return 1;
	});

	function_ptr_luafun msm("movie.setmode", [](lua_State* LS, const std::string& fname) -> int {
		auto& m = get_movie();
		std::string mod = get_string_argument(LS, 1, fname.c_str());
		if(mod == "reload") {
			m.readonly_mode(true);
			m.set_reload_mode(true);
		} else if(mod == "readonly") {
			m.readonly_mode(true);
			m.set_reload_mode(false);
		} else if(mod == "readwrite") {
			m.readonly_mode(false);
			m.set_reload_mode(false);
		}
		return 0;
	});
	

	function_ptr_luafun mfs("movie.frame_subframes", [](lua_State* LS, const std::string& fname) -> int {
		uint64_t frame = get_numeric_argument<uint64_t>(LS, 1, "movie.frame_subframes");
		auto& m = get_movie();
		lua_pushnumber(LS, m.frame_subframes(frame));
		return 1;
	});

	function_ptr_luafun mrs("movie.read_subframes", [](lua_State* LS, const std::string& fname) -> int {
		uint64_t frame = get_numeric_argument<uint64_t>(LS, 1, "movie.frame_subframes");
		uint64_t subframe = get_numeric_argument<uint64_t>(LS, 2, "movie.frame_subframes");
		auto& m = get_movie();
		controller_frame r = m.read_subframe(frame, subframe);
		lua_newtable(LS);

		lua_pushnumber(LS, 0);
		lua_pushnumber(LS, r.sync() ? 1 : 0);
		lua_settable(LS, -3);
		lua_pushnumber(LS, 1);
		lua_pushnumber(LS, r.reset() ? 1 : 0);
		lua_settable(LS, -3);
		lua_pushnumber(LS, 2);
		lua_pushnumber(LS, r.delay().first);
		lua_settable(LS, -3);
		lua_pushnumber(LS, 3);
		lua_pushnumber(LS, r.delay().second);
		lua_settable(LS, -3);

		if(extended_mode)
			for(size_t i = 0; i < MAX_BUTTONS; i++) {
				lua_pushnumber(LS, i + 4);
				lua_pushnumber(LS, r.axis2(i));
				lua_settable(LS, -3);
			}
		else
			for(size_t i = 0; i < 96; i++) {
				lua_pushnumber(LS, i + 4);
				lua_pushnumber(LS, r.axis2((i / 12) * MAX_CONTROLS_PER_CONTROLLER + (i % 12)));
				lua_settable(LS, -3);
			}
		return 1;
	});

	function_ptr_luafun rrc("movie.read_rtc", [](lua_State* LS, const std::string& fname) -> int {
		lua_pushnumber(LS, our_movie.rtc_second);
		lua_pushnumber(LS, our_movie.rtc_subsecond);
		return 2;
	});

	function_ptr_luafun musv("movie.unsafe_rewind", [](lua_State* LS, const std::string& fname) -> int {
		if(lua_isnoneornil(LS, 1)) {
			//Start process to mark save.
			mainloop_signal_need_rewind(NULL);
		} else if(lua_class<lua_unsaferewind>::is(LS, 1)) {
			//Load the save.
			lua_obj_pin<lua_unsaferewind>* u = lua_class<lua_unsaferewind>::pin(LS, 1, fname.c_str());
			mainloop_signal_need_rewind(u);
		} else {
			lua_pushstring(LS, "movie.unsafe_rewind: Expected nil or UNSAFEREWIND as 1st argument");
			lua_error(LS);
			return 0;
		}
		return 0;
	});
}

