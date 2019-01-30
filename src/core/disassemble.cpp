#include "cmdhelp/disassemble.hpp"
#include "core/command.hpp"
#include "core/instance.hpp"
#include "core/memorymanip.hpp"
#include "core/messages.hpp"
#include "interface/disassembler.hpp"
#include "library/hex.hpp"
#include "library/memoryspace.hpp"
#include "library/minmax.hpp"
#include "library/string.hpp"

#include <iomanip>
#include <fstream>
#include <iostream>

namespace
{
	struct dres
	{
		uint64_t addr;
		uint64_t len;
		std::string disasm;
	};

	command::fnptr<const std::string&> CMD_disassemble(lsnes_cmds, CDISASM::d,
		[](const std::string& t) {
		auto& core = CORE();
		regex_results r = regex("([^ \t]+)[ \t]+([0-9]+|0x[0-9A-Fa-f]+)([ \t]+([0-9]+))?"
			"([ \t]+to[ \t]+(.+))?", t);
		if(!r) {
			messages << "Syntax: disassemble <kind> <addr> [<count>] [to <filename>]" << std::endl;
			return;
		}
		std::string kind = r[1];
		uint64_t addr = parse_value<uint64_t>(r[2]);
		uint64_t count = 1;
		if(r[4] != "")
			count = parse_value<uint64_t>(r[4]);
		std::string file;
		if(r[6] != "")
			file = r[6];
		std::list<dres> result;
		disassembler* d;
		try {
			d = &disassembler::byname(kind);
		} catch(std::exception& e) {
			messages << "Can't find such disassembler" << std::endl;
			return;
		}
		uint64_t laddr = addr;
		uint64_t longest = 0;
		for(uint64_t i = 1; i <= count; i++) {
			uint64_t bytes = 0;
			dres x;
			x.addr = laddr;
			x.disasm = d->disassemble(laddr, [&core, &bytes, laddr]() -> unsigned char {
				return core.memory->read<uint8_t>(laddr + bytes++);
			});
			x.len = bytes;
			result.push_back(x);
			longest = max(longest, bytes);
			laddr += bytes;
		}
		std::ostream* strm = &messages.getstream();
		if(file != "") {
			strm = new std::ofstream(file);
			if(!*strm) {
				messages << "Can't open output file" << std::endl;
				return;
			}
		}
		for(auto i : result) {
			std::vector<unsigned char> tmp;
			tmp.resize(i.len);
			core.memory->read_range(i.addr, &tmp[0], i.len);
			std::string l = hex::to(i.addr) + " " + hex::b_to(&tmp[0], i.len) + " " + i.disasm;
			(*strm) << l << std::endl;
		}
		if(file != "")
			delete strm;
	});
}
