#ifdef CORETYPE_GAMBATTE
#include "lsnes.hpp"
#include <sstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "core/audioapi.hpp"
#include "core/misc.hpp"
#include "core/command.hpp"
#include "core/controllerframe.hpp"
#include "core/dispatch.hpp"
#include "core/framebuffer.hpp"
#include "core/window.hpp"
#include "interface/callbacks.hpp"
#include "interface/romtype.hpp"
#include "library/pixfmt-rgb32.hpp"
#include "library/string.hpp"
#include "library/portfn.hpp"
#include "library/serialization.hpp"
#include "library/minmax.hpp"
#include "library/framebuffer.hpp"
#define HAVE_CSTDINT
#include "libgambatte/include/gambatte.h"

#define SAMPLES_PER_FRAME 35112

#define LOGICAL_BUTTON_LEFT 0
#define LOGICAL_BUTTON_RIGHT 1
#define LOGICAL_BUTTON_UP 2
#define LOGICAL_BUTTON_DOWN 3
#define LOGICAL_BUTTON_A 4
#define LOGICAL_BUTTON_B 5
#define LOGICAL_BUTTON_SELECT 6
#define LOGICAL_BUTTON_START 7

namespace
{
	bool do_reset_flag = false;
	core_type* internal_rom = NULL;
	extern core_type type_dmg;
	extern core_type type_gbc;
	extern core_type type_gbc_gba;
	bool rtc_fixed;
	time_t rtc_fixed_val;
	gambatte::GB* instance;
	unsigned frame_overflow = 0;
	std::vector<unsigned char> romdata;
	uint32_t cover_fbmem[480 * 432];
	uint32_t primary_framebuffer[160*144];
	uint32_t accumulator_l = 0;
	uint32_t accumulator_r = 0;
	unsigned accumulator_s = 0;

	core_setting_group gambatte_settings;

	//Framebuffer.
	struct framebuffer_info cover_fbinfo = {
		&_pixel_format_rgb32,		//Format.
		(char*)cover_fbmem,		//Memory.
		480, 432, 1920,			//Physical size.
		480, 432, 1920,			//Logical size.
		0, 0				//Offset.
	};

	//////////////////////////// PORT ////////////////////////////
	const char* button_symbols = "ABsSrlud";
	port_controller_button gamepad_A = {port_controller_button::TYPE_BUTTON, "A"};
	port_controller_button gamepad_B = {port_controller_button::TYPE_BUTTON, "B"};
	port_controller_button gamepad_s = {port_controller_button::TYPE_BUTTON, "select"};
	port_controller_button gamepad_S = {port_controller_button::TYPE_BUTTON, "start"};
	port_controller_button gamepad_u = {port_controller_button::TYPE_BUTTON, "up"};
	port_controller_button gamepad_d = {port_controller_button::TYPE_BUTTON, "down"};
	port_controller_button gamepad_l = {port_controller_button::TYPE_BUTTON, "left"};
	port_controller_button gamepad_r = {port_controller_button::TYPE_BUTTON, "right"};
	port_controller_button* gamepad_buttons[] = {
		&gamepad_A, &gamepad_B, &gamepad_s, &gamepad_S,
		&gamepad_r, &gamepad_l, &gamepad_u, &gamepad_d
	};
	port_controller_button* none_buttons[] = {};
	port_controller system_controller = {"system", "system", 0, none_buttons};
	port_controller gb_buttons = {"gb", "gamepad", 8, gamepad_buttons};
	port_controller* gambatte_controllers[] = {&system_controller, &gb_buttons};
	port_controller_set _controller_info = {2, gambatte_controllers};

	struct porttype_system : public port_type
	{
		porttype_system() : port_type("<SYSTEM>", "<SYSTEM>", 9999, 2)
		{
			write = [](unsigned char* buffer, unsigned idx, unsigned ctrl, short x) -> void {
				if(idx < 2 && ctrl < 8)
					if(x)
						buffer[idx] |= (1 << ctrl);
					else
						buffer[idx] &= ~(1 << ctrl);
			};
			read = [](const unsigned char* buffer, unsigned idx, unsigned ctrl) -> short {
				short y = 0;
				if(idx < 2 && ctrl < 8)
					y = ((buffer[idx] >> ctrl) & 1) ? 1 : 0;
				return y;
			};
			display = [](const unsigned char* buffer, unsigned idx, char* buf) -> void {
				if(idx > 1)
					sprintf(buf, "");
				else if(idx == 1) {
					for(unsigned i = 0; i < 8; i++)
						buf[i] = ((buffer[1] & (1 << i)) != 0) ? button_symbols[i] : '-';
					buf[8] = '\0';
				} else
					sprintf(buf, "%c%c", ((buffer[0] & 1) ? 'F' : '.'),
						((buffer[0] & 2) ? 'R' : '.'));
			};
			serialize = [](const unsigned char* buffer, char* textbuf) -> size_t {
				char tmp[128];
				sprintf(tmp, "%c%c|", ((buffer[0] & 1) ? 'F' : '.'), ((buffer[0] & 2) ? 'R' : '.'));
				for(unsigned i = 0; i < 8; i++)
					tmp[i + 3] = ((buffer[1] & (1 << i)) != 0) ? button_symbols[i] : '.';
				tmp[11] = 0;
				size_t len = strlen(tmp);
				memcpy(textbuf, tmp, len);
				return len;
			};
			deserialize = [](unsigned char* buffer, const char* textbuf) -> size_t {
				memset(buffer, 0, 2);
				size_t ptr = 0;
				if(read_button_value(textbuf, ptr))
					buffer[0] |= 1;
				if(read_button_value(textbuf, ptr))
					buffer[0] |= 2;
				skip_rest_of_field(textbuf, ptr, true);
				for(unsigned i = 0; i < 8; i++)
					if(read_button_value(textbuf, ptr))
						buffer[1] |= (1 << i);
				skip_rest_of_field(textbuf, ptr, false);
				return ptr;
			};
			legal = generic_port_legal<1>;
			used_indices = [](unsigned c) -> unsigned { return c ? ((c == 1) ? 8 : 0) : 2; };
			controller_info = &_controller_info;
		}
	} psystem;

	port_type* port_types[] = { &psystem, NULL };










	time_t walltime_fn()
	{
		if(rtc_fixed)
			return rtc_fixed_val;
		if(ecore_callbacks)
			return ecore_callbacks->get_time();
		else
			return time(0);
	}

	class myinput : public gambatte::InputGetter
	{
	public:
		unsigned operator()()
		{
			unsigned v = 0;
			for(unsigned i = 0; i < 8; i++) {
				if(ecore_callbacks->get_input(0, 1, i))
					v |= (1 << i);
			}
			return v;
		};
	} getinput;

	void basic_init()
	{
		static bool done = false;
		if(done)
			return;
		done = true;
		instance = new gambatte::GB;
		instance->setInputGetter(&getinput);
		instance->set_walltime_fn(walltime_fn);
	}

	int load_rom_common(core_romimage* img, unsigned flags, uint64_t rtc_sec, uint64_t rtc_subsec,
		core_type* inttype)
	{
		basic_init();
		const char* markup = img[0].markup;
		int flags2 = 0;
		if(markup) {
			flags2 = atoi(markup);
			flags2 &= 4;
		}
		flags |= flags2;
		const unsigned char* data = img[0].data;
		size_t size = img[0].size;

		//Reset it really.
		instance->~GB();
		memset(instance, 0, sizeof(gambatte::GB));
		new(instance) gambatte::GB;
		instance->setInputGetter(&getinput);
		instance->set_walltime_fn(walltime_fn);
		memset(primary_framebuffer, 0, sizeof(primary_framebuffer));
		frame_overflow = 0;
		
		rtc_fixed = true;
		rtc_fixed_val = rtc_sec;
		instance->load(data, size, flags);
		rtc_fixed = false;
		romdata.resize(size);
		memcpy(&romdata[0], data, size);
		internal_rom = inttype;
		do_reset_flag = false;
		return 1;
	}

	port_index_triple t(unsigned p, unsigned c, unsigned i, bool nl)
	{
		port_index_triple x;
		x.port = p;
		x.controller = c;
		x.control = i;
		x.marks_nonlag = nl;
		return x;
	}

	controller_set _controllerconfig(std::map<std::string, std::string>& settings)
	{
		std::map<std::string, std::string> _settings = settings;
		controller_set r;
		r.ports.push_back(&psystem);
		for(unsigned i = 0; i < 4; i++)
			r.portindex.indices.push_back(t(0, 0, i, false));
		for(unsigned i = 0; i < 8; i++)
			r.portindex.indices.push_back(t(0, 1, i, true));
		r.portindex.logical_map.push_back(std::make_pair(0, 1));
		r.portindex.pcid_map.push_back(std::make_pair(0, 1));
		return r;
	}

	std::pair<uint64_t, uint64_t> gambatte_bus_map()
	{
		return std::make_pair(0, 0);
	}

	std::list<core_vma_info> get_VMAlist()
	{
		std::list<core_vma_info> vmas;
		if(!internal_rom)
			return vmas;
		core_vma_info sram;
		core_vma_info wram;
		core_vma_info vram;
		core_vma_info ioamhram;
		core_vma_info rom;

		auto g = instance->getSaveRam();
		sram.name = "SRAM";
		sram.base = 0x20000;
		sram.size = g.second;
		sram.backing_ram = g.first;
		sram.native_endian = false;
		sram.readonly = false;
		sram.iospace_rw = NULL;

		auto g2 = instance->getWorkRam();
		wram.name = "WRAM";
		wram.base = 0;
		wram.size = g2.second;
		wram.backing_ram = g2.first;
		wram.native_endian = false;
		wram.readonly = false;
		wram.iospace_rw = NULL;

		auto g3 = instance->getVideoRam();
		vram.name = "VRAM";
		vram.base = 0x10000;
		vram.size = g3.second;
		vram.backing_ram = g3.first;
		vram.native_endian = false;
		vram.readonly = false;
		vram.iospace_rw = NULL;

		auto g4 = instance->getIoRam();
		ioamhram.name = "IOAMHRAM";
		ioamhram.base = 0x18000;
		ioamhram.size = g4.second;
		ioamhram.backing_ram = g4.first;
		ioamhram.native_endian = false;
		ioamhram.readonly = false;
		ioamhram.iospace_rw = NULL;

		rom.name = "ROM";
		rom.base = 0x80000000;
		rom.size = romdata.size();
		rom.backing_ram = (void*)&romdata[0];
		rom.native_endian = false;
		rom.readonly = true;
		rom.iospace_rw = NULL;

		if(sram.size)
			vmas.push_back(sram);
		vmas.push_back(wram);
		vmas.push_back(rom);
		vmas.push_back(vram);
		vmas.push_back(ioamhram);
		return vmas;
	}

	std::set<std::string> srams()
	{
		std::set<std::string> s;
		if(!internal_rom)
			return s;
		auto g = instance->getSaveRam();
		if(g.second)
			s.insert("main");
		s.insert("rtc");
		return s;
	}

	unsigned world_compatible[] = {0, UINT_MAX};
	core_region_params _region_world = {
		"world", "World", 1, 0, false, {35112, 2097152, 16742706, 626688}, world_compatible
	};
	core_romimage_info_params _image_rom = {
		"rom", "Cartridge ROM", 1, 0, 0
	};

	core_region region_world(_region_world);
	core_romimage_info image_rom_dmg(_image_rom);
	core_romimage_info image_rom_gbc(_image_rom);
	core_romimage_info image_rom_gbca(_image_rom);
	core_region* regions_gambatte[] = {&region_world, NULL};
	core_romimage_info* dmg_images[] = {&image_rom_dmg, NULL};
	core_romimage_info* gbc_images[] = {&image_rom_gbc, NULL};
	core_romimage_info* gbca_images[] = {&image_rom_gbca, NULL};


	core_core_params _gambatte_core = {
		//Get core identifier.
		[]() -> std::string { return "libgambatte "+gambatte::GB::version(); },
		//Set core region.
		[](core_region& region) -> bool { return (&region == &region_world); },
		//Get video rate
		[]() -> std::pair<uint32_t, uint32_t> { return std::make_pair(262144, 4389); },
		//Get audio rate.
		[]() -> std::pair<uint32_t, uint32_t> { return std::make_pair(32768, 1); },
		//Get SNES CPU/APU rate (N/A).
		NULL,
		//Store SRAM.
		[]() -> std::map<std::string, std::vector<char>> 
		{
			std::map<std::string, std::vector<char>> s;
			if(!internal_rom)
				return s;
			auto g = instance->getSaveRam();
			s["main"].resize(g.second);
			memcpy(&s["main"][0], g.first, g.second);
			s["rtc"].resize(8);
			time_t timebase = instance->getRtcBase();
			for(size_t i = 0; i < 8; i++)
				s["rtc"][i] = ((unsigned long long)timebase >> (8 * i));
			return s;
		},
		//Load SRAM.
		[](std::map<std::string, std::vector<char>>& sram) -> void {
			if(!internal_rom)
				return;
			std::vector<char> x = sram.count("main") ? sram["main"] : std::vector<char>();
			std::vector<char> x2 = sram.count("rtc") ? sram["rtc"] : std::vector<char>();
			auto g = instance->getSaveRam();
			if(x.size()) {
				if(x.size() != g.second)
					messages << "WARNING: SRAM 'main': Loaded " << x.size()
						<< " bytes, but the SRAM is " << g.second << "." << std::endl;
				memcpy(g.first, &x[0], min(x.size(), g.second));
			}
			if(x2.size()) {
				time_t timebase = 0;
				for(size_t i = 0; i < 8 && i < x2.size(); i++)
					timebase |= (unsigned long long)(unsigned char)x2[i] << (8 * i);
				instance->setRtcBase(timebase);
			}
		},
		//Serialize core state
		[](std::vector<char>& out) -> void {
			if(!internal_rom)
				throw std::runtime_error("Can't save without ROM");
			instance->saveState(out);
			size_t osize = out.size();
			out.resize(osize + 4 * sizeof(primary_framebuffer) / sizeof(primary_framebuffer[0]));
			for(size_t i = 0; i < sizeof(primary_framebuffer) / sizeof(primary_framebuffer[0]); i++)
				write32ube(&out[osize + 4 * i], primary_framebuffer[i]);
			out.push_back(frame_overflow >> 8);
			out.push_back(frame_overflow);
		},
		//Unserialize core state
		[](const char* in, size_t insize) -> void {
			if(!internal_rom)
				throw std::runtime_error("Can't load without ROM");
			size_t foffset = insize - 2 - 4 * sizeof(primary_framebuffer) /
				sizeof(primary_framebuffer[0]);
			std::vector<char> tmp;
			tmp.resize(foffset);
			memcpy(&tmp[0], in, foffset);
			instance->loadState(tmp);
			for(size_t i = 0; i < sizeof(primary_framebuffer) / sizeof(primary_framebuffer[0]); i++)
				primary_framebuffer[i] = read32ube(&in[foffset + 4 * i]);

			unsigned x1 = (unsigned char)in[insize - 2];
			unsigned x2 = (unsigned char)in[insize - 1];
			frame_overflow = x1 * 256 + x2;
			do_reset_flag = false;
		},
		//Get region.
		[]() -> core_region& { return region_world; },
		//Power the core.
		[]() -> void {},
		//Unload cartridge.
		[]() -> void {},
		//Get scale factors
		[](uint32_t width, uint32_t height) -> std::pair<uint32_t, uint32_t> {
			return std::make_pair(max(512 / width, (uint32_t)1), max(448 / height, (uint32_t)1));
		},
		//Install handler
		[]() -> void  {},
		//Uninstall handler.
		[]() -> void {},
		//Emulate frame.
		[]() -> void {
			if(!internal_rom)
				return;
			int16_t reset = ecore_callbacks->set_input(0, 0, 1, do_reset_flag ? 1 : 0);
			if(reset) {
				instance->reset();
				messages << "GB(C) reset" << std::endl;
			}
			do_reset_flag = false;

			uint32_t samplebuffer[SAMPLES_PER_FRAME + 2064];
			while(true) {
				int16_t soundbuf[(SAMPLES_PER_FRAME + 63) / 32 + 66];
				size_t emitted = 0;
				unsigned samples_emitted = SAMPLES_PER_FRAME - frame_overflow;
				long ret = instance->runFor(primary_framebuffer, 160, samplebuffer, samples_emitted);
				for(unsigned i = 0; i < samples_emitted; i++) {
					uint32_t l = (int32_t)(int16_t)(samplebuffer[i]) + 32768;
					uint32_t r = (int32_t)(int16_t)(samplebuffer[i] >> 16) + 32768;
					accumulator_l += l;
					accumulator_r += r;
					accumulator_s++;
					if((accumulator_s & 63) == 0) {
						int16_t l2 = (accumulator_l >> 6) - 32768;
						int16_t r2 = (accumulator_r >> 6) - 32768;
						soundbuf[emitted++] = l2;
						soundbuf[emitted++] = r2;
						information_dispatch::do_sample(l2, r2);
						accumulator_l = accumulator_r = 0;
						accumulator_s = 0;
					}
				}
				audioapi_submit_buffer(soundbuf, emitted / 2, true, 32768);
				ecore_callbacks->timer_tick(samples_emitted, 2097152);
				frame_overflow += samples_emitted;
				if(frame_overflow >= SAMPLES_PER_FRAME) {
					frame_overflow -= SAMPLES_PER_FRAME;
					break;
				}
			}
			framebuffer_info inf;
			inf.type = &_pixel_format_rgb32;
			inf.mem = const_cast<char*>(reinterpret_cast<const char*>(primary_framebuffer));
			inf.physwidth = 160;
			inf.physheight = 144;
			inf.physstride = 640;
			inf.width = 160;
			inf.height = 144;
			inf.stride = 640;
			inf.offset_x = 0;
			inf.offset_y = 0;

			framebuffer_raw ls(inf);
			ecore_callbacks->output_frame(ls, 262144, 4389);
		},
		//Run to save.
		[]() -> void {},
		//Get poll flag.
		[]() -> unsigned { return 2; },
		//Set poll flag.
		[](unsigned pflag) -> void {},
		//Request reset.
		[](long delay) -> void { do_reset_flag = true; },
		//Port types.
		port_types,
		//Cover page.
		[]() -> framebuffer_raw& {
			static framebuffer_raw x(cover_fbinfo);
			return x;
		}
	};

	core_core gambatte_core(_gambatte_core);
	
	core_type_params  _type_dmg = {
		"dmg", "Game Boy", 1, 1,
		[](core_romimage* img, std::map<std::string, std::string>& settings, uint64_t rtc_sec,
			uint64_t rtc_subsec) -> int {
			return load_rom_common(img, gambatte::GB::FORCE_DMG, rtc_sec, rtc_subsec, &type_dmg);
		},
		_controllerconfig, "gb;dmg", NULL, regions_gambatte, dmg_images, &gambatte_settings, &gambatte_core,
		gambatte_bus_map, get_VMAlist, srams
	};
	core_type_params  _type_gbc = {
		"gbc", "Game Boy Color", 0, 1,
		[](core_romimage* img, std::map<std::string, std::string>& settings, uint64_t rtc_sec,
			uint64_t rtc_subsec) -> int {
			return load_rom_common(img, 0, rtc_sec, rtc_subsec, &type_gbc);
		},
		_controllerconfig, "gbc;cgb", NULL, regions_gambatte, gbc_images, &gambatte_settings, &gambatte_core,
		gambatte_bus_map, get_VMAlist, srams
	};
	core_type_params  _type_gbc_gba = {
		"gbc_gba", "Game Boy Color (GBA)", 2, 1,
		[](core_romimage* img, std::map<std::string, std::string>& settings, uint64_t rtc_sec,
			uint64_t rtc_subsec) -> int {
			return load_rom_common(img, gambatte::GB::GBA_CGB, rtc_sec, rtc_subsec, &type_gbc_gba);
		},
		_controllerconfig, "", NULL, regions_gambatte, gbca_images, &gambatte_settings, &gambatte_core,
		gambatte_bus_map, get_VMAlist, srams
	};

	core_type type_dmg(_type_dmg);
	core_type type_gbc(_type_gbc);
	core_type type_gbc_gba(_type_gbc_gba);
	core_sysregion sr1("gdmg", type_dmg, region_world);
	core_sysregion sr2("ggbc", type_gbc, region_world);
	core_sysregion sr3("ggbca", type_gbc_gba, region_world);

	std::vector<char> cmp_save;

	function_ptr_command<> cmp_save1(lsnes_cmd, "set-cmp-save", "", "\n", []() throw(std::bad_alloc,
		std::runtime_error) {
		if(!internal_rom)
			return;
		instance->saveState(cmp_save);
	});

	function_ptr_command<> cmp_save2(lsnes_cmd, "do-cmp-save", "", "\n", []() throw(std::bad_alloc,
		std::runtime_error) {
		std::vector<char> x;
		if(!internal_rom)
			return;
		instance->saveState(x, cmp_save);
	});

	//Init the fbmem.
	struct fbmem_initializer
	{
		fbmem_initializer()
		{
			for(size_t i = 0; i < sizeof(cover_fbmem)/sizeof(cover_fbmem[0]); i++)
				cover_fbmem[i] = 0x00FF0000;
		}
	} fbmem_initializer;
}

#endif
