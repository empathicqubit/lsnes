#include "cmdhelp/action.hpp"
#include "core/command.hpp"
#include "core/instance.hpp"
#include "core/messages.hpp"
#include "core/moviedata.hpp"
#include "core/rom.hpp"

namespace
{
	command::fnptr<const std::string&> CMD_action(lsnes_cmds, CACTION::e,
		[](const std::string& _args) {
			auto& core = CORE();
			if(_args == "") {
				messages << "Action name required." << std::endl;
				return;
			}
			token_iterator<char> itr(_args, {" ", "\t"});
			token_iterator<char> itre;
			std::string sym = *itr++;
			const interface_action* act = NULL;
			for(auto i : core.rom->get_actions())
				if(i->get_symbol() == sym) {
					act = i;
					break;
				}
			if(!act) {
				messages << "No such action." << std::endl;
				return;
			}
			if(!(core.rom->action_flags(act->id) & 1)) {
				messages << "Action not enabled." << std::endl;
				return;
			}
			std::vector<interface_action_paramval> params;
			for(auto i : act->params) {
				if(regex_match("toggle", i.model)) {
					interface_action_paramval pv;
					params.push_back(pv);
					continue;
				}
				if(itr == itre) {
					messages << "Action needs more parameters." << std::endl;
					return;
				}
				std::string p = *itr++;
				regex_results r;
				interface_action_paramval pv;
				if(r = regex("string(:(.*))?", i.model)) {
					try {
						if(r[2] != "" && !regex_match(r[2], p)) {
							messages << "String does not satisfy constraints."
								<< std::endl;
							return;
						}
					} catch(...) {
						messages << "Internal error: Bad constraint in '" << i.model << "'."
							<< std::endl;
						return;
					}
					pv.s = p;
				} else if(r = regex("int:([0-9]+),([0-9]+)", i.model)) {
					int64_t low, high, v;
					try {
						low = parse_value<int64_t>(r[1]);
						high = parse_value<int64_t>(r[2]);
					} catch(...) {
						messages << "Internal error: Unknown limits in '" << i.model << "'."
							<< std::endl;
						return;
					}
					try {
						v = parse_value<int64_t>(p);
					} catch(...) {
						messages << "Can't parse parameter as integer." << std::endl;
						return;
					}
					if(v < low || v > high) {
						messages << "Parameter out of limits." << std::endl;
						return;
					}
					pv.i = v;
				} else if(r = regex("enum:(.*)", i.model)) {
					try {
						JSON::node e(r[1]);
						unsigned num = 0;
						for(auto i : e) {
							std::string n;
							if(i.type() == JSON::string)
								n = i.as_string8();
							else if(i.type() == JSON::array)
								n = i.index(0).as_string8();
							else
								throw std::runtime_error("Choice not array nor "
									"string");
							if(n == p)
								goto out;
							num++;
						}
						messages << "Invalid choice for enumeration." << std::endl;
						return;
out:
						pv.i = num;
					} catch(std::exception& e) {
						messages << "JSON parse error parsing " << "model: "
							<< e.what() << std::endl;
						return;
					}
				} else if(regex_match("bool", i.model)) {
					int r = string_to_bool(p);
					if(r < 0) {
						messages << "Bad value for boolean parameter." << std::endl;
						return;
					}
					pv.b = (r > 0);
				} else if(regex_match("toggle", i.model)) {
				} else {
					messages << "Internal error: Unknown parameter model '" << i.model << "'."
						<< std::endl;
					return;
				}
				params.push_back(pv);
			}
			if(itr != itre) {
				messages << "Excess parameters for action." << std::endl;
				return;
			}
			core.rom->execute_action(act->id, params);
		});
}
