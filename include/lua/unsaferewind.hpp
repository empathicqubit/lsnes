#ifndef _lua__unsaferewind__hpp__included__
#define _lua__unsaferewind__hpp__included__

#include "library/lua-base.hpp"
#include "library/string.hpp"

struct lua_unsaferewind
{
	lua_unsaferewind(lua::state& L);
	static size_t overcommit() { return 0; }
	std::vector<char> state;
	uint64_t frame;
	uint64_t lag;
	uint64_t ptr;
	uint64_t secs;
	uint64_t ssecs;
	uint64_t vi_counter;
	uint32_t vi_this_frame;
	std::vector<uint32_t> pollcounters;
	std::vector<char> hostmemory;
	std::string print()
	{
		return (stringfmt() << "to frame " << frame).str();
	}
};

#endif
