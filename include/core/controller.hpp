#ifndef _controller__hpp__included__
#define _controller__hpp__included__

#include <string>
#include <map>
#include "library/dispatch.hpp"
#include "library/command.hpp"
#include "library/text.hpp"

struct project_info;
struct controller_state;
struct emu_framebuffer;
struct emulator_dispatch;
struct lua_state;
struct core_core;
namespace keyboard { class invbind; }
namespace keyboard { class ctrlrkey; }
namespace keyboard { class mapper; }
namespace keyboard { class keyboard; }
namespace portctrl { struct type; }
namespace portctrl { struct controller; }

class button_mapping
{
public:
	struct controller_bind
	{
		text cclass;
		unsigned number;
		text name;
		int mode;	//0 => Button, 1 => Axis pair, 2 => Single axis.
		bool xrel;
		bool yrel;
		unsigned control1;
		unsigned control2;	//Axis only, UINT_MAX if not valid.
		int16_t rmin;
		int16_t rmax;
		bool centered;
	};
	struct active_bind
	{
		unsigned port;
		unsigned controller;
		struct controller_bind bind;
	};
	struct controller_triple
	{
		text cclass;
		unsigned port;
		unsigned controller;
		bool operator<(const struct controller_triple& t) const throw()
		{
			if(cclass < t.cclass) return true;
			if(cclass > t.cclass) return false;
			if(port < t.port) return true;
			if(port > t.port) return false;
			if(controller < t.controller) return true;
			if(controller > t.controller) return false;
			return false;
		}
		bool operator==(const struct controller_triple& t) const throw()
		{
			return (cclass == t.cclass && port == t.port && controller == t.controller);
		}
	};
/**
 * Ctor.
 */
	button_mapping(controller_state& _controls, keyboard::mapper& mapper, keyboard::keyboard& keyboard,
		emu_framebuffer& fbuf, emulator_dispatch& _dispatch, lua_state& _lua2, command::group& _cmd);
/**
 * Dtor.
 */
	~button_mapping();
/**
 * Reread active buttons.
 */
	void reread();
/**
 * Reinitialize buttons.
 */
	void reinit();
/**
 * Load macros.
 */
	void load(controller_state& ctrlstate);
/**
 * Load macros (from project).
 */
	void load(controller_state& ctrlstate, project_info& pinfo);
/**
 * Cleanup memory used.
 */
	void cleanup();
/**
 * Lookup button by name.
 */
	std::pair<int, int> byname(const text& name);
/**
 * Map of button keys.
 */
	std::map<text, text> button_keys;
private:
	void do_analog_action(const text& a);
	void do_autofire_action(const text& a, int mode);
	void do_action(const text& name, short state, int mode);
	void promote_key(keyboard::ctrlrkey& k);
	void add_button(const text& name, const controller_bind& binding);
	void process_controller(portctrl::controller& controller, unsigned number);
	void process_controller(std::map<text, unsigned>& allocated,
		std::map<controller_triple, unsigned>& assigned,  portctrl::controller& controller, unsigned port,
		unsigned number_in_port);
	void process_port(std::map<text, unsigned>& allocated,
		std::map<controller_triple, unsigned>& assigned, unsigned port, portctrl::type& ptype);
	void init();
	bool check_button_active(const text& name);
	void do_button_action(const text& name, short newstate, int mode);
	void send_analog(const text& name, int32_t x, int32_t y);
	std::map<text, keyboard::invbind*> macro_binds;
	std::map<text, keyboard::invbind*> macro_binds2;
	std::map<text, controller_bind> all_buttons;
	std::map<text, active_bind> active_buttons;
	std::map<text, keyboard::ctrlrkey*> added_keys;
	std::set<core_core*> cores_done;
	controller_state& controls;
	keyboard::mapper& mapper;
	keyboard::keyboard& keyboard;
	emu_framebuffer& fbuf;
	emulator_dispatch& edispatch;
	lua_state& lua2;
	command::group& cmd;
	struct dispatch::target<> ncore;
	command::_fnptr<const text&> button_p;
	command::_fnptr<const text&> button_r;
	command::_fnptr<const text&> button_h;
	command::_fnptr<const text&> button_t;
	command::_fnptr<const text&> button_d;
	command::_fnptr<const text&> button_ap;
	command::_fnptr<const text&> button_ar;
	command::_fnptr<const text&> button_at;
	command::_fnptr<const text&> button_a;
};



#endif
