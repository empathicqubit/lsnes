#include "cmdhelp/loadlib.hpp"
#include "core/command.hpp"
#include "core/dispatch.hpp"
#include "core/instance.hpp"
#include "core/loadlib.hpp"
#include "core/messages.hpp"
#include "core/misc.hpp"
#include "core/rom.hpp"
#include "interface/c-interface.hpp"
#include "interface/romtype.hpp"
#include "library/command.hpp"
#include "library/directory.hpp"
#include "library/filelist.hpp"
#include "library/loadlib.hpp"
#include "library/opus.hpp"
#include "library/running-executable.hpp"

#include <stdexcept>
#include <sstream>
#include <dirent.h>

namespace
{
	std::map<unsigned, loadlib::module*> modules;
	unsigned next_mod = 0;

	text get_name(text path)
	{
#if defined(_WIN32) || defined(_WIN64)
		const char32_t* sep = U"\\/";
#else
		const char32_t* sep = U"/";
#endif
		size_t p = path.find_last_of(sep);
		text name;
		if(p >= path.length())
			name = path;
		else
			name = path.substr(p + 1);
		return name;
	}

	text get_user_library_dir()
	{
		return get_config_path() + "/autoload";
	}

	text get_system_library_dir()
	{
		text path;
		try {
			path = running_executable();
		} catch(...) {
			return "";
		}
#if __WIN32__ || __WIN64__
		const char32_t* sep = U"\\/";
#else
		const char32_t* sep = U"/";
#endif
		size_t p = path.find_last_of(sep);
		if(p >= path.length())
			path = ".";
		else if(p == 0)
			path = "/";
		else
			path = path.substr(0, p);
#if !__WIN32__ && !__WIN64__
		//If executable is in /bin, translate library path to corresponding /lib/lsnes.
		regex_results r = regex("(.*)/bin", path);
		if(r) path = r[1] + "/lib/lsnes";
		else
#endif
			path = path + "/plugins";
		return path;
	}
}

void handle_post_loadlibrary()
{
	if(new_core_flag) {
		new_core_flag = false;
		core_core::initialize_new_cores();
		notify_new_core();
	}
}

void with_loaded_library(const loadlib::module& l)
{
	try {
		if(!opus::libopus_loaded())
			opus::load_libopus(l);
	} catch(...) {
		//This wasn't libopus.
	}
	try {
		module_loading = &l;
		try_init_c_module(l);
		module_loading = NULL;
	} catch(...) {
		module_loading = NULL;
		//Ignored.
	}
	messages << "Loaded library '" << l.get_libname() << "'" << std::endl;
	modules[next_mod++] = const_cast<loadlib::module*>(&l);
}

bool with_unloaded_library(loadlib::module& l)
{
	//Check that this module is not needed.
	if(!CORE().rom->get_internal_rom_type().safe_to_unload(l)) {
		messages << "Current core is from this module, can't unload" << std::endl;
		return false;
	}
	try {
		try_uninit_c_module(l);
	} catch(...) {
	}
	messages << "Unloading library '" << l.get_libname() << "'" << std::endl;
	//Spot removed cores.
	notify_new_core();
	delete &l;
	notify_new_core();	//In case only unload made it go away.
	return true;
}

namespace
{
	void load_libraries(std::set<text> libs, bool system,
		void(*on_error)(const text& libname, const text& err, bool system))
	{
		std::set<text> blacklist;
		std::set<text> killlist;
		//System plugins can't be killlisted nor blacklisted.
		if(!system) {
			filelist _blacklist(get_user_library_dir() + "/blacklist", get_user_library_dir());
			filelist _killlist(get_user_library_dir() + "/killlist", get_user_library_dir());
			killlist = _killlist.enumerate();
			blacklist = _blacklist.enumerate();
			//Try to kill the libs that don't exist anymore.
			for(auto i : libs)
				if(killlist.count(get_name(i)))
					remove(i.c_str());
			killlist = _killlist.enumerate();
			//All killlisted plugins are automatically blacklisted.
			for(auto i : killlist)
				blacklist.insert(i);
		}

		text extension = loadlib::library::extension();
		for(auto i : libs) {
			if(i.length() < extension.length() + 1)
				continue;
			if(i[i.length() - extension.length() - 1] != '.')
				continue;
			text tmp = i;
			if(tmp.substr(i.length() - extension.length()) != extension)
				continue;
			if(blacklist.count(get_name(i)))
				continue;
			try {
				with_loaded_library(*new loadlib::module(loadlib::library(i)));
			} catch(std::exception& e) {
				text x = "Can't load '" + i + "': " + e.what();

				if(on_error)
					on_error(get_name(i), e.what(), system);
				messages << x << std::endl;
			}
		}
	}

	command::fnptr<command::arg_filename> CMD_load_library(lsnes_cmds, CLOADLIB::load,
		[](command::arg_filename args) throw(std::bad_alloc, std::runtime_error) {
			with_loaded_library(*new loadlib::module(loadlib::library(args)));
			handle_post_loadlibrary();
		});

	command::fnptr<const text&> CMD_unload_library(lsnes_cmds, CLOADLIB::unload,
		[](const text& args) throw(std::bad_alloc, std::runtime_error) {
			unsigned libid = parse_value<unsigned>(args);
			if(!modules.count(libid))
				throw std::runtime_error("No such library loaded");
			if(with_unloaded_library(*modules[libid]))
				modules.erase(libid);
		});

	command::fnptr<> CMD_list_library(lsnes_cmds, CLOADLIB::list,
		[]() throw(std::bad_alloc, std::runtime_error) {
			for(auto i : modules)
				messages << "#" << i.first << " [" << i.second->get_libname() << "]" << std::endl;
		});
}

void autoload_libraries(void(*on_error)(const text& libname, const text& err, bool system))
{
	try {
		auto libs = directory::enumerate(get_user_library_dir(), ".*");
		load_libraries(libs, false, on_error);
	} catch(std::exception& e) {
		messages << e.what() << std::endl;
	}
	try {
		auto libs = directory::enumerate(get_system_library_dir(), ".*");
		load_libraries(libs, true, on_error);
	} catch(std::exception& e) {
		messages << e.what() << std::endl;
	}
	handle_post_loadlibrary();
}
