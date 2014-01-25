#include "core/command.hpp"
#include "lua/internal.hpp"
#include "core/debug.hpp"
#include "core/memorymanip.hpp"
#include "core/memorywatch.hpp"
#include "core/moviedata.hpp"
#include "core/moviefile.hpp"
#include "core/rom.hpp"
#include "library/sha256.hpp"
#include "library/string.hpp"
#include "library/minmax.hpp"
#include "library/hex.hpp"
#include "library/int24.hpp"

namespace
{
	uint64_t get_vmabase(lua::state& L, const std::string& vma)
	{
		for(auto i : lsnes_memory.get_regions())
			if(i->name == vma)
				return i->base;
		throw std::runtime_error("No such VMA");
	}

	uint64_t get_read_address(lua::state& L, int& base, const char* fn)
	{
		uint64_t vmabase = 0;
		if(L.type(base) == LUA_TSTRING) {
			vmabase = get_vmabase(L, L.get_string(base, fn));
			base++;
		}
		uint64_t addr = L.get_numeric_argument<uint64_t>(base++, fn) + vmabase;
		return addr;
	}

	template<typename T, T (memory_space::*rfun)(uint64_t addr)>
	class lua_read_memory : public lua::function
	{
	public:
		lua_read_memory(const std::string& name) : lua::function(lua_func_misc, name) {}
		int invoke(lua::state& L)
		{
			int base = 1;
			uint64_t addr = get_read_address(L, base, "lua_read_memory");
			L.pushnumber(static_cast<T>((lsnes_memory.*rfun)(addr)));
			return 1;
		}
	};

	template<typename T, bool (memory_space::*wfun)(uint64_t addr, T value)>
	class lua_write_memory : public lua::function
	{
	public:
		lua_write_memory(const std::string& name) : lua::function(lua_func_misc, name) {}
		int invoke(lua::state& L)
		{
			int base = 1;
			uint64_t addr = get_read_address(L, base, "lua_write_memory");
			T value = L.get_numeric_argument<T>(base + 2, fname.c_str());
			(lsnes_memory.*wfun)(addr, value);
			return 0;
		}
	};

	class mmap_base
	{
	public:
		~mmap_base() {}
		virtual void read(lua::state& L, uint64_t addr) = 0;
		virtual void write(lua::state& L, uint64_t addr) = 0;
	};


	template<typename T, T (memory_space::*rfun)(uint64_t addr), bool (memory_space::*wfun)(uint64_t addr,
		T value)>
	class lua_mmap_memory_helper : public mmap_base
	{
	public:
		~lua_mmap_memory_helper() {}
		void read(lua::state& L, uint64_t addr)
		{
			L.pushnumber(static_cast<T>((lsnes_memory.*rfun)(addr)));
		}

		void write(lua::state& L, uint64_t addr)
		{
			T value = L.get_numeric_argument<T>(3, "aperture(write)");
			(lsnes_memory.*wfun)(addr, value);
		}
	};
}

class lua_mmap_struct
{
public:
	lua_mmap_struct(lua::state& L);

	~lua_mmap_struct()
	{
	}

	int index(lua::state& L, const std::string& fname)
	{
		const char* c = L.tostring(2);
		if(!c) {
			L.pushnil();
			return 1;
		}
		std::string c2(c);
		if(!mappings.count(c2)) {
			L.pushnil();
			return 1;
		}
		auto& x = mappings[c2];
		x.first->read(L, x.second);
		return 1;
	}
	int newindex(lua::state& L, const std::string& fname)
	{
		const char* c = L.tostring(2);
		if(!c)
			return 0;
		std::string c2(c);
		if(!mappings.count(c2))
			return 0;
		auto& x = mappings[c2];
		x.first->write(L, x.second);
		return 0;
	}
	int map(lua::state& L, const std::string& fname);
	std::string print()
	{
		size_t s = mappings.size();
		return (stringfmt() << s << " " << ((s != 1) ? "mappings" : "mapping")).str();
	}
private:
	std::map<std::string, std::pair<mmap_base*, uint64_t>> mappings;
};

namespace
{
	int aperture_read_fun(lua_State* _L)
	{
		lua::state& L = *reinterpret_cast<lua::state*>(lua_touserdata(_L, lua_upvalueindex(4)));
		uint64_t base = L.tonumber(lua_upvalueindex(1));
		uint64_t size = 0xFFFFFFFFFFFFFFFFULL;
		if(L.type(lua_upvalueindex(2)) == LUA_TNUMBER)
			size = L.tonumber(lua_upvalueindex(2));
		mmap_base* fn = reinterpret_cast<mmap_base*>(L.touserdata(lua_upvalueindex(3)));
		uint64_t addr = L.get_numeric_argument<uint64_t>(2, "aperture(read)");
		if(addr > size || addr + base < addr) {
			L.pushnumber(0);
			return 1;
		}
		addr += base;
		fn->read(L, addr);
		return 1;
	}

	int aperture_write_fun(lua_State* _L)
	{
		lua::state& L = *reinterpret_cast<lua::state*>(lua_touserdata(_L, lua_upvalueindex(4)));
		uint64_t base = L.tonumber(lua_upvalueindex(1));
		uint64_t size = 0xFFFFFFFFFFFFFFFFULL;
		if(L.type(lua_upvalueindex(2)) == LUA_TNUMBER)
			size = L.tonumber(lua_upvalueindex(2));
		mmap_base* fn = reinterpret_cast<mmap_base*>(L.touserdata(lua_upvalueindex(3)));
		uint64_t addr = L.get_numeric_argument<uint64_t>(2, "aperture(write)");
		if(addr > size || addr + base < addr)
			return 0;
		addr += base;
		fn->write(L, addr);
		return 0;
	}

	void aperture_make_fun(lua::state& L, uint64_t base, uint64_t size, mmap_base& type)
	{
		L.newtable();
		L.newtable();
		L.pushstring( "__index");
		L.pushnumber(base);
		if(!(size + 1))
			L.pushnil();
		else
			L.pushnumber(size);
		L.pushlightuserdata(&type);
		L.pushlightuserdata(&L);
		L.pushcclosure(aperture_read_fun, 4);
		L.settable(-3);
		L.pushstring("__newindex");
		L.pushnumber(base);
		if(!(size + 1))
			L.pushnil();
		else
			L.pushnumber(size);
		L.pushlightuserdata(&type);
		L.pushlightuserdata(&L);
		L.pushcclosure(aperture_write_fun, 4);
		L.settable(-3);
		L.setmetatable(-2);
	}

	struct lua_debug_callback
	{
		uint64_t addr;
		debug_type type;
		debug_handle h;
		bool dead;
		const void* lua_fn;
		static int dtor(lua_State* L)
		{
			lua_debug_callback* D = (lua_debug_callback*)lua_touserdata(L, 1);
			return D->_dtor(L);
		}
		int _dtor(lua_State* L);
	};
	std::map<uint64_t, std::list<lua_debug_callback*>> cbs;

	int lua_debug_callback::_dtor(lua_State* L)
	{
		if(dead) return 0;
		dead = true;
		lua_pushlightuserdata(L, &type);
		lua_pushnil(L);
		lua_rawset(L, LUA_REGISTRYINDEX);
		debug_remove_callback(addr, type, h);
		for(auto j = cbs[addr].begin(); j != cbs[addr].end(); j++)
			if(*j == this) {
				cbs[addr].erase(j);
				break;
			}
		if(cbs[addr].empty())
			cbs.erase(addr);
		return 0;
	}

	void do_lua_error(lua::state& L, int ret)
	{
		if(!ret) return;
		switch(ret) {
		case LUA_ERRRUN:
			messages << "Error in Lua memory callback: " << L.get_string(-1, "errhnd") << std::endl;
			L.pop(1);
			return;
		case LUA_ERRMEM:
			messages << "Error in Lua memory callback (Out of memory)" << std::endl;
			return;
		case LUA_ERRERR:
			messages << "Error in Lua memory callback (Double fault)" << std::endl;
			return;
		default:
			messages << "Error in Lua memory callback (\?\?\?)" << std::endl;
			return;
		}
	}

	template<debug_type type, bool reg> class lua_registerX : public lua::function
	{
	public:
		lua_registerX(const std::string& name) : lua::function(lua_func_misc, name) {}
		int invoke(lua::state& L)
		{
			uint64_t addr;
			int base = 1;
			if(L.type(1) == LUA_TNIL && type != DEBUG_TRACE) {
				addr = 0xFFFFFFFFFFFFFFFFULL;
				base = 2;
			} else if(type != DEBUG_TRACE)
				addr = get_read_address(L, base, fname.c_str());
			else
				addr = L.get_numeric_argument<uint64_t>(base++, fname.c_str());
			if(L.type(base) != LUA_TFUNCTION)
				throw std::runtime_error("Expected last argument to " + fname + " to be function");
			if(reg) {
				handle_registerX(L, addr, base);
				L.pushvalue(base);
				return 1;
			} else {
				handle_unregisterX(L, addr, base);
				return 0;
			}
		}
		void handle_registerX(lua::state& L, uint64_t addr, int lfn)
		{
			auto& cbl = cbs[addr];

			//Put the context in userdata so it can be gc'd when Lua context is terminated.
			lua_debug_callback* D = (lua_debug_callback*)L.newuserdata(sizeof(lua_debug_callback));
			L.newtable();
			L.pushstring("__gc");
			L.pushcclosure(&lua_debug_callback::dtor, 0);
			L.rawset(-3);
			L.setmetatable(-2);
			L.pushlightuserdata(&D->addr);
			L.pushvalue(-2);
			L.rawset(LUA_REGISTRYINDEX);
			L.pop(1); //Pop the copy of object.

			cbl.push_back(D);

			D->dead = false;
			D->addr = addr;
			D->type = type;
			D->lua_fn = L.topointer(lfn);
			lua::state* LL = &L.get_master();
			void* D2 = &D->type;
			if(type != DEBUG_TRACE)
				D->h = debug_add_callback(addr, type, [LL, D2](uint64_t addr, uint64_t value) {
					LL->pushlightuserdata(D2);
					LL->rawget(LUA_REGISTRYINDEX);
					LL->pushnumber(addr);
					LL->pushnumber(value);
					do_lua_error(*LL, LL->pcall(2, 0, 0));
				}, [LL, D]() {
					LL->pushlightuserdata(&D->addr);
					LL->pushnil();
					LL->rawset(LUA_REGISTRYINDEX);
					D->_dtor(LL->handle());
				});
			else
				D->h = debug_add_trace_callback(addr, [LL, D2](uint64_t proc, const char* str) {
					LL->pushlightuserdata(D2);
					LL->rawget(LUA_REGISTRYINDEX);
					LL->pushnumber(proc);
					LL->pushstring(str);
					do_lua_error(*LL, LL->pcall(2, 0, 0));
				}, [LL, D]() {
					LL->pushlightuserdata(&D->addr);
					LL->pushnil();
					LL->rawset(LUA_REGISTRYINDEX);
					D->_dtor(LL->handle());
				});
			L.pushlightuserdata(D2);
			L.pushvalue(lfn);
			L.rawset(LUA_REGISTRYINDEX);
		}
		void handle_unregisterX(lua::state& L, uint64_t addr, int lfn)
		{
			if(!cbs.count(addr))
				return;
			auto& cbl = cbs[addr];
			for(auto i = cbl.begin(); i != cbl.end(); i++) {
				if((*i)->type != type) continue;
				if(L.topointer(lfn) != (*i)->lua_fn) continue;
				L.pushlightuserdata(&(*i)->type);
				L.pushnil();
				L.rawset(LUA_REGISTRYINDEX);
				(*i)->_dtor(L.handle());
				//Lua will GC the object.
				break;
			}
		}
	};

	command::fnptr<> callbacks_show_lua(lsnes_cmd, "show-lua-callbacks", "", "",
		[]() throw(std::bad_alloc, std::runtime_error) {
		for(auto& i : cbs)
			for(auto& j : i.second)
				messages << "addr=" << j->addr << " type=" << j->type << " handle="
					<< j->h.handle << " dead=" << j->dead << " lua_fn="
					<< j->lua_fn << std::endl;
		});

	class lua_mmap_memory : public lua::function
	{
	public:
		lua_mmap_memory(const std::string& name, mmap_base& _h) : lua::function(lua_func_misc, name), h(_h) {}
		int invoke(lua::state& L)
		{
			if(L.isnoneornil(1)) {
				aperture_make_fun(L, 0, 0xFFFFFFFFFFFFFFFFULL, h);
				return 1;
			}
			int base = 1;
			uint64_t addr = get_read_address(L, base, "lua_mmap_memory");
			uint64_t size = L.get_numeric_argument<uint64_t>(base, fname.c_str());
			if(!size)
				throw std::runtime_error("Aperture with zero size is not valid");
			aperture_make_fun(L, addr, size - 1, h);
			return 1;
		}
		mmap_base& h;
	};

	lua::fnptr vmacount(lua_func_misc, "memory.vma_count", [](lua::state& L, const std::string& fname)
		-> int {
		L.pushnumber(lsnes_memory.get_regions().size());
		return 1;
	});

	lua::fnptr cheat(lua_func_misc, "memory.cheat", [](lua::state& L, const std::string& fname)
		-> int {
		int base = 1;
		uint64_t addr = get_read_address(L, base, fname.c_str());
		if(L.type(base) == LUA_TNIL || L.type(base) == LUA_TNONE) {
			debug_clear_cheat(addr);
		} else {
			uint64_t value = L.get_numeric_argument<uint64_t>(base, fname.c_str());
			debug_set_cheat(addr, value);
		}
		return 0;
	});

	lua::fnptr xmask(lua_func_misc, "memory.setxmask", [](lua::state& L, const std::string& fname)
		-> int {
		uint64_t value = L.get_numeric_argument<uint64_t>(1, fname.c_str());
		debug_setxmask(value);
		return 0;
	});

	int handle_push_vma(lua::state& L, memory_region& r)
	{
		L.newtable();
		L.pushstring("region_name");
		L.pushlstring(r.name.c_str(), r.name.size());
		L.settable(-3);
		L.pushstring("baseaddr");
		L.pushnumber(r.base);
		L.settable(-3);
		L.pushstring("size");
		L.pushnumber(r.size);
		L.settable(-3);
		L.pushstring("lastaddr");
		L.pushnumber(r.last_address());
		L.settable(-3);
		L.pushstring("readonly");
		L.pushboolean(r.readonly);
		L.settable(-3);
		L.pushstring("iospace");
		L.pushboolean(r.special);
		L.settable(-3);
		L.pushstring("native_endian");
		L.pushboolean(r.endian == 0);
		L.settable(-3);
		L.pushstring("endian");
		L.pushnumber(r.endian);
		L.settable(-3);
		return 1;
	}

	lua::fnptr readvma(lua_func_misc, "memory.read_vma", [](lua::state& L, const std::string& fname)
		-> int {
		std::list<memory_region*> regions = lsnes_memory.get_regions();
		uint32_t num = L.get_numeric_argument<uint32_t>(1, fname.c_str());
		uint32_t j = 0;
		for(auto i = regions.begin(); i != regions.end(); i++, j++)
			if(j == num)
				return handle_push_vma(L, **i);
		L.pushnil();
		return 1;
	});

	lua::fnptr findvma(lua_func_misc, "memory.find_vma", [](lua::state& L, const std::string& fname)
		-> int {
		uint64_t addr = L.get_numeric_argument<uint64_t>(1, fname.c_str());
		auto r = lsnes_memory.lookup(addr);
		if(r.first)
			return handle_push_vma(L, *r.first);
		L.pushnil();
		return 1;
	});

	const char* hexes = "0123456789ABCDEF";

	lua::fnptr hashstate(lua_func_misc, "memory.hash_state", [](lua::state& L, const std::string& fname)
		-> int {
		char hash[64];
		auto x = our_rom.save_core_state();
		size_t offset = x.size() - 32;
		L.pushlstring(hex::b_to((uint8_t*)&x[offset], 32));
		return 1;
	});

#define BLOCKSIZE 256

	lua::fnptr hashmemory(lua_func_misc, "memory.hash_region", [](lua::state& L, const std::string& fname)
		-> int {
		std::string hash;
		int base = 1;
		uint64_t addr = get_read_address(L, base, fname.c_str());
		uint64_t size = L.get_numeric_argument<uint64_t>(base, fname.c_str());
		char buffer[BLOCKSIZE];
		sha256 h;
		while(size > BLOCKSIZE) {
			for(size_t i = 0; i < BLOCKSIZE; i++)
				buffer[i] = lsnes_memory.read<uint8_t>(addr + i);
			h.write(buffer, BLOCKSIZE);
			addr += BLOCKSIZE;
			size -= BLOCKSIZE;
		}
		for(size_t i = 0; i < size; i++)
			buffer[i] = lsnes_memory.read<uint8_t>(addr + i);
		h.write(buffer, size);
		hash = h.read();
		L.pushlstring(hash.c_str(), 64);
		return 1;
	});

	lua::fnptr readmemoryr(lua_func_misc, "memory.readregion", [](lua::state& L, const std::string& fname)
		-> int {
		std::string hash;
		int base = 1;
		uint64_t addr = get_read_address(L, base, fname.c_str());
		uint64_t size = L.get_numeric_argument<uint64_t>(base, fname.c_str());
		L.newtable();
		char buffer[BLOCKSIZE];
		uint64_t ctr = 0;
		while(size > 0) {
			size_t rsize = min(size, static_cast<uint64_t>(BLOCKSIZE));
			lsnes_memory.read_range(addr, buffer, rsize);
			for(size_t i = 0; i < rsize; i++) {
				L.pushnumber(ctr++);
				L.pushnumber(static_cast<unsigned char>(buffer[i]));
				L.settable(-3);
			}
			addr += rsize;
			size -= rsize;
		}
		return 1;
	});

	lua::fnptr writememoryr(lua_func_misc, "memory.writeregion", [](lua::state& L,
		const std::string& fname) -> int {
		std::string hash;
		int base = 1;
		uint64_t addr = get_read_address(L, base, fname.c_str());
		uint64_t size = L.get_numeric_argument<uint64_t>(base, fname.c_str());
		char buffer[BLOCKSIZE];
		uint64_t ctr = 0;
		while(size > 0) {
			size_t rsize = min(size, static_cast<uint64_t>(BLOCKSIZE));
			for(size_t i = 0; i < rsize; i++) {
				L.pushnumber(ctr++);
				L.gettable(3);
				buffer[i] = L.tointeger(-1);
				L.pop(1);
			}
			lsnes_memory.write_range(addr, buffer, rsize);
			addr += rsize;
			size -= rsize;
		}
		return 1;
	});

	lua::fnptr gui_cbitmap(lua_func_misc, "memory.map_structure", [](lua::state& L,
		const std::string& fname) -> int {
		lua::_class<lua_mmap_struct>::create(L);
		return 1;
	});

	template<bool write, bool sign> int memory_scattergather(lua::state& L, const std::string& fname)
	{
		uint64_t val = 0;
		int ptr = 1;
		unsigned shift = 0;
		uint64_t addr = 0;
		uint64_t vmabase = 0;
		if(write)
			val = L.get_numeric_argument<uint64_t>(ptr++, fname.c_str());
		while(L.type(ptr) != LUA_TNIL && L.type(ptr) != LUA_TNONE) {
			if(L.type(ptr) == LUA_TBOOLEAN) {
				if(L.toboolean(ptr++))
					addr++;
				else
					addr--;
			} else if(L.type(ptr) == LUA_TSTRING) {
				vmabase = get_vmabase(L, L.get_string(ptr++, fname.c_str()));
				continue;
			} else
				addr = L.get_numeric_argument<uint64_t>(ptr++, fname.c_str());
			if(write)
				lsnes_memory.write<uint8_t>(addr + vmabase, val >> shift);
			else
				val = val + ((uint64_t)lsnes_memory.read<uint8_t>(addr + vmabase) << shift);
			shift += 8;
		}
		if(!write) {
			int64_t sval = val;
			if(val >= (1ULL << (shift - 1))) sval -= (1ULL << shift);
			if(sign) L.pushnumber(sval); else L.pushnumber(val);
		}
		return write ? 0 : 1;
	}

	lua::fnptr scattergather1(lua_func_misc, "memory.read_sg", [](lua::state& L, const std::string& fname)
		-> int {
		return memory_scattergather<false, false>(L, fname);
	});

	lua::fnptr scattergather2(lua_func_misc, "memory.sread_sg", [](lua::state& L,
		const std::string& fname) -> int {
		return memory_scattergather<false, true>(L, fname);
	});

	lua::fnptr scattergather3(lua_func_misc, "memory.write_sg", [](lua::state& L,
		const std::string& fname) -> int {
		return memory_scattergather<true, false>(L, fname);
	});

	lua_read_memory<uint8_t, &memory_space::read<uint8_t>> rub("memory.readbyte");
	lua_read_memory<int8_t, &memory_space::read<int8_t>> rsb("memory.readsbyte");
	lua_read_memory<uint16_t, &memory_space::read<uint16_t>> ruw("memory.readword");
	lua_read_memory<int16_t, &memory_space::read<int16_t>> rsw("memory.readsword");
	lua_read_memory<ss_uint24_t, &memory_space::read<ss_uint24_t>> ruh("memory.readhword");
	lua_read_memory<ss_int24_t, &memory_space::read<ss_int24_t>> rsh("memory.readshword");
	lua_read_memory<uint32_t, &memory_space::read<uint32_t>> rud("memory.readdword");
	lua_read_memory<int32_t, &memory_space::read<int32_t>> rsd("memory.readsdword");
	lua_read_memory<uint64_t, &memory_space::read<uint64_t>> ruq("memory.readqword");
	lua_read_memory<int64_t, &memory_space::read<int64_t>> rsq("memory.readsqword");
	lua_read_memory<float, &memory_space::read<float>> rf4("memory.readfloat");
	lua_read_memory<double, &memory_space::read<double>> rf8("memory.readdouble");
	lua_write_memory<uint8_t, &memory_space::write<uint8_t>> wb("memory.writebyte");
	lua_write_memory<uint16_t, &memory_space::write<uint16_t>> ww("memory.writeword");
	lua_write_memory<ss_uint24_t, &memory_space::write<ss_uint24_t>> wh("memory.writehword");
	lua_write_memory<uint32_t, &memory_space::write<uint32_t>> wd("memory.writedword");
	lua_write_memory<uint64_t, &memory_space::write<uint64_t>> wq("memory.writeqword");
	lua_write_memory<float, &memory_space::write<float>> wf4("memory.writefloat");
	lua_write_memory<double, &memory_space::write<double>> wf8("memory.writedouble");
	lua_mmap_memory_helper<uint8_t, &memory_space::read<uint8_t>, &memory_space::write<uint8_t>> mhub;
	lua_mmap_memory_helper<int8_t, &memory_space::read<int8_t>, &memory_space::write<int8_t>> mhsb;
	lua_mmap_memory_helper<uint16_t, &memory_space::read<uint16_t>, &memory_space::write<uint16_t>> mhuw;
	lua_mmap_memory_helper<int16_t, &memory_space::read<int16_t>, &memory_space::write<int16_t>> mhsw;
	lua_mmap_memory_helper<ss_uint24_t, &memory_space::read<ss_uint24_t>, &memory_space::write<ss_uint24_t>> mhuh;
	lua_mmap_memory_helper<ss_int24_t, &memory_space::read<ss_int24_t>, &memory_space::write<ss_int24_t>> mhsh;
	lua_mmap_memory_helper<uint32_t, &memory_space::read<uint32_t>, &memory_space::write<uint32_t>> mhud;
	lua_mmap_memory_helper<int32_t, &memory_space::read<int32_t>, &memory_space::write<int32_t>> mhsd;
	lua_mmap_memory_helper<uint64_t, &memory_space::read<uint64_t>, &memory_space::write<uint64_t>> mhuq;
	lua_mmap_memory_helper<int64_t, &memory_space::read<int64_t>, &memory_space::write<int64_t>> mhsq;
	lua_mmap_memory_helper<float, &memory_space::read<float>, &memory_space::write<float>> mhf4;
	lua_mmap_memory_helper<double, &memory_space::read<double>, &memory_space::write<double>> mhf8;
	lua_mmap_memory mub("memory.mapbyte", mhub);
	lua_mmap_memory msb("memory.mapsbyte", mhsb);
	lua_mmap_memory muw("memory.mapword", mhuw);
	lua_mmap_memory msw("memory.mapsword", mhsw);
	lua_mmap_memory muh("memory.maphword", mhuh);
	lua_mmap_memory msh("memory.mapshword", mhsh);
	lua_mmap_memory mud("memory.mapdword", mhud);
	lua_mmap_memory msd("memory.mapsdword", mhsd);
	lua_mmap_memory muq("memory.mapqword", mhuq);
	lua_mmap_memory msq("memory.mapsqword", mhsq);
	lua_mmap_memory mf4("memory.mapfloat", mhf4);
	lua_mmap_memory mf8("memory.mapdouble", mhf8);
	lua_registerX<DEBUG_READ, true> mrr("memory.registerread");
	lua_registerX<DEBUG_READ, false> murr("memory.unregisterread");
	lua_registerX<DEBUG_WRITE, true> mrw("memory.registerwrite");
	lua_registerX<DEBUG_WRITE, false> murw("memory.unregisterwrite");
	lua_registerX<DEBUG_EXEC, true> mrx("memory.registerexec");
	lua_registerX<DEBUG_EXEC, false> murx("memory.unregisterexec");
	lua_registerX<DEBUG_TRACE, true> mrt("memory.registertrace");
	lua_registerX<DEBUG_TRACE, false> murt("memory.unregistertrace");

	lua::_class<lua_mmap_struct> class_mmap_struct(lua_class_memory, "MMAP_STRUCT", {}, {
		{"__index", &lua_mmap_struct::index},
		{"__newindex", &lua_mmap_struct::newindex},
		{"__call", &lua_mmap_struct::map},
	});
}

int lua_mmap_struct::map(lua::state& L, const std::string& fname)
{
	const char* name = L.tostring(2);
	int base = 0;
	uint64_t vmabase = 0;
	if(L.type(3) == LUA_TSTRING) {
		vmabase = get_vmabase(L, L.get_string(3, fname.c_str()));
		base = 1;
	}
	uint64_t addr = L.get_numeric_argument<uint64_t>(base + 3, fname.c_str());
	const char* type = L.tostring(base + 4);
	if(!name)
		(stringfmt() << fname << ": Bad name").throwex();
	if(!type)
		(stringfmt() << fname << ": Bad type").throwex();
	std::string name2(name);
	std::string type2(type);
	if(type2 == "byte")
		mappings[name2] = std::make_pair(&mhub, addr);
	else if(type2 == "sbyte")
		mappings[name2] = std::make_pair(&mhsb, addr);
	else if(type2 == "word")
		mappings[name2] = std::make_pair(&mhuw, addr);
	else if(type2 == "sword")
		mappings[name2] = std::make_pair(&mhsw, addr);
	else if(type2 == "hword")
		mappings[name2] = std::make_pair(&mhuh, addr);
	else if(type2 == "shword")
		mappings[name2] = std::make_pair(&mhsh, addr);
	else if(type2 == "dword")
		mappings[name2] = std::make_pair(&mhud, addr);
	else if(type2 == "sdword")
		mappings[name2] = std::make_pair(&mhsd, addr);
	else if(type2 == "qword")
		mappings[name2] = std::make_pair(&mhuq, addr);
	else if(type2 == "sqword")
		mappings[name2] = std::make_pair(&mhsq, addr);
	else if(type2 == "float")
		mappings[name2] = std::make_pair(&mhf4, addr);
	else if(type2 == "double")
		mappings[name2] = std::make_pair(&mhf8, addr);
	else
		(stringfmt() << fname << ": Bad type").throwex();
	return 0;
}

lua_mmap_struct::lua_mmap_struct(lua::state& L)
{
}
