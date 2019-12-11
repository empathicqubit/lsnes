#include <wx/wx.h>

#include "lsnes.hpp"

#include "core/audioapi.hpp"
#include "core/command.hpp"
#include "core/controller.hpp"
#include "core/dispatch.hpp"
#include "core/framerate.hpp"
#include "core/joystickapi.hpp"
#include "core/keymapper.hpp"
#include "core/loadlib.hpp"
#include "lua/lua.hpp"
#include "core/advdumper.hpp"
#include "core/mainloop.hpp"
#include "core/messages.hpp"
#include "core/misc.hpp"
#include "core/instance.hpp"
#include "core/misc.hpp"
#include "core/moviefile-common.hpp"
#include "core/moviedata.hpp"
#include "core/random.hpp"
#include "core/rom.hpp"
#include "core/settings.hpp"
#include "core/window.hpp"
#include "interface/romtype.hpp"
#include "library/crandom.hpp"
#include "library/directory.hpp"
#include "library/running-executable.hpp"
#include "library/string.hpp"
#include "library/threads.hpp"
#include "library/utf8.hpp"
#include "library/zip.hpp"

#include "platform/wxwidgets/settings-common.hpp"
#include "platform/wxwidgets/platform.hpp"
#include "platform/wxwidgets/window_messages.hpp"
#include "platform/wxwidgets/window_status.hpp"
#include "platform/wxwidgets/window_mainwindow.hpp"

#include <functional>
#include <math.h>
#include <cassert>

#include <wx/wx.h>
#include <wx/event.h>
#include <wx/control.h>
#include <wx/combobox.h>
#include <wx/cmdline.h>
#include <iostream>

#ifdef __WXMAC__
#error "Mac OS is not supported"
#endif

#define UISERV_REFRESH_TITLE 9990
#define UISERV_RESIZED 9991
#define UISERV_UIFUN 9992
#define UISERV_EXIT 9994
#define UISERV_PANIC 9998
#define UISERV_ERROR 9999

wxwin_messages* msg_window;
wxwin_mainwindow* main_window;
std::string our_rom_name;

bool wxwidgets_exiting = false;

namespace
{
	threads::id ui_thread;
	volatile bool panic_ack = false;
	std::string error_message_text;
	volatile bool modal_dialog_confirm;
	volatile bool modal_dialog_active;
	threads::lock ui_mutex;
	threads::cv ui_condition;
	bool preboot_env = true;
	runuifun_once_ctx screenupdate_once;
	runuifun_once_ctx statusupdate_once;
	runuifun_once_ctx message_once;

	struct uiserv_event : public wxEvent
	{
		uiserv_event(int code)
		{
			SetId(code);
		}

		wxEvent* Clone() const
		{
			return new uiserv_event(*this);
		}
	};

	class ui_services_type : public wxEvtHandler
	{
		bool ProcessEvent(wxEvent& event);
	};

	struct ui_queue_entry
	{
		void(*fn)(void*);
		void* arg;
		runuifun_once_ctx* ctx;
	};

	std::list<ui_queue_entry> ui_queue;

	void do_panic()
	{
		std::string msg = "Panic: Unrecoverable error, can't continue";
		std::string msg2 = platform::msgbuf.get_last_message();
		if(msg2 != "")
			msg += "\n\n" + msg2;
		wxMessageBox(towxstring(msg), _T("Error"), wxICON_ERROR | wxOK);
	}

	bool ui_services_type::ProcessEvent(wxEvent& event)
	{
		CHECK_UI_THREAD;
		int c = event.GetId();
		if(c == UISERV_PANIC) {
			//We need to panic.
			do_panic();
			panic_ack = true;
		} else if(c == UISERV_REFRESH_TITLE) {
			if(main_window)
				main_window->refresh_title();
		} else if(c == UISERV_RESIZED) {
			if(main_window)
				main_window->notify_resized();
		} else if(c == UISERV_ERROR) {
			std::string text = error_message_text;
			wxMessageBox(towxstring(text), _T("lsnes: Error"), wxICON_EXCLAMATION | wxOK, main_window);
		} else if(c == UISERV_EXIT) {
			if(main_window)
				main_window->notify_exit();
		} else if(c == UISERV_UIFUN) {
			std::list<ui_queue_entry>::iterator i;
			ui_queue_entry e;
			queue_synchronous_fn_warning = true;
back:
			{
				threads::alock h(ui_mutex);
				if(ui_queue.empty())
					goto end;
				i = ui_queue.begin();
				e = *i;
				ui_queue.erase(i);
			}
			if(e.ctx) e.ctx->clear_flag();
			e.fn(e.arg);
			goto back;
end:
			queue_synchronous_fn_warning = false;
		}
		return true;
	}

	ui_services_type* ui_services;

	void post_ui_event(int code)
	{
		uiserv_event uic(code);
		wxPostEvent(ui_services, uic);
	}

	std::string loaded_pdev;
	std::string loaded_rdev;

	double from_logscale(double v)
	{
		return exp(v);
	}

	void handle_config_line(std::string line)
	{
		regex_results r;
		if(r = regex("SET[ \t]+([^ \t]+)[ \t]+(.*)", line)) {
			lsnes_instance.setcache->set(r[1], r[2], true);
			messages << "Setting " << r[1] << " set to " << r[2] << std::endl;
		} else if(r = regex("ALIAS[ \t]+([^ \t]+)[ \t]+(.*)", line)) {
			if(!lsnes_instance.command->valid_alias_name(r[1])) {
				messages << "Illegal alias name " << r[1] << std::endl;
				return;
			}
			std::string tmp = lsnes_instance.command->get_alias_for(r[1]);
			tmp = tmp + r[2] + "\n";
			lsnes_instance.command->set_alias_for(r[1], tmp);
			messages << r[1] << " aliased to " << r[2] << std::endl;
		} else if(r = regex("BIND[ \t]+([^/]*)/([^|]*)\\|([^ \t]+)[ \t]+(.*)", line)) {
			std::string tmp = r[4];
			regex_results r2 = regex("(load|load-smart|load-readonly|load-preserve|load-state"
				"|load-movie|save-state|save-movie)[ \t]+\\$\\{project\\}(.*)\\.lsmv", tmp);
			if(r2) tmp = r2[1] + " $SLOT:" + r2[2];
			lsnes_instance.mapper->bind(r[1], r[2], r[3], tmp);
			if(r[1] != "" || r[2] != "")
				messages << r[1] << "/" << r[2] << " ";
			messages << r[3] << " bound to '" << tmp << "'" << std::endl;
		} else if(r = regex("BUTTON[ \t]+([^ \t]+)[ \t](.*)", line)) {
			keyboard::ctrlrkey* ckey = lsnes_instance.mapper->get_controllerkey(r[2]);
			if(ckey) {
				ckey->append(r[1]);
				messages << r[1] << " bound (button) to " << r[2] << std::endl;
			} else
				lsnes_instance.buttons->button_keys[r[2]] = r[1];
		} else if(r = regex("PREFER[ \t]+([^ \t]+)[ \t]+(.*)", line)) {
			if(r[2] != "") {
				core_selections[r[1]] = r[2];
				messages << "Prefer " << r[2] << " for " << r[1] << std::endl;
			}
		} else if(r = regex("AUDIO_PDEV[ \t]+([^ \t].*)", line)) {
			loaded_pdev = r[1];
		} else if(r = regex("AUDIO_RDEV[ \t]+([^ \t].*)", line)) {
			loaded_rdev = r[1];
		} else if(r = regex("AUDIO_GVOL[ \t]+([^ \t].*)", line)) {
			lsnes_instance.audio->music_volume(from_logscale(parse_value<double>(r[1])));
		} else if(r = regex("AUDIO_RVOL[ \t]+([^ \t].*)", line)) {
			lsnes_instance.audio->voicer_volume(from_logscale(parse_value<double>(r[1])));
		} else if(r = regex("AUDIO_PVOL[ \t]+([^ \t].*)", line)) {
			lsnes_instance.audio->voicep_volume(from_logscale(parse_value<double>(r[1])));
		} else if(r = regex("VIDEO_ARC[ \t]*", line)) {
			arcorrect_enabled = true;
		} else if(r = regex("VIDEO_HFLIP[ \t]*", line)) {
			hflip_enabled = true;
		} else if(r = regex("VIDEO_VFLIP[ \t]*", line)) {
			vflip_enabled = true;
		} else if(r = regex("VIDEO_ROTATE[ \t]*", line)) {
			rotate_enabled = true;
		} else if(r = regex("VIDEO_SFACT[ \t]+([^ \t].*)", line)) {
			double val = parse_value<double>(r[1]);
			if(val < 0.1 || val > 10) throw std::runtime_error("Crazy scale factor");
			video_scale_factor = val;
		} else if(r = regex("VIDEO_SFLAGS[ \t]+([^ \t].*)", line)) {
			scaling_flags = parse_value<uint32_t>(r[1]);
		} else
			(stringfmt() << "Unrecognized directive: " << line).throwex();
	}

	void load_configuration()
	{
		std::string cfg = get_config_path() + "/lsneswxw.cfg";
		std::ifstream cfgfile(cfg.c_str());
		std::string line;
		size_t lineno = 1;
		while(std::getline(cfgfile, line)) {
			try {
				handle_config_line(line);
			} catch(std::exception& e) {
				messages << "Error processing line " << lineno << ": " << e.what() << std::endl;
			}
			lineno++;
		}
		platform::set_sound_device_by_description(loaded_pdev, loaded_rdev);
		(*lsnes_instance.abindmanager)();
		lsnes_uri_rewrite.load(get_config_path() + "/lsnesurirewrite.cfg");
	}

	double to_logscale(double v)
	{
		if(fabs(v) < 1e-15)
			return -999.0;
		return log(fabs(v));
	}

	void save_configuration()
	{
		std::string cfg = get_config_path() + "/lsneswxw.cfg";
		std::string cfgtmp = cfg + ".tmp";
		std::ofstream cfgfile(cfgtmp.c_str());
		//Settings.
		for(auto i : lsnes_instance.setcache->get_all())
			cfgfile << "SET " << i.first << " " << i.second << std::endl;
		//Aliases.
		for(auto i : lsnes_instance.command->get_aliases()) {
			std::string old_alias_value = lsnes_instance.command->get_alias_for(i);
			while(old_alias_value != "") {
				std::string aliasline;
				size_t s = old_alias_value.find_first_of("\n");
				if(s < old_alias_value.length()) {
					aliasline = old_alias_value.substr(0, s);
					old_alias_value = old_alias_value.substr(s + 1);
				} else {
					aliasline = old_alias_value;
					old_alias_value = "";
				}
				cfgfile << "ALIAS " << i << " " << aliasline << std::endl;
			}
		}
		//Keybindings.
		for(auto i : lsnes_instance.mapper->get_bindings())
			cfgfile << "BIND " << std::string(i) << " " << lsnes_instance.mapper->get(i) << std::endl;
		//Buttons.
		for(auto i : lsnes_instance.mapper->get_controller_keys()) {
			std::string b;
			unsigned idx = 0;
			while((b = i->get_string(idx++)) != "")
				cfgfile << "BUTTON " << b << " " << i->get_command() << std::endl;
		}
		for(auto i : lsnes_instance.buttons->button_keys)
			cfgfile << "BUTTON " << i.second << " " << i.first << std::endl;
		for(auto i : core_selections)
			if(i.second != "")
				cfgfile << "PREFER " << i.first << " " << i.second << std::endl;
		//Sound config.
		cfgfile << "AUDIO_PDEV " << platform::get_sound_device_description(false) << std::endl;
		cfgfile << "AUDIO_RDEV " << platform::get_sound_device_description(true) << std::endl;
		cfgfile << "AUDIO_GVOL " << to_logscale(lsnes_instance.audio->music_volume()) << std::endl;
		cfgfile << "AUDIO_RVOL " << to_logscale(lsnes_instance.audio->voicer_volume()) << std::endl;
		cfgfile << "AUDIO_PVOL " << to_logscale(lsnes_instance.audio->voicep_volume()) << std::endl;
		cfgfile << "VIDEO_SFACT " << video_scale_factor << std::endl;
		cfgfile << "VIDEO_SFLAGS " << scaling_flags << std::endl;
		if(arcorrect_enabled) cfgfile << "VIDEO_ARC" << std::endl;
		if(hflip_enabled) cfgfile << "VIDEO_HFLIP" << std::endl;
		if(vflip_enabled) cfgfile << "VIDEO_VFLIP" << std::endl;
		if(rotate_enabled) cfgfile << "VIDEO_ROTATE" << std::endl;
		if(!cfgfile) {
			show_message_ok(NULL, "Error Saving configuration", "Error saving configuration",
				wxICON_EXCLAMATION);
			return;
		}
		cfgfile.close();
		directory::rename_overwrite(cfgtmp.c_str(), cfg.c_str());
		//Last save.
		std::ofstream lsave(get_config_path() + "/" + our_rom_name + ".ls");
		lsave << last_save;
		lsnes_uri_rewrite.save(get_config_path() + "/lsnesurirewrite.cfg");
	}

	void* eloop_helper(int x)
	{
		platform::dummy_event_loop();
		return NULL;
	}

	std::string get_loaded_movie(const std::vector<std::string>& cmdline)
	{
		for(auto i : cmdline)
			if(!i.empty() && i[0] != '-')
				return i;
		return "";
	}
}

wxString towxstring(const std::string& str)
{
	return wxString(str.c_str(), wxConvUTF8);
}

std::string tostdstring(const wxString& str)
{
	return std::string(str.mb_str(wxConvUTF8));
}

wxString towxstring(const std::u32string& str)
{
	return wxString(utf8::to8(str).c_str(), wxConvUTF8);
}

std::u32string tou32string(const wxString& str)
{
	return utf8::to32(std::string(str.mb_str(wxConvUTF8)));
}

std::string pick_archive_member(wxWindow* parent, const std::string& filename)
{
	CHECK_UI_THREAD;
	//Did we pick a .zip file?
	std::string f;
	try {
		zip::reader zr(filename);
		std::vector<wxString> files;
		for(auto i : zr)
			files.push_back(towxstring(i));
		wxSingleChoiceDialog* d2 = new wxSingleChoiceDialog(parent, wxT("Select file within .zip"),
			wxT("Select member"), files.size(), &files[0]);
		if(d2->ShowModal() == wxID_CANCEL) {
			d2->Destroy();
			return "";
		}
		f = filename + "/" + tostdstring(d2->GetStringSelection());
		d2->Destroy();
	} catch(...) {
		//Ignore error.
		f = filename;
	}
	return f;
}

void signal_program_exit()
{
	post_ui_event(UISERV_EXIT);
}

void signal_resize_needed()
{
	post_ui_event(UISERV_RESIZED);
}


static const wxCmdLineEntryDesc dummy_descriptor_table[] = {
	{ wxCMD_LINE_PARAM,  NULL, NULL, NULL, wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL |
		wxCMD_LINE_PARAM_MULTIPLE },
	{ wxCMD_LINE_NONE }
};

class lsnes_app : public wxApp
{
public:
	lsnes_app();
	virtual bool OnInit();
	virtual int OnExit();
	virtual void OnInitCmdLine(wxCmdLineParser& parser);
	virtual bool OnCmdLineParsed(wxCmdLineParser& parser);
private:
	bool settings_mode;
	bool pluginmanager_mode;
	std::string c_rom;
	std::string c_file;
	std::vector<std::string> cmdline;
	std::map<std::string, std::string> c_settings;
	std::vector<std::string> c_lua;
	std::vector<std::string> c_library;
	bool exit_immediately;
	bool fullscreen_mode;
	bool start_unpaused;
	struct dispatch::target<> screenupdate;
	struct dispatch::target<> statusupdate;
	struct dispatch::target<> actionupdate;
};

IMPLEMENT_APP(lsnes_app)

lsnes_app::lsnes_app()
{
	settings_mode = false;
	pluginmanager_mode = false;
	exit_immediately = false;
	fullscreen_mode = false;
	start_unpaused = false;
}

void lsnes_app::OnInitCmdLine(wxCmdLineParser& parser)
{
	parser.SetDesc(dummy_descriptor_table);
	parser.SetSwitchChars(wxT(""));
}

static bool regex_sanity_check()
{
	bool regex_sane = true;
	try {
		//Simple sanity checks.
		regex_sane &= regex_match("foo.*baz", "foobarbaz", REGEX_MATCH_REGEX);
		regex_sane &= regex_match(".*foo.*baz.*", "foobarbaz", REGEX_MATCH_REGEX);
		regex_sane &= regex_match("foo*baz", "FOOBARBAZ", REGEX_MATCH_IWILDCARDS);
		regex_sane &= regex_match("foo.*baz", "FOOBARBAZ", REGEX_MATCH_IREGEX);
	} catch(...) {
		regex_sane = false;
	}
	return regex_sane;
}

bool lsnes_app::OnCmdLineParsed(wxCmdLineParser& parser)
{
	for(size_t i = 0; i< parser.GetParamCount(); i++)
		cmdline.push_back(tostdstring(parser.GetParam(i)));
	for(auto i: cmdline) {
		regex_results r;
		if(i == "--help" || i == "-h") {
			std::cout << "--settings: Show the settings dialog" << std::endl;
			std::cout << "--pluginmanager: Show the plugin manager" << std::endl;
			std::cout << "--fullscreen: Start fullscreen" << std::endl;
			std::cout << "--unpause: Start unpaused (only if ROM is loaded)" << std::endl;
			std::cout << "--rom=<filename>: Load specified ROM on startup" << std::endl;
			std::cout << "--load=<filename>: Load specified save/movie on starup" << std::endl;
			std::cout << "--lua=<filename>: Load specified Lua script on startup" << std::endl;
			std::cout << "--library=<filename>: Load specified library on startup" << std::endl;
			std::cout << "--set=<a>=<b>: Set setting <a> to value <b>" << std::endl;
			std::cout << "--sanity-check: Perfrom some simple sanity checks" << std::endl;
			std::cout << "<filename>: Load specified ROM on startup" << std::endl;
			exit_immediately = true;
			return true;
		}
		if(i == "--settings")
			settings_mode = true;
		if(i == "--unpause")
			start_unpaused = true;
		if(i == "--fullscreen")
			fullscreen_mode = true;
		if(i == "--pluginmanager")
			pluginmanager_mode = true;
		if(r = regex("--set=([^=]+)=(.+)", i))
			c_settings[r[1]] = r[2];
		if(r = regex("--lua=(.+)", i))
			c_lua.push_back(r[1]);
		if(r = regex("--library=(.+)", i))
			c_library.push_back(r[1]);
		if(i == "--sanity-check") {
			if(regex_sanity_check()) {
				std::cout << "Regex library passes basic sanity checks." << std::endl;
			} else {
				std::cout << "Regex library FAILS basic sanity checks." << std::endl;
			}
			std::cout << "Executable: '" << running_executable() << "'" << std::endl; 
			std::cout << "Configuration directory: '" << get_config_path()
				<< "'" << std::endl;
			std::cout << "System autoload directory: '" << loadlib_debug_get_system_library_dir()
				<< "'" << std::endl;
			std::cout << "User autoload directory: '" << loadlib_debug_get_user_library_dir()
				<< "'" << std::endl;
			exit_immediately = true;
		}
	}
	return true;
}

bool lsnes_app::OnInit()
{
	wxApp::OnInit();
	if(exit_immediately)
		return false;

	screenupdate.set(lsnes_instance.dispatch->screen_update, []() {
		runuifun(screenupdate_once, []() {
			if(main_window)
				main_window->notify_update();
			wxwindow_memorysearch_update(CORE());
			wxwindow_tasinput_update(CORE());
		});
	});
	statusupdate.set(lsnes_instance.dispatch->status_update, []() {
		runuifun(statusupdate_once, []() {
			if(main_window)
				main_window->notify_update_status();
			wxeditor_movie_update(CORE());
			wxeditor_hexeditor_update(CORE());
		});
	});
	actionupdate.set(lsnes_instance.dispatch->action_update, []() {
		//This can be called early, so check for main_window existing.
		if(main_window)
			main_window->action_updated();
	});

	try {
		crandom::init();
	} catch(std::exception& e) {
		show_message_ok(NULL, "RNG error", "Error initializing system RNG", wxICON_ERROR);
		return false;
	}

	if(!regex_sanity_check()) {
		wxMessageBox(towxstring("Regex sanity check FAILED.\n\nExpect problems."),
			_T("lsnes: Error"), wxICON_EXCLAMATION | wxOK, NULL);
	}

	reached_main();
	set_random_seed();

	if(pluginmanager_mode)
		if(!wxeditor_plugin_manager_display(NULL))
			return false;

	ui_services = new ui_services_type();

	ui_thread = threads::this_id();
	platform::init();

	messages << "lsnes version: lsnes rr" << lsnes_version << std::endl;

	loaded_rom dummy_rom;
	std::map<std::string, std::string> settings;
	auto ctrldata = dummy_rom.controllerconfig(settings);
	portctrl::type_set& ports = portctrl::type_set::make(ctrldata.ports, ctrldata.portindex());

	lsnes_instance.buttons->reinit();
	lsnes_instance.controls->set_ports(ports);

	std::string cfgpath = get_config_path();
	autoload_libraries([](const std::string& libname, const std::string& error, bool system) {
		show_message_ok(NULL, "Error loading plugin " + libname, "Error loading '" + libname + "'\n\n" +
			error, wxICON_EXCLAMATION);
		if(!system)
			wxeditor_plugin_manager_notify_fail(libname);
	});
	messages << "Saving per-user data to: " << get_config_path() << std::endl;
	messages << "--- Loading configuration --- " << std::endl;
	load_configuration();
	messages << "--- End running lsnesrc --- " << std::endl;

	if(settings_mode) {
		//We got to boot this up quite a bit to get the joystick driver working.
		//In practicular, we need joystick thread and emulator thread in pause.
		threads::thread* dummy_loop = new threads::thread(eloop_helper, 8);
		display_settings_dialog(NULL, lsnes_instance, NULL);
		platform::exit_dummy_event_loop();
		joystick_driver_quit();
		dummy_loop->join();
		save_configuration();
		return false;
	}
	init_lua(lsnes_instance);
	lsnes_instance.mdumper->set_output(&messages.getstream());

	msg_window = new wxwin_messages(lsnes_instance);
	msg_window->Show();

	init_main_callbacks();

	//Load libraries before trying to load movie, in case there are cores in there.
	for(auto i : c_library) {
		try {
			with_loaded_library(*new loadlib::module(loadlib::library(i)));
		} catch(std::exception& e) {
			show_message_ok(NULL, "Error loading library", std::string("Error loading library '") +
				i + "':\n\n" + e.what(), wxICON_EXCLAMATION);
		}
	}

	const std::string movie_file = get_loaded_movie(cmdline);
	loaded_rom rom;
	try {
		moviefile mov;
		rom = construct_rom(movie_file, cmdline);
		rom.load(c_settings, mov.movie_rtc_second, mov.movie_rtc_subsecond);
	} catch(std::exception& e) {
		std::cerr << "Can't load ROM: " << e.what() << std::endl;
		show_message_ok(NULL, "Error loading ROM", std::string("Error loading ROM:\n\n") +
			e.what(), wxICON_EXCLAMATION);
		quit_lua(lsnes_instance);	//Don't crash.
		return false;
	}

	moviefile* mov = NULL;
	if(movie_file != "")
		try {
			mov = new moviefile(movie_file, rom.get_internal_rom_type());
			rom.load(mov->settings, mov->movie_rtc_second, mov->movie_rtc_subsecond);
		} catch(std::exception& e) {
			std::cerr << "Can't load state: " << e.what() << std::endl;
			show_message_ok(NULL, "Error loading movie", std::string("Error loading movie:\n\n") +
				e.what(), wxICON_EXCLAMATION);
			quit_lua(lsnes_instance);	//Don't crash.
			return false;
		}
	else {
		mov = new moviefile(rom, c_settings, DEFAULT_RTC_SECOND, DEFAULT_RTC_SUBSECOND);
	}
	*lsnes_instance.rom = rom;
	mov->start_paused = start_unpaused ? rom.isnull() : true;
	for(auto i : c_lua)
		lsnes_instance.lua2->add_startup_script(i);
	preboot_env = false;
	boot_emulator(lsnes_instance, rom, *mov, fullscreen_mode);
	return true;
}

int lsnes_app::OnExit()
{
	if(settings_mode)
		return 0;
	//NULL these so no further messages will be sent.
	auto x = msg_window;
	msg_window = NULL;
	main_window = NULL;
	if(x)
		x->Destroy();
	save_configuration();
	quit_lua(lsnes_instance);
	lsnes_instance.mlogic->release_memory();
	platform::quit();
	lsnes_instance.buttons->cleanup();
	cleanup_keymapper();
	deinitialize_wx_mouse(lsnes_instance);
	deinitialize_wx_keyboard(lsnes_instance);
	return 0;
}

void do_save_configuration()
{
	save_configuration();
}

namespace
{
	struct _graphics_driver drv = {
		.init = []() -> void {
			initialize_wx_keyboard(lsnes_instance);
			initialize_wx_mouse(lsnes_instance);
		},
		.quit = []() -> void {},
		.notify_message = []() -> void
		{
			runuifun(message_once, []() {
				if(msg_window)
					msg_window->notify_update();
			});
		},
		.error_message = [](const std::string& text) -> void {
			error_message_text = text;
			post_ui_event(UISERV_ERROR);
		},
		.fatal_error = []() -> void {
			//Fun: This can be called from any thread!
			if(ui_thread == threads::this_id()) {
				//UI thread.
				platform::set_modal_pause(true);
				do_panic();
			} else {
				//Emulation thread panic. Signal the UI thread.
				post_ui_event(UISERV_PANIC);
				while(!panic_ack);
			}
		},
		.name = []() -> const char* { return "wxwidgets graphics plugin"; },
		.request_rom = [](rom_request& req)
		{
			rom_request* _req = &req;
			threads::lock lock;
			threads::cv cv;
			bool done = false;
			if(preboot_env) {
				try {
					//main_window is NULL, hope this does not crash.
					main_window->request_rom(*_req);
				} catch(...) {
					_req->canceled = true;
				}
				return;
			}
			threads::alock h(lock);
			runuifun([_req, &lock, &cv, &done]() -> void {
				try {
					main_window->request_rom(*_req);
				} catch(...) {
					_req->canceled = true;
				}
				threads::alock h(lock);
				done = true;
				cv.notify_all();
			});
			while(!done)
				cv.wait(h);
		}
	};
	struct graphics_driver _drv(drv);
}

void signal_core_change()
{
	post_ui_event(UISERV_REFRESH_TITLE);
}

void _runuifun_async(runuifun_once_ctx* ctx, void (*fn)(void*), void* arg)
{
	if(ctx && !ctx->set_flag()) return;
	threads::alock h(ui_mutex);
	ui_queue_entry e;
	e.fn = fn;
	e.arg = arg;
	e.ctx = ctx;
	ui_queue.push_back(e);
	post_ui_event(UISERV_UIFUN);
}


canceled_exception::canceled_exception() : std::runtime_error("Dialog canceled") {}

std::string pick_file(wxWindow* parent, const std::string& title, const std::string& startdir)
{
	CHECK_UI_THREAD;
	wxString _title = towxstring(title);
	wxString _startdir = towxstring(startdir);
	std::string filespec;
	filespec = "All files|*";
	wxFileDialog* d = new wxFileDialog(parent, _title, _startdir, wxT(""), towxstring(filespec), wxFD_OPEN);
	if(d->ShowModal() == wxID_CANCEL)
		throw canceled_exception();
	std::string filename = tostdstring(d->GetPath());
	d->Destroy();
	if(filename == "")
		throw canceled_exception();
	return filename;
}

std::string pick_file_member(wxWindow* parent, const std::string& title, const std::string& startdir)
{
	CHECK_UI_THREAD;
	std::string filename = pick_file(parent, title, startdir);
	//Did we pick a .zip file?
	if(!regex_match(".*\\.[zZ][iI][pP]", filename))
		return filename;	//Not a ZIP.
	try {
		zip::reader zr(filename);
		std::vector<std::string> files;
		for(auto i : zr)
			files.push_back(i);
		filename = filename + "/" + pick_among(parent, "Select member", "Select file within .zip", files);
	} catch(canceled_exception& e) {
		//Throw these forward.
		throw;
	} catch(...) {
		//Ignore error.
	}
	return filename;
}

unsigned pick_among_index(wxWindow* parent, const std::string& title, const std::string& prompt,
	const std::vector<std::string>& choices, unsigned defaultchoice)
{
	CHECK_UI_THREAD;
	std::vector<wxString> _choices;
	for(auto i : choices)
		_choices.push_back(towxstring(i));
	wxSingleChoiceDialog* d2 = new wxSingleChoiceDialog(parent, towxstring(prompt), towxstring(title),
		_choices.size(), &_choices[0]);
	d2->SetSelection(defaultchoice);
	if(d2->ShowModal() == wxID_CANCEL) {
		d2->Destroy();
		throw canceled_exception();
	}
	unsigned idx = d2->GetSelection();
	d2->Destroy();
	return idx;
}

std::string pick_among(wxWindow* parent, const std::string& title, const std::string& prompt,
	const std::vector<std::string>& choices, unsigned defaultchoice)
{
	unsigned idx = pick_among_index(parent, title, prompt, choices, defaultchoice);
	if(idx < choices.size())
		return choices[idx];
	throw canceled_exception();
}

std::string pick_text(wxWindow* parent, const std::string& title, const std::string& prompt, const std::string& dflt,
	bool multiline)
{
	CHECK_UI_THREAD;
	wxTextEntryDialog* d2 = new wxTextEntryDialog(parent, towxstring(prompt), towxstring(title), towxstring(dflt),
		wxOK | wxCANCEL | wxCENTRE | (multiline ? wxTE_MULTILINE : 0));
	if(d2->ShowModal() == wxID_CANCEL) {
		d2->Destroy();
		throw canceled_exception();
	}
	std::string text = tostdstring(d2->GetValue());
	d2->Destroy();
	return text;
}

void show_message_ok(wxWindow* parent, const std::string& title, const std::string& text, int icon)
{
	CHECK_UI_THREAD;
	wxMessageDialog* d3 = new wxMessageDialog(parent, towxstring(text), towxstring(title), wxOK | icon);
	d3->ShowModal();
	d3->Destroy();
}

bool run_show_error(wxWindow* parent, const std::string& title, const std::string& text, std::function<void()> fn)
{
	try {
		fn();
		return false;
	} catch(std::exception& e) {
		std::string err = e.what();
		std::string _title = title;
		std::string _text = (text == "") ? err : (text + ": " + err);
		runuifun([parent, _title, _text]() {
			show_message_ok(parent, _title, _text, wxICON_EXCLAMATION);
		});
		return true;
	}
}

void show_exception(wxWindow* parent, const std::string& title, const std::string& text, std::exception& e)
{
	CHECK_UI_THREAD;
	std::string err = e.what();
	std::string _title = title;
	std::string _text = (text == "") ? err : (text + ": " + err);
	show_message_ok(parent, _title, _text, wxICON_EXCLAMATION);
}

void show_exception_any(wxWindow* parent, const std::string& title, const std::string& text, std::exception& e)
{
	std::string err = e.what();
	std::string _title = title;
	std::string _text = (text == "") ? err : (text + ": " + err);
	runuifun([parent, _title, _text]() {
		show_message_ok(parent, _title, _text, wxICON_EXCLAMATION);
	});
}

void _check_ui_thread(const char* file, int line)
{
	if(ui_thread == threads::this_id())
		return;
	std::cerr << "UI routine running in wrong thread at " << file << ":" << line << std::endl;
}
