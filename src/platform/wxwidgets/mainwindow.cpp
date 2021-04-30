#include "lsnes.hpp"

#include <wx/dnd.h>
#include "platform/wxwidgets/menu_dump.hpp"
#include "platform/wxwidgets/menu_upload.hpp"
#include "platform/wxwidgets/platform.hpp"
#include "platform/wxwidgets/loadsave.hpp"
#include "platform/wxwidgets/window_mainwindow.hpp"
#include "platform/wxwidgets/window_messages.hpp"
#include "platform/wxwidgets/window_status.hpp"
#include "platform/wxwidgets/window-romload.hpp"
#include "platform/wxwidgets/settings-common.hpp"
#include "platform/wxwidgets/menu_tracelog.hpp"
#include "platform/wxwidgets/menu_branches.hpp"
#include "platform/wxwidgets/menu_projects.hpp"

#include "cmdhelp/framebuffer.hpp"
#include "cmdhelp/loadsave.hpp"
#include "cmdhelp/lua.hpp"
#include "cmdhelp/subtitles.hpp"
#include "core/audioapi.hpp"
#include "core/audioapi-driver.hpp"
#include "core/command.hpp"
#include "core/controller.hpp"
#include "core/controllerframe.hpp"
#include "core/dispatch.hpp"
#include "core/emustatus.hpp"
#include "core/framebuffer.hpp"
#include "core/framerate.hpp"
#include "core/instance.hpp"
#include "core/keymapper.hpp"
#include "core/ui-services.hpp"
#include "interface/romtype.hpp"
#include "core/loadlib.hpp"
#include "lua/lua.hpp"
#include "core/mainloop.hpp"
#include "core/memorywatch.hpp"
#include "core/messages.hpp"
#include "core/misc.hpp"
#include "core/moviedata.hpp"
#include "core/project.hpp"
#include "core/rom.hpp"
#include "core/settings.hpp"
#include "core/window.hpp"
#include "library/directory.hpp"
#include "library/minmax.hpp"
#include "library/string.hpp"
#include "library/zip.hpp"
#if defined(_WIN32) || defined(_WIN64) || defined(TEST_WIN32_CODE)
#define FUCKED_SYSTEM
#endif

#include <cmath>
#include <vector>
#include <string>


extern "C"
{
#ifndef UINT64_C
#define UINT64_C(val) val##ULL
#endif
#include <libswscale/swscale.h>
}

enum
{
	wxID_PAUSE = wxID_HIGHEST + 1,
	wxID_FRAMEADVANCE,
	wxID_SUBFRAMEADVANCE,
	wxID_NEXTPOLL,
	wxID_AUDIO_ENABLED,
	wxID_SAVE_STATE,
	wxID_SAVE_MOVIE,
	wxID_SAVE_SUBTITLES,
	wxID_LOAD_STATE,
	wxID_LOAD_MOVIE,
	wxID_RUN_SCRIPT,
	wxID_RUN_LUA,
	wxID_RESET_LUA,
	wxID_EVAL_LUA,
	wxID_SAVE_SCREENSHOT,
	wxID_READONLY_MODE,
	wxID_EDIT_AUTHORS,
	wxID_AUTOHOLD,
	wxID_EDIT_MEMORYWATCH,
	wxID_SAVE_MEMORYWATCH,
	wxID_LOAD_MEMORYWATCH,
	wxID_EDIT_SUBTITLES,
	wxID_EDIT_VSUBTITLES,
	wxID_DUMP_FIRST,
	wxID_DUMP_LAST = wxID_DUMP_FIRST + 1023,
	wxID_REWIND_MOVIE,
	wxID_MEMORY_SEARCH,
	wxID_CANCEL_SAVES,
	wxID_SHOW_STATUS,
	wxID_SET_SPEED,
	wxID_SPEED_5,
	wxID_SPEED_10,
	wxID_SPEED_17,
	wxID_SPEED_20,
	wxID_SPEED_25,
	wxID_SPEED_33,
	wxID_SPEED_50,
	wxID_SPEED_100,
	wxID_SPEED_150,
	wxID_SPEED_200,
	wxID_SPEED_300,
	wxID_SPEED_500,
	wxID_SPEED_1000,
	wxID_SPEED_TURBO,
	wxID_LOAD_LIBRARY,
	wxID_RELOAD_ROM_IMAGE,
	wxID_LOAD_ROM_IMAGE_FIRST,
	wxID_LOAD_ROM_IMAGE_LAST = wxID_LOAD_ROM_IMAGE_FIRST + 1023,
	wxID_NEW_MOVIE,
	wxID_SHOW_MESSAGES,
	wxID_DEDICATED_MEMORY_WATCH,
	wxID_RMOVIE_FIRST,
	wxID_RMOVIE_LAST = wxID_RMOVIE_FIRST + 16,
	wxID_RROM_FIRST,
	wxID_RROM_LAST = wxID_RROM_FIRST + 16,
	wxID_CONFLICTRESOLUTION,
	wxID_VUDISPLAY,
	wxID_MOVIE_EDIT,
	wxID_TASINPUT,
	wxID_NEW_PROJECT,
	wxID_CLOSE_PROJECT,
	wxID_CLOSE_ROM,
	wxID_EDIT_MACROS,
	wxID_ENTER_FULLSCREEN,
	wxID_ACTIONS_FIRST,
	wxID_ACTIONS_LAST = wxID_ACTIONS_FIRST + 256,
	wxID_SETTINGS_FIRST,
	wxID_SETTINGS_LAST = wxID_SETTINGS_FIRST + 256,
	wxID_HEXEDITOR,
	wxID_MULTITRACK,
	wxID_CHDIR,
	wxID_RLUA_FIRST,
	wxID_RLUA_LAST = wxID_RLUA_FIRST + 16,
	wxID_UPLOAD_FIRST,
	wxID_UPLOAD_LAST = wxID_UPLOAD_FIRST + 256,
	wxID_DOWNLOAD,
	wxID_TRACELOG_FIRST,
	wxID_TRACELOG_LAST = wxID_TRACELOG_FIRST + 256,
	wxID_PLUGIN_MANAGER,
	wxID_BRANCH_FIRST,
	wxID_BRANCH_LAST = wxID_BRANCH_FIRST + 10240,
	wxID_PROJECT_FIRST,
	wxID_PROJECT_LAST = wxID_PROJECT_FIRST + 17,
	wxID_DISASSEMBLER,
	wxID_START_R16M,
	wxID_END_R16M,
};


double video_scale_factor = 1.0;
int scaling_flags = SWS_POINT;
bool arcorrect_enabled = false;
bool hflip_enabled = false;
bool vflip_enabled = false;
bool rotate_enabled = false;

namespace
{
	std::string last_volume = "0dB";
	std::string last_volume_record = "0dB";
	std::string last_volume_voice = "0dB";
	unsigned char* screen_buffer;
	struct SwsContext* sws_ctx;
	uint32_t* rotate_buffer;
	uint32_t old_width;
	uint32_t old_height;
	int old_flags = SWS_POINT;
	bool old_hflip = false;
	bool old_vflip = false;
	bool old_rotate = false;
	bool main_window_dirty;
	bool is_fs = false;
	bool hashing_in_progress = false;
	uint64_t hashing_left = 0;
	uint64_t hashing_total = 0;
	int64_t last_update = 0;
	threads::thread* emulation_thread;
	bool status_updated = false;
	bool becoming_fullscreen = false;
	wxSize current_resolution;

	settingvar::variable<settingvar::model_bool<settingvar::yes_no>> background_audio(*lsnes_instance.settings,
		"background-audio", "GUI‣Enable background audio", true);

	class _status_timer : public wxTimer
	{
	public:
		_status_timer()
		{
			Start(50);
		}
		void Notify()
		{
			if(status_updated) {
				status_updated = false;
				if(main_window) main_window->update_statusbar();
			}
		}
	};

	void cleanup_dead_download_timers();
	class _focus_timer : public wxTimer
	{
	public:
		_focus_timer()
		{
			was_focused = (wxWindow::FindFocus() != NULL);
			was_enabled = platform::is_sound_enabled();
			Start(500);
		}
		void Notify()
		{
			CHECK_UI_THREAD;
			bool is_focused = (wxWindow::FindFocus() != NULL);
			if(is_focused && !was_focused) {
				//Gained focus.
				if(!background_audio)
					platform::sound_enable(was_enabled);
			} else if(!is_focused && was_focused) {
				//Lost focus.
				was_enabled = platform::is_sound_enabled();
				if(!background_audio)
					platform::sound_enable(false);
			}
			was_focused = is_focused;
			cleanup_dead_download_timers();
		}
	private:
		bool was_focused;
		bool was_enabled;
	};

	class download_timer;
	std::list<download_timer*> download_timer_gc_queue;

	class download_timer : public wxTimer
	{
	public:
		download_timer(wxwin_mainwindow* main, emulator_instance& _inst)
			: inst(_inst)
		{
			w = main;
			Start(50);
		}
		void Notify()
		{
			CHECK_UI_THREAD;
			if(!w->download_in_progress) {
				//Received a call with download finish already done. Ignore.
				return;
			}
			if(w->download_in_progress->finished) {
				w->update_statusbar();
				auto old = w->download_in_progress;
				w->download_in_progress = NULL;
				if(old->errormsg != "") {
					show_message_ok(w, "Error downloading movie", old->errormsg,
						wxICON_EXCLAMATION);
				} else {
					inst.iqueue->queue(CLOADSAVE::ldm.name, "$MEMORY:wxwidgets_download_tmp");
				}
				delete old;
				try { download_timer_gc_queue.push_back(this); } catch(...) { Stop(); }
			} else {
				w->update_statusbar();
			}
		}
	private:
		emulator_instance& inst;
		wxwin_mainwindow* w;
	};

	void cleanup_dead_download_timers()
	{
		for(auto i : download_timer_gc_queue) {
			i->Stop();
			delete i;
		}
		download_timer_gc_queue.clear();
	}

	void hash_callback(uint64_t left, uint64_t total)
	{
		wxwin_mainwindow* mwin = main_window;
		if(left == 0xFFFFFFFFFFFFFFFFULL) {
			hashing_in_progress = false;
			runuifun([mwin]() { if(mwin) mwin->notify_update_status(); });
			last_update = framerate_regulator::get_utime() - 2000000;
			return;
		}
		hashing_in_progress = true;
		hashing_left = left;
		hashing_total = total;
		int64_t this_update = framerate_regulator::get_utime();
		if(this_update < last_update - 1000000 || this_update > last_update + 1000000) {
			runuifun([mwin]() { if(mwin) mwin->notify_update_status(); });
			last_update = this_update;
		}
	}

	std::pair<std::string, std::string> lsplit(std::string l)
	{
		for(unsigned i = 0; i < l.length() - 3; i++)
			if((uint8_t)l[i] == 0xE2 && (uint8_t)l[i + 1] == 0x80 && (uint8_t)l[i + 2] == 0xA3)
				return std::make_pair(l.substr(0, i), l.substr(i + 3));
		return std::make_pair("", l);
	}

	recentfiles::multirom loadreq_to_multirom(const romload_request& req)
	{
		recentfiles::multirom r;
		r.packfile = req.packfile;
		r.singlefile = req.singlefile;
		r.core = req.core;
		r.system = req.system;
		r.region = req.region;
		for(unsigned i = 0; i < ROM_SLOT_COUNT; i++)
			if(req.files[i] != "") {
				r.files.resize(i + 1);
				r.files[i] = req.files[i];
			}
		return r;
	}

	class system_menu : public wxMenu
	{
	public:
		system_menu(wxWindow* win, emulator_instance& _inst);
		~system_menu();
		void on_select(wxCommandEvent& e);
		void update(bool light);
	private:
		emulator_instance& inst;
		wxWindow* pwin;
		void insert_pass(int id, const std::string& label);
		void insert_act(unsigned id, const std::string& label, bool dots, bool check);
		wxMenu* lookup_menu(const std::string& key);
		wxMenuItem* sep;
		std::map<int, unsigned> action_by_id;
		std::map<unsigned, wxMenuItem*> item_by_action;
		std::map<wxMenuItem*, wxMenu*> menu_by_item;
		std::map<std::string, wxMenu*> submenu_by_name;
		std::map<std::string, wxMenuItem*> submenui_by_name;
		std::set<unsigned> toggles;
		int next_id;
	};

	wxMenu* system_menu::lookup_menu(const std::string& key)
	{
		CHECK_UI_THREAD;
		if(key == "")
			return this;
		if(submenu_by_name.count(key))
			return submenu_by_name[key];
		//Not found, create.
		if(!sep)
			sep = AppendSeparator();
		auto p = lsplit(key);
		wxMenu* into = lookup_menu(p.first);
		submenu_by_name[key] = new wxMenu();
		submenui_by_name[key] = into->AppendSubMenu(submenu_by_name[key], towxstring(p.second));
		menu_by_item[submenui_by_name[key]] = into;
		return submenu_by_name[key];
	}

	void system_menu::insert_act(unsigned id, const std::string& label, bool dots, bool check)
	{
		CHECK_UI_THREAD;
		if(!sep)
			sep = AppendSeparator();

		auto p = lsplit(label);
		wxMenu* into = lookup_menu(p.first);

		action_by_id[next_id] = id;
		std::string use_label = p.second + (dots ? "..." : "");
		if(check) {
			item_by_action[id] = into->AppendCheckItem(next_id, towxstring(use_label));
			toggles.insert(id);
		} else
			item_by_action[id] = into->Append(next_id, towxstring(use_label));
		menu_by_item[item_by_action[id]] = into;
		pwin->Connect(next_id++, wxEVT_COMMAND_MENU_SELECTED,
			wxCommandEventHandler(system_menu::on_select), NULL, this);
	}

	void system_menu::insert_pass(int id, const std::string& label)
	{
		CHECK_UI_THREAD;
		pwin->Connect(id, wxEVT_COMMAND_MENU_SELECTED,
			wxCommandEventHandler(wxwin_mainwindow::handle_menu_click), NULL, pwin);
		Append(id, towxstring(label));
	}

	system_menu::system_menu(wxWindow* win, emulator_instance& _inst)
		: inst(_inst)
	{
		CHECK_UI_THREAD;
		pwin = win;
		insert_pass(wxID_PAUSE, "Pause/Unpause");
		insert_pass(wxID_FRAMEADVANCE, "Step frame");
		insert_pass(wxID_SUBFRAMEADVANCE, "Step subframe");
		insert_pass(wxID_NEXTPOLL, "Step poll");
		sep = NULL;
	}

	system_menu::~system_menu()
	{
	}

	void system_menu::on_select(wxCommandEvent& e)
	{
		CHECK_UI_THREAD;
		if(!action_by_id.count(e.GetId()))
			return;
		unsigned act_id = action_by_id[e.GetId()];
		const interface_action* act = NULL;
		for(auto i : inst.rom->get_actions())
			if(i->id == act_id) {
				act = i;
				break;
			}
		if(!act)
			return;
		try {
			auto p = prompt_action_params(pwin, act->get_title(), act->params);
			inst.iqueue->run([this, act_id,p]() { this->inst.rom->execute_action(act_id, p); });
		} catch(canceled_exception& e) {
		} catch(std::bad_alloc& e) {
			OOM_panic();
		}
	}

	void system_menu::update(bool light)
	{
		CHECK_UI_THREAD;
		if(!light) {
			next_id = wxID_ACTIONS_FIRST;
			if(sep) {
				Destroy(sep);
				sep = NULL;
			}
			for(auto i = item_by_action.begin(); i != item_by_action.end(); i++)
				menu_by_item[i->second]->Destroy(i->second);
			for(auto i = submenui_by_name.rbegin(); i != submenui_by_name.rend(); i++)
				menu_by_item[i->second]->Destroy(i->second);
			action_by_id.clear();
			item_by_action.clear();
			menu_by_item.clear();
			submenu_by_name.clear();
			submenui_by_name.clear();
			toggles.clear();

			for(auto i : inst.rom->get_actions())
				insert_act(i->id, i->get_title(), !i->params.empty(), i->is_toggle());
		}
		for(auto i : item_by_action)
			i.second->Enable(inst.rom->action_flags(i.first) & 1);
		for(auto i : toggles)
			item_by_action[i]->Check(inst.rom->action_flags(i) & 2);
	}

	std::string munge_name(const std::string& orig)
	{
		std::string newname;
		regex_results r;
		if(r = regex("(.*)\\(([0-9]+)\\)", newname)) {
			uint64_t sequence;
			try {
				sequence = parse_value<uint64_t>(r[2]);
				newname = (stringfmt() << r[1] << "(" << sequence + 1 << ")").str();
			} catch(...) {
				newname = newname + "(2)";
			}
		} else {
			newname = newname + "(2)";
		}
		return newname;
	}

	void handle_watch_load(emulator_instance& inst, std::map<std::string, std::string>& new_watches,
		std::set<std::string>& old_watches)
	{
		auto proj = inst.project->get();
		if(proj) {
			for(auto i : new_watches) {
				std::string name = i.first;
				while(true) {
					if(!old_watches.count(name)) {
						try {
							if(name != "" && i.second != "")
								inst.mwatch->set(name, i.second);
						} catch(std::exception& e) {
							messages << "Can't set memory watch '" << name << "': "
								<< e.what() << std::endl;
						}
						break;
					} else if(inst.mwatch->get_string(name) == i.second)
						break;
					else
						name = munge_name(name);
				}
			}
		} else {
			for(auto i : new_watches)
				try {
					if(i.first != "" && i.second != "")
						inst.mwatch->set(i.first, i.second);
				} catch(std::exception& e) {
					messages << "Can't set memory watch '" << i.first << "': "
						<< e.what() << std::endl;
				}
			for(auto i : old_watches)
				if(!new_watches.count(i))
					try {
						inst.mwatch->clear(i);
					} catch(std::exception& e) {
						messages << "Can't clear memory watch '" << i << "': "
							<< e.what() << std::endl;
					}
		}
	}

	std::string get_default_screenshot_name(emulator_instance& inst)
	{
		auto p = inst.project->get();
		if(!p)
			return "";
		else {
			auto files = directory::enumerate(p->directory, ".*-[0-9]+\\.png");
			std::set<std::string> numbers;
			for(auto i : files) {
				size_t split;
#ifdef FUCKED_SYSTEM
				split = i.find_last_of("\\/");
#else
				split = i.find_last_of("/");
#endif
				std::string name = i;
				if(split < name.length())
					name = name.substr(split + 1);
				regex_results r = regex("(.*)-([0-9]+)\\.png", name);
				if(r[1] != p->prefix)
					continue;
				numbers.insert(r[2]);
			}
			for(uint64_t i = 1;; i++) {
				std::string candidate = (stringfmt() << i).str();
				if(!numbers.count(candidate))
					return p->prefix + "-" + candidate + ".png";
			}
		}
	}

	std::string project_prefixname(emulator_instance& inst, const std::string ext)
	{
		auto p = inst.project->get();
		if(!p)
			return "";
		else
			return p->prefix + "." + ext;

	}

	void recent_rom_selected(emulator_instance& inst, const recentfiles::multirom& file)
	{
		romload_request req;
		req.packfile = file.packfile;
		req.singlefile = file.singlefile;
		req.core = file.core;
		req.system = file.system;
		req.region = file.region;
		for(unsigned i = 0; i < file.files.size() && i < ROM_SLOT_COUNT; i++)
			req.files[i] = file.files[i];
		inst.iqueue->run_async([req]() {
			CORE().command->invoke("unpause-emulator");
			load_new_rom(req);
		}, [](std::exception& e) {});
	}

	void recent_movie_selected(emulator_instance& inst, const recentfiles::path& file)
	{
		inst.iqueue->queue(CLOADSAVE::ldsm.name, file.get_path());
	}

	void recent_script_selected(emulator_instance& inst, const recentfiles::path& file)
	{
		inst.iqueue->queue(CLUA::run.name, file.get_path());
	}

	wxString getname(emulator_instance& inst)
	{
		std::string windowname = "lsnes rr" + lsnes_version + " [";
		auto p = inst.project->get();
		if(p)
			windowname = windowname + p->name;
		else
			windowname = windowname + inst.rom->get_core_identifier();
		windowname = windowname + "]";
		return towxstring(windowname);
	}

	struct emu_args
	{
		emulator_instance* inst;
		struct loaded_rom rom;
		struct moviefile* initial;
		bool load_has_to_succeed;
	};

	void* emulator_main(void* _args)
	{
		struct emu_args* args = reinterpret_cast<struct emu_args*>(_args);
		auto& inst = *args->inst;
		try {
			*inst.rom = args->rom;
			messages << "Using core: " << inst.rom->get_core_identifier() << std::endl;
			struct moviefile* movie = args->initial;
			bool has_to_succeed = args->load_has_to_succeed;
			platform::flush_command_queue();
			main_loop(*inst.rom, *movie, has_to_succeed);
			signal_program_exit();
		} catch(std::bad_alloc& e) {
			OOM_panic();
		} catch(std::exception& e) {
			messages << "FATAL: " << e.what() << std::endl;
			platform::fatal_error();
		}
		delete args;
		return NULL;
	}

	void join_emulator_thread()
	{
		emulation_thread->join();
	}

	bool is_readonly_mode(emulator_instance& inst)
	{
		bool ret;
		inst.iqueue->run([&ret]() {
			ret = *CORE().mlogic ? CORE().mlogic->get_movie().readonly_mode() : false;
		});
		return ret;
	}

	void set_speed(emulator_instance& inst, double target)
	{
		if(target < 0)
			inst.framerate->set_speed_multiplier(std::numeric_limits<double>::infinity());
		else
			inst.framerate->set_speed_multiplier(target / 100);
	}

	void update_preferences()
	{
		preferred_core.clear();
		for(auto i : core_type::get_core_types()) {
			std::string val = i->get_hname() + " / " + i->get_core_identifier();
			for(auto j : i->get_extensions()) {
				std::string key = "ext:" + j;
				if(core_selections.count(key) && core_selections[key] == val)
					preferred_core[key] = i;
			}
			std::string key2 = "type:" + i->get_iname();
			if(core_selections.count(key2) && core_selections[key2] == val)
				preferred_core[key2] = i;
		}
	}

	bool is_lsnes_movie(const std::string& filename)
	{
		std::istream* s = NULL;
		try {
			bool ans = false;
			s = &zip::openrel(filename, "");
			char buf[6] = {0};
			s->read(buf, 5);
			if(*s && !strcmp(buf, "lsmv\x1A"))
				ans = true;
			delete s;
			if(ans) return true;
		} catch(...) {
			delete s;
		}
		try {
			zip::reader r(filename);
			std::istream& s = r["systemid"];
			std::string s2;
			std::getline(s, s2);
			delete &s;
			istrip_CR(s2);
			return (s2 == "lsnes-rr1");
		} catch(...) {
			return false;
		}
	}

	class loadfile : public wxFileDropTarget
	{
	public:
		loadfile(wxwin_mainwindow* win, emulator_instance& _inst) : inst(_inst), pwin(win) {};
		bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames)
		{
			CHECK_UI_THREAD;
			bool ret = false;
			if(filenames.Count() == 2) {
				std::string a = tostdstring(filenames[0]);
				std::string b = tostdstring(filenames[1]);
				bool amov = is_lsnes_movie(a);
				bool bmov = is_lsnes_movie(b);
				if(amov == bmov)
					return false;
				if(amov) std::swap(a, b);
				inst.iqueue->run_async([a, b]() {
					CORE().command->invoke("unpause-emulator");
					romload_request req;
					req.packfile = a;
					load_new_rom(req);
					CORE().command->invoke(CLOADSAVE::ldsm.name, b);
				}, [](std::exception& e) {});
				ret = true;
			}
			if(filenames.Count() == 1) {
				std::string a = tostdstring(filenames[0]);
				bool amov = is_lsnes_movie(a);
				if(amov) {
					inst.iqueue->queue(CLOADSAVE::ldsm.name, a);
					pwin->recent_movies->add(a);
					ret = true;
				} else {
					romload_request req;
					req.packfile = a;
					inst.iqueue->run_async([req]() {
						CORE().command->invoke("unpause-emulator");
						load_new_rom(req);
					}, [](std::exception& e) {});
					pwin->recent_roms->add(loadreq_to_multirom(req));
					ret = true;
				}
			}
			return ret;
		}
		emulator_instance& inst;
		wxwin_mainwindow* pwin;
	};
}

void boot_emulator(emulator_instance& inst, loaded_rom& rom, moviefile& movie, bool fscreen)
{
	CHECK_UI_THREAD;
	update_preferences();
	try {
		struct emu_args* a = new emu_args;
		a->rom = rom;
		a->initial = &movie;
		a->load_has_to_succeed = false;
		a->inst = &inst;
		modal_pause_holder hld;
		emulation_thread = new threads::thread(emulator_main, a);
		main_window = new wxwin_mainwindow(inst, fscreen);
		main_window->Show();
	} catch(std::bad_alloc& e) {
		OOM_panic();
	}
}

wxwin_mainwindow::panel::panel(wxWindow* win, emulator_instance& _inst)
	: wxPanel(win, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS), inst(_inst)
{
	CHECK_UI_THREAD;
	this->Connect(wxEVT_PAINT, wxPaintEventHandler(panel::on_paint), NULL, this);
	this->Connect(wxEVT_ERASE_BACKGROUND, wxEraseEventHandler(panel::on_erase), NULL, this);
	this->Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(panel::on_keyboard_down), NULL, this);
	this->Connect(wxEVT_KEY_UP, wxKeyEventHandler(panel::on_keyboard_up), NULL, this);
	this->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_MIDDLE_DOWN, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_MIDDLE_UP, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_RIGHT_DOWN, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_RIGHT_UP, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_MOTION, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_ENTER_WINDOW, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(panel::on_mouse), NULL, this);
	SetMinSize(wxSize(512, 448));
}

void wxwin_mainwindow::menu_start(wxString name)
{
	CHECK_UI_THREAD;
	while(!upper.empty())
		upper.pop();
	current_menu = new wxMenu();
	menubar->Append(current_menu, name);
}

void wxwin_mainwindow::menu_special(wxString name, wxMenu* menu)
{
	CHECK_UI_THREAD;
	while(!upper.empty())
		upper.pop();
	menubar->Append(menu, name);
	current_menu = NULL;
}

wxMenuItem* wxwin_mainwindow::menu_special_sub(wxString name, wxMenu* menu)
{
	CHECK_UI_THREAD;
	return current_menu->AppendSubMenu(menu, name);
}

void wxwin_mainwindow::menu_entry(int id, wxString name)
{
	CHECK_UI_THREAD;
	current_menu->Append(id, name);
	Connect(id, wxEVT_COMMAND_MENU_SELECTED,
		wxCommandEventHandler(wxwin_mainwindow::wxwin_mainwindow::handle_menu_click), NULL, this);
}

void wxwin_mainwindow::menu_entry_check(int id, wxString name)
{
	CHECK_UI_THREAD;
	checkitems[id] = current_menu->AppendCheckItem(id, name);
	Connect(id, wxEVT_COMMAND_MENU_SELECTED,
		wxCommandEventHandler(wxwin_mainwindow::wxwin_mainwindow::handle_menu_click), NULL, this);
}

void wxwin_mainwindow::menu_start_sub(wxString name)
{
	CHECK_UI_THREAD;
	wxMenu* old = current_menu;
	upper.push(current_menu);
	current_menu = new wxMenu();
	old->AppendSubMenu(current_menu, name);
}

void wxwin_mainwindow::menu_end_sub()
{
	current_menu = upper.top();
	upper.pop();
}

bool wxwin_mainwindow::menu_ischecked(int id)
{
	CHECK_UI_THREAD;
	if(checkitems.count(id))
		return checkitems[id]->IsChecked();
	else
		return false;
}

void wxwin_mainwindow::menu_check(int id, bool newstate)
{
	CHECK_UI_THREAD;
	if(checkitems.count(id))
		return checkitems[id]->Check(newstate);
	else
		return;
}

void wxwin_mainwindow::menu_enable(int id, bool newstate)
{
	CHECK_UI_THREAD;
	auto item = menubar->FindItem(id);
	if(!item)
		return;
	item->Enable(newstate);
}

void wxwin_mainwindow::menu_separator()
{
	CHECK_UI_THREAD;
	current_menu->AppendSeparator();
}

void wxwin_mainwindow::panel::request_paint()
{
	CHECK_UI_THREAD;
	Refresh();
}

std::pair<double, double> calc_scale_factors(double factor, bool ar, double par)
{
	if(!ar)
		return std::make_pair(factor, factor);
	else if(par < 1) {
		//Too wide, make taller.
		return std::make_pair(factor, factor / par);
	} else {
		//Too narrow, make wider.
		return std::make_pair(factor * par, factor);
	}
}

void wxwin_mainwindow::panel::on_paint(wxPaintEvent& e)
{
	CHECK_UI_THREAD;
	if(wx_escape_count >= 3 && is_fs) {
		//Leave fullscreen mode.
		main_window->enter_or_leave_fullscreen(false);
	}
	inst.fbuf->render_framebuffer();
	uint8_t* srcp[1];
	int srcs[1];
	uint8_t* dstp[1];
	int dsts[1];
	wxPaintDC dc(this);
	uint32_t tw, th;
	bool aux = hflip_enabled || vflip_enabled || rotate_enabled;
	auto sfactors = calc_scale_factors(video_scale_factor, arcorrect_enabled, inst.rom->get_PAR());
	if(rotate_enabled) {
		tw = inst.fbuf->main_screen.get_height() * sfactors.second + 0.5;
		th = inst.fbuf->main_screen.get_width() * sfactors.first + 0.5;
	} else {
		tw = inst.fbuf->main_screen.get_width() * sfactors.first + 0.5;
		th = inst.fbuf->main_screen.get_height() * sfactors.second + 0.5;
	}
	if(!tw || !th) {
		main_window_dirty = false;
		return;
	}
	//Scale this to fullscreen.
	unsigned dx = 0, dy = 0;
	if(is_fs) {
		wxSize screen = main_window->GetSize();

		double fss = min(1.0 * screen.GetWidth() / tw, 1.0 * screen.GetHeight() / th);
		tw *= fss;
		th *= fss;
		if((signed)tw < screen.GetWidth())
			dx = (screen.GetWidth() - tw) / 2;
		if((signed)th < screen.GetHeight())
			dy = (screen.GetHeight() - th) / 2;
		if(/*becoming_fullscreen && */current_resolution != screen) {
			//Force panel to fullscreen.
			SetSize(screen);
			Move(0, 0);
			current_resolution = screen;
			//becoming_fullscreen = false;
		}
		//Erase borders.
		signed dx2 = dx + tw;
		signed dy2 = dy + th;
		dc.SetBrush(*wxBLACK_BRUSH);
		dc.SetPen(*wxBLACK_PEN);
		//Erase the borders we don't draw.
		if(dx > 0) dc.DrawRectangle(0, 0, dx, screen.GetHeight());
		if(dy > 0) dc.DrawRectangle(0, 0, screen.GetWidth(), dy);
		if(dx2 < screen.GetWidth()) dc.DrawRectangle(dx2, 0, screen.GetWidth() - dx2, screen.GetHeight());
		if(dy2 < screen.GetHeight()) dc.DrawRectangle(0, dy2, screen.GetWidth(), screen.GetHeight() - dy2);
	}

	if(!screen_buffer || tw != old_width || th != old_height || scaling_flags != old_flags ||
		hflip_enabled != old_hflip || vflip_enabled != old_vflip || rotate_enabled != old_rotate) {
		if(screen_buffer) {
			delete[] screen_buffer;
			screen_buffer = NULL;
		}
		if(rotate_buffer) {
			delete[] rotate_buffer;
			rotate_buffer = NULL;
		}
		old_height = th;
		old_width = tw;
		old_flags = scaling_flags;
		old_hflip = hflip_enabled;
		old_vflip = vflip_enabled;
		old_rotate = rotate_enabled;
		uint32_t w = inst.fbuf->main_screen.get_width();
		uint32_t h = inst.fbuf->main_screen.get_height();
		if(w && h)
			sws_ctx = sws_getCachedContext(sws_ctx, rotate_enabled ? h : w, rotate_enabled ? w : h,
				AV_PIX_FMT_RGBA, tw, th, AV_PIX_FMT_BGR24, scaling_flags, NULL, NULL, NULL);
		tw = max(tw, static_cast<uint32_t>(128));
		th = max(th, static_cast<uint32_t>(112));
		screen_buffer = new unsigned char[tw * th * 3 + 64];
		if(aux)
			rotate_buffer = new uint32_t[inst.fbuf->main_screen.get_width() *
				inst.fbuf->main_screen.get_height()];
		if(!is_fs) {
			//This is not preformed in fullscreen mode.
			SetMinSize(wxSize(tw, th));
			signal_resize_needed();
		}
	}
	if(aux) {
		//Hflip, Vflip or rotate active.
		size_t width = inst.fbuf->main_screen.get_width();
		size_t height = inst.fbuf->main_screen.get_height();
		size_t width1 = width - 1;
		size_t height1 = height - 1;
		size_t stride = inst.fbuf->main_screen.rowptr(1) - inst.fbuf->main_screen.rowptr(0);
		uint32_t* pixels = inst.fbuf->main_screen.rowptr(0);
		if(rotate_enabled) {
			for(unsigned y = 0; y < height; y++) {
				uint32_t* pixels2 = pixels + (vflip_enabled ? (height1 - y) : y) * stride;
				uint32_t* dpixels = rotate_buffer + (height1 - y);
				if(hflip_enabled)
					for(unsigned x = 0; x < width; x++)
						dpixels[x * height] = pixels2[width1 - x];
				else
					for(unsigned x = 0; x < width; x++)
						dpixels[x * height] = pixels2[x];
			}
		} else {
			for(unsigned y = 0; y < height; y++) {
				uint32_t* pixels2 = pixels + (vflip_enabled ? (height1 - y) : y) * stride;
				uint32_t* dpixels = rotate_buffer + y * width;
				if(hflip_enabled)
					for(unsigned x = 0; x < width; x++)
						dpixels[x] = pixels2[width1 - x];
				else
					for(unsigned x = 0; x < width; x++)
						dpixels[x] = pixels2[x];
			}
		}
	}
	if(aux)
		srcs[0] = 4 * (rotate_enabled ? inst.fbuf->main_screen.get_height() :
			inst.fbuf->main_screen.get_width());
	else
		srcs[0] = 4 * inst.fbuf->main_screen.get_stride();
	dsts[0] = 3 * tw;
	srcp[0] = reinterpret_cast<unsigned char*>(aux ? rotate_buffer : inst.fbuf->main_screen.rowptr(0));
	dstp[0] = screen_buffer;
	memset(screen_buffer, 0, tw * th * 3);
	if(inst.fbuf->main_screen.get_width() && inst.fbuf->main_screen.get_height())
		sws_scale(sws_ctx, srcp, srcs, 0, rotate_enabled ? inst.fbuf->main_screen.get_width() :
			inst.fbuf->main_screen.get_height(),
		dstp, dsts);
	wxBitmap bmp(wxImage(tw, th, screen_buffer, true));
	dc.DrawBitmap(bmp, dx, dy, false);
	main_window_dirty = false;
	main_window->update_statusbar();
}

void wxwin_mainwindow::panel::on_erase(wxEraseEvent& e)
{
	//Blank.
}

void wxwin_mainwindow::panel::on_keyboard_down(wxKeyEvent& e)
{
	CHECK_UI_THREAD;
	handle_wx_keyboard(inst, e, true);
}

void wxwin_mainwindow::panel::on_keyboard_up(wxKeyEvent& e)
{
	CHECK_UI_THREAD;
	handle_wx_keyboard(inst, e, false);
	if(wx_escape_count >= 3 && is_fs) {
		//Force leave fullscreen mode.
		main_window->enter_or_leave_fullscreen(false);
	}
}

void wxwin_mainwindow::panel::on_mouse(wxMouseEvent& e)
{
	CHECK_UI_THREAD;
	handle_wx_mouse(inst, e);
}

wxwin_mainwindow::wxwin_mainwindow(emulator_instance& _inst, bool fscreen)
	: wxFrame(NULL, wxID_ANY, getname(_inst), wxDefaultPosition, wxSize(-1, -1),
		wxMINIMIZE_BOX | wxSYSTEM_MENU | wxCAPTION | wxCLIP_CHILDREN | wxCLOSE_BOX), inst(_inst)
{
	CHECK_UI_THREAD;
	download_in_progress = NULL;
	Centre();
	mwindow = NULL;
	toplevel = new wxFlexGridSizer(1, 2, 0, 0);
	toplevel->Add(gpanel = new panel(this, inst), 1, wxGROW);
	toplevel->Add(spanel = new wxwin_status::panel(this, inst, gpanel, 20), 1, wxGROW);
	spanel_shown = true;
        if(getenv("LSNES_HIDE_STATUSPANEL")) {
            spanel_shown = false;
            spanel->Hide();
        }
	toplevel->SetSizeHints(this);
	SetSizer(toplevel);
	Fit();
	gpanel->SetFocus();
	Connect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(wxwin_mainwindow::on_close));
	SetMenuBar(menubar = new wxMenuBar);
	SetStatusBar(statusbar = new wxStatusBar(this));

	menu_start(wxT("File"));
	menu_start_sub(wxT("New"));
	menu_entry(wxID_NEW_MOVIE, wxT("Movie..."));
	menu_entry(wxID_NEW_PROJECT, wxT("Project..."));
	menu_end_sub();
	menu_start_sub(wxT("Load"));
	menu_entry(wxID_LOAD_STATE, wxT("State..."));
	menu_entry(wxID_LOAD_MOVIE, wxT("Movie..."));
	menu_entry(wxID_DOWNLOAD, wxT("Download movie..."));
	if(loadlib::library::name() != "") {
		menu_separator();
		menu_entry(wxID_LOAD_LIBRARY, towxstring(std::string("Load ") + loadlib::library::name()));
		menu_entry(wxID_PLUGIN_MANAGER, towxstring("Plugin manager"));
	}
	menu_separator();
	menu_entry(wxID_RELOAD_ROM_IMAGE, wxT("Reload ROM"));
	menu_entry(wxID_LOAD_ROM_IMAGE_FIRST, wxT("ROM..."));
	menu_special_sub(wxT("Multifile ROM"), loadroms = new loadrom_menu(this, wxID_LOAD_ROM_IMAGE_FIRST + 1,
		wxID_LOAD_ROM_IMAGE_LAST, [this](core_type* t) { this->do_load_rom_image(t); }));
	menu_special_sub(wxT("Project"), projects = new projects_menu(this, inst, wxID_PROJECT_FIRST,
		wxID_PROJECT_LAST, get_config_path() + "/recent-projects.txt", [this](const std::string& id) {
		this->project_selected(id); }));
	menu_separator();
	menu_special_sub(wxT("Recent ROMs"), recent_roms = new recent_menu<recentfiles::multirom>(this, inst,
		wxID_RROM_FIRST, wxID_RROM_LAST, get_config_path() + "/recent-roms.txt", recent_rom_selected));
	menu_special_sub(wxT("Recent Movies"), recent_movies = new recent_menu<recentfiles::path>(this, inst,
		wxID_RMOVIE_FIRST, wxID_RMOVIE_LAST, get_config_path() + "/recent-movies.txt",
		recent_movie_selected));
	menu_special_sub(wxT("Recent Lua scripts"), recent_scripts = new recent_menu<recentfiles::path>(this, inst,
		wxID_RLUA_FIRST, wxID_RLUA_LAST, get_config_path() + "/recent-scripts.txt",
		recent_script_selected));
	menu_separator();
	menu_entry(wxID_CONFLICTRESOLUTION, wxT("Conflict resolution"));
	menu_separator();
	branches_menu* brlist;
	auto brlist_item = menu_special_sub(wxT("Branches"), brlist = new branches_menu(this, inst,
		wxID_BRANCH_FIRST, wxID_BRANCH_LAST));
	brlist->set_disabler([brlist_item](bool enabled) { brlist_item->Enable(enabled); });
	brlist->update();
	menu_end_sub();
	menu_start_sub(wxT("Save"));
	menu_entry(wxID_SAVE_STATE, wxT("State..."));
	menu_entry(wxID_SAVE_MOVIE, wxT("Movie..."));
	menu_entry(wxID_SAVE_SCREENSHOT, wxT("Screenshot..."));
	menu_entry(wxID_SAVE_SUBTITLES, wxT("Subtitles..."));
	menu_entry(wxID_CANCEL_SAVES, wxT("Cancel pending saves"));
	menu_separator();
	menu_entry(wxID_CHDIR, wxT("Change working directory..."));
	menu_separator();
	menu_special_sub(wxT("Upload"), new upload_menu(this, inst, wxID_UPLOAD_FIRST, wxID_UPLOAD_LAST));
	menu_end_sub();
	menu_start_sub(wxT("Close"));
	menu_entry(wxID_CLOSE_PROJECT, wxT("Project"));
	menu_entry(wxID_CLOSE_ROM, wxT("ROM"));
	menu_enable(wxID_CLOSE_PROJECT, inst.project->get() != NULL);
	menu_enable(wxID_CLOSE_ROM, inst.project->get() == NULL);
	menu_end_sub();
	menu_separator();
	menu_entry(wxID_EXIT, wxT("Quit"));

	menu_special(wxT("System"), reinterpret_cast<wxMenu*>(sysmenu = new system_menu(this, inst)));

	menu_start(wxT("Movie"));
	menu_entry_check(wxID_READONLY_MODE, wxT("Readonly mode"));
	menu_check(wxID_READONLY_MODE, is_readonly_mode(inst));
	menu_entry(wxID_EDIT_AUTHORS, wxT("Edit game name && authors..."));
	menu_entry(wxID_EDIT_SUBTITLES, wxT("Edit subtitles..."));
	menu_entry(wxID_EDIT_VSUBTITLES, wxT("Edit commantary track..."));
	menu_separator();
	menu_entry(wxID_REWIND_MOVIE, wxT("Rewind to start"));

	menu_start(wxT("Speed"));
	menu_entry(wxID_SPEED_5, wxT("1/20x"));
	menu_entry(wxID_SPEED_10, wxT("1/10x"));
	menu_entry(wxID_SPEED_17, wxT("1/6x"));
	menu_entry(wxID_SPEED_20, wxT("1/5x"));
	menu_entry(wxID_SPEED_25, wxT("1/4x"));
	menu_entry(wxID_SPEED_33, wxT("1/3x"));
	menu_entry(wxID_SPEED_50, wxT("1/2x"));
	menu_entry(wxID_SPEED_100, wxT("1x"));
	menu_entry(wxID_SPEED_150, wxT("1.5x"));
	menu_entry(wxID_SPEED_200, wxT("2x"));
	menu_entry(wxID_SPEED_300, wxT("3x"));
	menu_entry(wxID_SPEED_500, wxT("5x"));
	menu_entry(wxID_SPEED_1000, wxT("10x"));
	menu_entry(wxID_SPEED_TURBO, wxT("Turbo"));
	menu_entry(wxID_SET_SPEED, wxT("Set..."));

	menu_start(wxT("Tools"));
	menu_entry(wxID_RUN_SCRIPT, wxT("Run batch file..."));
	menu_separator();
	menu_entry(wxID_EVAL_LUA, wxT("Evaluate Lua statement..."));
	menu_entry(wxID_RUN_LUA, wxT("Run Lua script..."));
	menu_separator();
	menu_entry(wxID_RESET_LUA, wxT("Reset Lua VM"));
	menu_separator();
	menu_entry(wxID_AUTOHOLD, wxT("Autohold/Autofire..."));
	menu_entry(wxID_TASINPUT, wxT("TAS input plugin..."));
	menu_entry(wxID_MULTITRACK, wxT("Multitrack..."));
	menu_entry(wxID_EDIT_MACROS, wxT("Edit macros..."));
	menu_separator();
	menu_entry(wxID_EDIT_MEMORYWATCH, wxT("Edit memory watch..."));
	menu_separator();
	menu_entry(wxID_LOAD_MEMORYWATCH, wxT("Load memory watch..."));
	menu_entry(wxID_SAVE_MEMORYWATCH, wxT("Save memory watch..."));
	menu_separator();
	menu_entry(wxID_MEMORY_SEARCH, wxT("Memory Search..."));
	menu_entry(wxID_HEXEDITOR, wxT("Memory editor..."));
	tracelog_menu* trlog;
	auto trlog_item = menu_special_sub(wxT("Trace log"), trlog = new tracelog_menu(this, inst,
		wxID_TRACELOG_FIRST, wxID_TRACELOG_LAST));
	trlog->set_disabler([trlog_item](bool enabled) { trlog_item->Enable(enabled); });
	trlog->update();
	menu_entry(wxID_DISASSEMBLER, wxT("Disassembler..."));
	menu_separator();
	menu_entry(wxID_MOVIE_EDIT, wxT("Edit movie..."));
	menu_separator();
	menu_special_sub(wxT("Video Capture"), reinterpret_cast<dumper_menu*>(dmenu = new dumper_menu(this,
		inst, wxID_DUMP_FIRST, wxID_DUMP_LAST)));
	menu_separator();
	menu_start_sub(wxT("Movie dump"));
	menu_entry(wxID_START_R16M, wxT("Start r16m dump..."));
	menu_entry(wxID_END_R16M, wxT("End r16m dump"));
	menu_end_sub();

	menu_start(wxT("Configure"));
	menu_entry_check(wxID_SHOW_STATUS, wxT("Show status panel"));
	menu_check(wxID_SHOW_STATUS, true);
	menu_entry_check(wxID_DEDICATED_MEMORY_WATCH, wxT("Dedicated memory watch"));
	menu_entry(wxID_SHOW_MESSAGES, wxT("Show messages"));
	menu_special_sub(wxT("Settings"), new settings_menu(this, inst, wxID_SETTINGS_FIRST));
	if(audioapi_driver_initialized()) {
		menu_separator();
		menu_entry_check(wxID_AUDIO_ENABLED, wxT("Sounds enabled"));
		menu_entry(wxID_VUDISPLAY, wxT("VU display / sound controls"));
		menu_check(wxID_AUDIO_ENABLED, platform::is_sound_enabled());
	}
	menu_separator();
	menu_entry(wxID_ENTER_FULLSCREEN, wxT("Enter fullscreen mode"));

	menu_start(wxT("Help"));
	menu_entry(wxID_ABOUT, wxT("About..."));

	corechange.set(inst.dispatch->core_change, []() { signal_core_change(); });
	titlechange.set(inst.dispatch->title_change, []() { signal_core_change(); });
	newcore.set(notify_new_core, []() { update_preferences(); });
	unmuted.set(inst.dispatch->sound_unmute, [this](bool unmute) {
		runuifun([this, unmute]() { this->menu_check(wxID_AUDIO_ENABLED, unmute); });
	});
	modechange.set(inst.dispatch->mode_change, [this](bool readonly) {
		runuifun([this, readonly]() { this->menu_check(wxID_READONLY_MODE, readonly); });
	});
	gpanel->SetDropTarget(new loadfile(this, inst));
	spanel->SetDropTarget(new loadfile(this, inst));
	set_hasher_callback(hash_callback);
	reinterpret_cast<system_menu*>(sysmenu)->update(false);
	menubar->SetMenuLabel(1, towxstring(inst.rom->get_systemmenu_name()));
	focus_timer = new _focus_timer;
	status_timer = new _status_timer;
	if(fscreen) {
		wx_escape_count = 0;
		enter_or_leave_fullscreen(true);
	}
}

wxwin_mainwindow::~wxwin_mainwindow()
{
	CHECK_UI_THREAD;
	if(sws_ctx) sws_freeContext(sws_ctx);
	if(screen_buffer) delete[] screen_buffer;
	if(rotate_buffer) delete[] rotate_buffer;
	focus_timer->Stop();
	delete focus_timer;
	status_timer->Stop();
	delete status_timer;
}

void wxwin_mainwindow::request_paint()
{
	CHECK_UI_THREAD;
	gpanel->Refresh();
}

void wxwin_mainwindow::on_close(wxCloseEvent& e)
{
	CHECK_UI_THREAD;
	//Veto it for now, latter things will delete it.
	e.Veto();
	inst.iqueue->queue("quit-emulator");
}

void wxwin_mainwindow::notify_update() throw()
{
	CHECK_UI_THREAD;
	if(!main_window_dirty) {
		main_window_dirty = true;
		gpanel->Refresh();
	}
}

void wxwin_mainwindow::notify_resized() throw()
{
	CHECK_UI_THREAD;
	toplevel->Layout();
	toplevel->SetSizeHints(this);
	Fit();
}

void wxwin_mainwindow::notify_update_status() throw()
{
	CHECK_UI_THREAD;
	spanel->request_paint();
	if(mwindow)
		mwindow->notify_update();
	status_updated = true;
}

void wxwin_mainwindow::notify_exit() throw()
{
	CHECK_UI_THREAD;
	wxwidgets_exiting = true;
	join_emulator_thread();
	Destroy();
}

std::string read_variable_map(const std::map<std::string, std::u32string>& vars, const std::string& key)
{
	if(!vars.count(key))
		return "";
	return utf8::to8(vars.find(key)->second);
}

void wxwin_mainwindow::update_statusbar()
{
	CHECK_UI_THREAD;
	if(download_in_progress) {
		statusbar->SetStatusText(towxstring(download_in_progress->statusmsg()));
		return;
	}
	if(hashing_in_progress) {
		//TODO: Display this as a dialog.
		std::ostringstream s;
		s << "Hashing ROMs, approximately " << ((hashing_left + 524288) >> 20) << " of "
			<< ((hashing_total + 524288) >> 20) << "MB left...";
		statusbar->SetStatusText(towxstring(s.str()));
		return;
	}
	auto& vars = inst.status->get_read();
	if(!vars.valid) {
		inst.status->put_read();
		return;
	}
	try {
		std::ostringstream s;
		bool recording = (vars.mode == 'R');
		if(vars.movie_valid) {
			if(recording)
				s << "Frame: " << vars.curframe;
			else
				s << "Frame: " << vars.curframe << "/" << vars.length;
			s << "  Lag: " << vars.lag;
			if(vars.subframe == _lsnes_status::subframe_savepoint)
				s << "  Subframe: S";
			else if(vars.subframe == _lsnes_status::subframe_video)
				s << "  Subframe: V";
			else
				s << "  Subframe: " << vars.subframe;
		} else {
			s << "Frame: N/A  Lag: N/A  Subframe: N/A";
		}
		if(vars.saveslot_valid) {
			s << "  Slot: ";
			if(vars.branch_valid) s << utf8::to8(vars.branch) << "→";
			s << vars.saveslot;
			s << " [" << utf8::to8(vars.slotinfo) << "]";
		}
		s << "  Speed: " << vars.speed << "% ";
		if(vars.pause == _lsnes_status::pause_break)
			s << " Breakpoint";
		else if(vars.pause == _lsnes_status::pause_normal)
			s << " Paused";
		if(vars.dumping)
			s << " Dumping";
		if(vars.mode == 'C')
			s << " Corrupt";
		else if(vars.mode == 'R')
			s << " Recording";
		else if(vars.mode == 'P')
			s << " Playback";
		else if(vars.mode == 'F')
			s << " Finished";
		else
			s << " Unknown";
		if(vars.mbranch_valid)
			s << "  Branch: " << utf8::to8(vars.mbranch);
		std::string macros = utf8::to8(vars.macros);
		if(macros.length())
			s << "  Macros: " << macros;

		statusbar->SetStatusText(towxstring(s.str()));
	} catch(std::exception& e) {
	}
	inst.status->put_read();
}

#define NEW_KEYBINDING "A new binding..."
#define NEW_ALIAS "A new alias..."
#define NEW_WATCH "A new watch..."

void wxwin_mainwindow::handle_menu_click(wxCommandEvent& e)
{
	CHECK_UI_THREAD;
	try {
		handle_menu_click_cancelable(e);
	} catch(canceled_exception& e) {
		//Ignore.
	} catch(std::bad_alloc& e) {
		OOM_panic();
	} catch(std::exception& e) {
		show_message_ok(this, "Error in menu handler", e.what(), wxICON_EXCLAMATION);
	}
}

void wxwin_mainwindow::refresh_title() throw()
{
	CHECK_UI_THREAD;
	SetTitle(getname(inst));
	auto p = inst.project->get();
	menu_enable(wxID_RELOAD_ROM_IMAGE, !p);
	for(int i = wxID_LOAD_ROM_IMAGE_FIRST; i <= wxID_LOAD_ROM_IMAGE_LAST; i++)
		menu_enable(i, !p);
	menu_enable(wxID_CLOSE_PROJECT, p != NULL);
	menu_enable(wxID_CLOSE_ROM, p == NULL);
	reinterpret_cast<system_menu*>(sysmenu)->update(false);
	menubar->SetMenuLabel(1, towxstring(inst.rom->get_systemmenu_name()));
}

namespace
{
	struct movie_or_savestate
	{
	public:
		typedef std::pair<std::string,std::string> returntype;
		movie_or_savestate(emulator_instance& _inst, bool is_state)
			: inst(_inst)
		{
			state = is_state;
		}
		filedialog_input_params input(bool save) const
		{
			filedialog_input_params p;
			std::string ext = state ? inst.project->savestate_ext() : "lsmv";
			std::string name = state ? "Savestates" : "Movies";
			if(save) {
				p.types.push_back(filedialog_type_entry(name, "*." + ext, ext));
				p.types.push_back(filedialog_type_entry(name + " (binary)", "*." + ext, ext));
			} else
				p.types.push_back(filedialog_type_entry(name, "*." + ext + ";*." + ext + ".backup",
					ext));
			if(!save && state) {
				p.types.push_back(filedialog_type_entry("Savestates [playback]", "*." + ext +
					";*." + ext + ".backup", ext));
				p.types.push_back(filedialog_type_entry("Savestates [recording]", "*." + ext +
					";*." + ext + ".backup", ext));
				p.types.push_back(filedialog_type_entry("Savestates [preserve]", "*." + ext +
					";*." + ext + ".backup", ext));
				p.types.push_back(filedialog_type_entry("Savestates [all branches]", "*." + ext +
					";*." + ext + ".backup", ext));
			}
			p.default_type = save ? (state ? save_dflt_binary(*inst.settings) :
				movie_dflt_binary(*inst.settings)) : 0;
			return p;
		}
		std::pair<std::string, std::string> output(const filedialog_output_params& p, bool save) const
		{
			std::string cmdmod;
			if(save)
				cmdmod = p.typechoice ? "-binary" : "-zip";
			else if(state)
				switch(p.typechoice) {
				case 0: cmdmod = ""; break;
				case 1: cmdmod = "-readonly"; break;
				case 2: cmdmod = "-state"; break;
				case 3: cmdmod = "-preserve"; break;
				case 4: cmdmod = "-allbranches"; break;
			}
			return std::make_pair(cmdmod, p.path);
		}
	private:
		emulator_instance& inst;
		bool state;
	};
	struct movie_or_savestate filetype_movie(lsnes_instance, false);
	struct movie_or_savestate filetype_savestate(lsnes_instance, true);
}

void wxwin_mainwindow::project_selected(const std::string& id)
{
	std::string filename, displayname;
	bool load_ok = false;
	inst.iqueue->run([id, &filename, &displayname, &load_ok]() -> void {
		try {
			auto& p = CORE().project->load(id);	//Check.
			filename = p.filename;
			displayname = p.name;
			load_ok = true;
			delete &p;
			switch_projects(id);
		} catch(std::exception& e) {
			messages << "Failed to change project: " << e.what() << std::endl;
		}
	});
	if(load_ok) {
		recentfiles::namedobj obj;
		obj._id = id;
		obj._filename = filename;
		obj._display = displayname;
		projects->add(obj);
	}
}

void wxwin_mainwindow::handle_menu_click_cancelable(wxCommandEvent& e)
{
	CHECK_UI_THREAD;
	std::string filename;
	std::pair<std::string, std::string> filename2;
	bool s;
	switch(e.GetId()) {
	case wxID_FRAMEADVANCE:
		inst.iqueue->queue("+advance-frame");
		inst.iqueue->queue("-advance-frame");
		return;
	case wxID_SUBFRAMEADVANCE:
		inst.iqueue->queue("+advance-poll");
		inst.iqueue->queue("-advance-poll");
		return;
	case wxID_NEXTPOLL:
		inst.iqueue->queue("advance-skiplag");
		return;
	case wxID_PAUSE:
		inst.iqueue->queue("pause-emulator");
		return;
	case wxID_EXIT:
		inst.iqueue->queue("quit-emulator");
		return;
	case wxID_AUDIO_ENABLED:
		platform::sound_enable(menu_ischecked(wxID_AUDIO_ENABLED));
		return;
	case wxID_CANCEL_SAVES:
		inst.iqueue->queue("cancel-saves");
		return;
	case wxID_LOAD_MOVIE:
		filename = choose_file_load(this, "Load Movie", UI_get_project_moviepath(inst),
			filetype_movie).second;
		recent_movies->add(filename);
		inst.iqueue->queue(CLOADSAVE::ldm.name, filename);
		return;
	case wxID_LOAD_STATE:
		filename2 = choose_file_load(this, "Load State", UI_get_project_moviepath(inst),
			filetype_savestate);
		recent_movies->add(filename2.second);
		inst.iqueue->queue("load" + filename2.first + " " + filename2.second);
		return;
	case wxID_REWIND_MOVIE:
		inst.iqueue->queue("rewind-movie");
		return;
	case wxID_SAVE_MOVIE:
		filename2 = choose_file_save(this, "Save Movie", UI_get_project_moviepath(inst), filetype_movie,
			project_prefixname(inst, "lsmv"));
		recent_movies->add(filename2.second);
		inst.iqueue->queue("save-movie" + filename2.first + " " + filename2.second);
		return;
	case wxID_SAVE_SUBTITLES:
		inst.iqueue->queue(CSUBTITLE::save.name, choose_file_save(this, "Save subtitles",
			UI_get_project_moviepath(inst), filetype_sub, project_prefixname(inst, "sub")));
		return;
	case wxID_SAVE_STATE:
		filename2 = choose_file_save(this, "Save State", UI_get_project_moviepath(inst),
			filetype_savestate);
		recent_movies->add(filename2.second);
		inst.iqueue->queue("save-state" + filename2.first + " " + filename2.second);
		return;
	case wxID_SAVE_SCREENSHOT:
		inst.iqueue->queue(CFRAMEBUF::ss.name, choose_file_save(this, "Save Screenshot",
			UI_get_project_moviepath(inst), filetype_png, get_default_screenshot_name(inst)));
		return;
	case wxID_RUN_SCRIPT:
		inst.iqueue->queue(CLUA::run.name, pick_file_member(this, "Select Script",
			UI_get_project_otherpath(inst)));
		return;
	case wxID_RUN_LUA: {
		std::string f = choose_file_load(this, "Select Lua Script", UI_get_project_otherpath(inst),
			filetype_lua_script);
		inst.iqueue->queue(CLUA::run.name, f);
		recent_scripts->add(f);
		return;
	}
	case wxID_RESET_LUA:
		inst.iqueue->queue(CLUA::reset.name);
		return;
	case wxID_EVAL_LUA:
		inst.iqueue->queue(CLUA::eval.name, pick_text(this, "Evaluate Lua", "Enter Lua Statement:"));
		return;
	case wxID_READONLY_MODE:
		s = menu_ischecked(wxID_READONLY_MODE);
		inst.iqueue->run([s]() {
			auto& core = CORE();
			if(!s)
				core.lua2->callback_movie_lost("readwrite");
			if(*core.mlogic) core.mlogic->get_movie().readonly_mode(s);
			core.dispatch->mode_change(s);
			if(!s)
				core.lua2->callback_do_readwrite();
			core.supdater->update();
			core.dispatch->status_update();
		});
		return;
	case wxID_AUTOHOLD:
		wxeditor_autohold_display(this, inst);
		return;
	case wxID_EDIT_AUTHORS:
		wxeditor_authors_display(this, inst);
		return;
	case wxID_EDIT_MACROS:
		wxeditor_macro_display(this, inst);
		return;
	case wxID_EDIT_SUBTITLES:
		wxeditor_subtitles_display(this, inst);
		return;
	case wxID_EDIT_VSUBTITLES:
		show_wxeditor_voicesub(this, inst);
		return;
	case wxID_EDIT_MEMORYWATCH:
		wxeditor_memorywatches_display(this, inst);
		return;
	case wxID_SAVE_MEMORYWATCH: {
		modal_pause_holder hld;
		std::set<std::string> old_watches;
		inst.iqueue->run([&old_watches]() { old_watches = CORE().mwatch->enumerate(); });
		std::string filename = choose_file_save(this, "Save watches to file",
			UI_get_project_otherpath(inst), filetype_watch);
		std::ofstream out(filename.c_str());
		for(auto i : old_watches) {
			std::string val;
			inst.iqueue->run([i, &val]() {
				try {
					val = CORE().mwatch->get_string(i);
				} catch(std::exception& e) {
					messages << "Can't get value of watch '" << i << "': " << e.what()
						<< std::endl;
				}
			});
			out << i << std::endl << val << std::endl;
		}
		out.close();
		return;
	}
	case wxID_LOAD_MEMORYWATCH: {
		modal_pause_holder hld;
		std::set<std::string> old_watches;
		inst.iqueue->run([&old_watches]() { old_watches = CORE().mwatch->enumerate(); });
		std::map<std::string, std::string> new_watches;
		std::string filename = choose_file_load(this, "Choose memory watch file",
			UI_get_project_otherpath(inst), filetype_watch);
		try {
			std::istream& in = zip::openrel(filename, "");
			while(in) {
				std::string wname;
				std::string wexpr;
				std::getline(in, wname);
				std::getline(in, wexpr);
				new_watches[strip_CR(wname)] = strip_CR(wexpr);
			}
			delete &in;
		} catch(std::exception& e) {
			show_message_ok(this, "Error", std::string("Can't load memory watch: ") + e.what(),
				wxICON_EXCLAMATION);
			return;
		}

		inst.iqueue->run([this, &new_watches, &old_watches]() {
			handle_watch_load(this->inst, new_watches, old_watches);
		});
		return;
	}
	case wxID_MEMORY_SEARCH:
		wxwindow_memorysearch_display(inst);
		return;
	case wxID_TASINPUT:
		wxeditor_tasinput_display(this, inst);
		return;
	case wxID_ABOUT: {
		std::ostringstream str;
		str << "Version: lsnes rr" << lsnes_version << std::endl;
		str << "Revision: " << lsnes_git_revision << std::endl;
		for(auto i : core_core::all_cores())
			if(!i->is_hidden())
				str << "Core: " << i->get_core_identifier() << std::endl;
		wxMessageBox(towxstring(str.str()), _T("About"), wxICON_INFORMATION | wxOK, this);
		return;
	}
	case wxID_SHOW_STATUS: {
		bool newstate = menu_ischecked(wxID_SHOW_STATUS);
		if(newstate)
			spanel->Show();
		if(newstate && !spanel_shown)
			toplevel->Add(spanel, 1, wxGROW);
		else if(!newstate && spanel_shown)
			toplevel->Detach(spanel);
		if(!newstate)
			spanel->Hide();
		spanel_shown = newstate;
		toplevel->Layout();
		toplevel->SetSizeHints(this);
		Fit();
		return;
	}
	case wxID_DEDICATED_MEMORY_WATCH: {
		bool newstate = menu_ischecked(wxID_DEDICATED_MEMORY_WATCH);
		if(newstate && !mwindow) {
			mwindow = new wxwin_status(-1, inst, "Memory Watch");
			spanel->set_watch_flag(1);
			mwindow->Show();
		} else if(!newstate && mwindow) {
			mwindow->Destroy();
			mwindow = NULL;
			spanel->set_watch_flag(0);
		}
		return;
	}
	case wxID_SET_SPEED: {
		std::string value = "infinite";
		double val = inst.framerate->get_speed_multiplier();
		if(!(val == std::numeric_limits<double>::infinity()))
			value = (stringfmt() << (100 * val)).str();
		value = pick_text(this, "Set speed", "Enter percentage speed (or \"infinite\"):", value);
		try {
			if(value == "infinite")
				inst.framerate->set_speed_multiplier(
					std::numeric_limits<double>::infinity());
			else {
				double v = parse_value<double>(value) / 100;
				if(v <= 0.0001)
					throw 42;
				inst.framerate->set_speed_multiplier(v);
			}
		} catch(...) {
			wxMessageBox(wxT("Invalid speed"), _T("Error"), wxICON_EXCLAMATION | wxOK, this);
		}
		return;
	}
	case wxID_SPEED_5:
		set_speed(inst, 5);
		break;
	case wxID_SPEED_10:
		set_speed(inst, 10);
		break;
	case wxID_SPEED_17:
		set_speed(inst, 16.66666666666);
		break;
	case wxID_SPEED_20:
		set_speed(inst, 20);
		break;
	case wxID_SPEED_25:
		set_speed(inst, 25);
		break;
	case wxID_SPEED_33:
		set_speed(inst, 33.3333333333333);
		break;
	case wxID_SPEED_50:
		set_speed(inst, 50);
		break;
	case wxID_SPEED_100:
		set_speed(inst, 100);
		break;
	case wxID_SPEED_150:
		set_speed(inst, 150);
		break;
	case wxID_SPEED_200:
		set_speed(inst, 200);
		break;
	case wxID_SPEED_300:
		set_speed(inst, 300);
		break;
	case wxID_SPEED_500:
		set_speed(inst, 500);
		break;
	case wxID_SPEED_1000:
		set_speed(inst, 1000);
		break;
	case wxID_SPEED_TURBO:
		set_speed(inst, -1);
		break;
	case wxID_LOAD_LIBRARY: {
		std::string name = std::string("load ") + loadlib::library::name();
		with_loaded_library(*new loadlib::module(loadlib::library(choose_file_load(this, name,
			UI_get_project_otherpath(inst), single_type(loadlib::library::extension(),
			loadlib::library::name())))));
		handle_post_loadlibrary();
		break;
	}
	case wxID_PLUGIN_MANAGER:
		wxeditor_plugin_manager_display(this);
		return;
	case wxID_RELOAD_ROM_IMAGE:
		inst.iqueue->run([]() {
			CORE().command->invoke("unpause-emulator");
			reload_current_rom();
		});
		return;
	case wxID_NEW_MOVIE:
		show_projectwindow(this, inst);
		return;
	case wxID_SHOW_MESSAGES:
		msg_window->reshow();
		return;
	case wxID_CONFLICTRESOLUTION:
		show_conflictwindow(this);
		return;
	case wxID_VUDISPLAY:
		open_vumeter_window(this, inst);
		return;
	case wxID_DISASSEMBLER:
		wxeditor_disassembler_display(this, inst);
		return;
	case wxID_MOVIE_EDIT:
		wxeditor_movie_display(this, inst);
		return;
	case wxID_NEW_PROJECT:
		open_new_project_window(this, inst);
		return;
	case wxID_CLOSE_PROJECT:
		inst.iqueue->run([this]() -> void { this->inst.project->set(NULL); });
		return;
	case wxID_CLOSE_ROM:
		inst.iqueue->run([]() -> void { close_rom(); });
		return;
	case wxID_ENTER_FULLSCREEN:
		wx_escape_count = 0;
		enter_or_leave_fullscreen(true);
		return;
	case wxID_LOAD_ROM_IMAGE_FIRST:
		do_load_rom_image(NULL);
		return;
	case wxID_HEXEDITOR:
		wxeditor_hexedit_display(this, inst);
		return;
	case wxID_MULTITRACK:
		wxeditor_multitrack_display(this, inst);
		return;
	case wxID_CHDIR: {
		wxDirDialog* d = new wxDirDialog(this, wxT("Change working directory"), wxT("."),
			wxDD_DIR_MUST_EXIST);
		if(d->ShowModal() == wxID_CANCEL) {
			d->Destroy();
			return;
		}
		std::string path = tostdstring(d->GetPath());
		d->Destroy();
		chdir(path.c_str());
		messages << "Changed working directory to '" << path << "'" << std::endl;
		return;
	}
	case wxID_DOWNLOAD: {
		if(download_in_progress) return;
		filename = pick_text(this, "Download movie", "Enter URL to download");
		download_in_progress = new file_download();
		download_in_progress->url = lsnes_uri_rewrite(filename);
		download_in_progress->target_slot = "wxwidgets_download_tmp";
		download_in_progress->do_async(*inst.rom);
		new download_timer(this, inst);
		return;
	}
	case wxID_START_R16M: {
		std::string filename = choose_file_save(this, "Save r16m dump to file",
			UI_get_project_otherpath(inst), filetype_r16m);
		inst.iqueue->run([filename]() {
			CORE().command->invoke("start-r16m "+filename);
		});
		return;
	}
	case wxID_END_R16M: {
		inst.iqueue->run([]() {
			CORE().command->invoke("end-r16m");
		});
		return;
	}
	};
}

void wxwin_mainwindow::action_updated()
{
	runuifun([this]() { reinterpret_cast<system_menu*>(sysmenu)->update(true); });
}

void wxwin_mainwindow::enter_or_leave_fullscreen(bool fs)
{
	CHECK_UI_THREAD;
	if(fs && !is_fs) {
		//Save current resolution, so we can see the change.
		current_resolution = main_window->GetSize();
		if(spanel_shown)
			toplevel->Detach(spanel);
		spanel->Hide();
		is_fs = fs;
		ShowFullScreen(true);
		becoming_fullscreen = true;
		request_paint();	//Finish the resizing by running paint handler.
	} else if(!fs && is_fs) {
		becoming_fullscreen = false;
		ShowFullScreen(false);
		gpanel->Show();
		if(spanel_shown) {
			spanel->Show();
			toplevel->Add(spanel, 1, wxGROW);
		}
		Fit();
		gpanel->SetFocus();
		is_fs = fs;
		wx_escape_count = 0;
		request_paint();	//Don't leave graphical corruption.
	}
}

namespace
{
	struct command::stub _exit_fullscreen = {"exit-fullscreen", "Exit fullscreen",
		"Syntax: exit_fullscreen\nExit fullscreen"};
	command::fnptr<> exit_fullscreen(lsnes_cmds, _exit_fullscreen,
		[]() {
			runuifun([]() {
				if(is_fs)
					main_window->enter_or_leave_fullscreen(false);
			});
		});
}
