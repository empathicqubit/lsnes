#include "command.hpp"
#include "keyboard.hpp"
#include "keyboard-mapper.hpp"
#include "stateobject.hpp"
#include "string.hpp"
#include "threads.hpp"

namespace keyboard
{
namespace
{
	threads::rlock* global_lock;
	threads::rlock& get_keymap_lock()
	{
		if(!global_lock) global_lock = new threads::rlock;
		return *global_lock;
	}

	struct set_internal
	{
		std::map<std::string, invbind_info*> invbinds;
		std::set<invbind_set::listener*> callbacks;
	};

	struct mapper_internal
	{
		std::map<std::string, invbind*> ibinds;
		std::map<std::string, ctrlrkey*> ckeys;
		std::map<mapper::triplet, std::string> bindings;
		std::set<invbind_set*> invbind_set_cbs;
	};

	typedef stateobject::type<invbind_set, set_internal> set_internal_t;
	typedef stateobject::type<mapper, mapper_internal> mapper_internal_t;
}

invbind_set::listener::~listener()
{
}

std::string mapper::fixup_command_polarity(std::string cmd, bool polarity)
{
	if(cmd == "" || cmd == "*")
		return "";
	if(cmd[0] != '*') {
		if(cmd[0] != '+' && polarity)
			return "";
		if(cmd[0] == '+' && !polarity)
			cmd[0] = '-';
	} else {
		if(cmd[1] != '+' && polarity)
			return "";
		if(cmd[1] == '+' && !polarity)
			cmd[1] = '-';
	}
	return cmd;
}

keyspec::keyspec()
{
}

keyspec::keyspec(const std::string& keyspec)
{
	regex_results r = regex("([^/]*)/([^|]*)\\|(.*)", keyspec, "Invalid keyspec");
	mod = r[1];
	mask = r[2];
	key = r[3];
}

keyspec::operator std::string()
{
	return mod + "/" + mask + "|" + key;
}

keyspec::operator bool() throw()
{
	return (key != "");
}

bool keyspec::operator!() throw()
{
	return (key == "");
}

void keyspec::clear() throw()
{
	mod = "";
	mask = "";
	key = "";
}

bool keyspec::operator==(const keyspec& keyspec)
{
	return (mod == keyspec.mod && mask == keyspec.mask && key == keyspec.key);
}

bool keyspec::operator!=(const keyspec& keyspec)
{
	return (mod != keyspec.mod || mask != keyspec.mask || key != keyspec.key);
}

std::set<invbind*> mapper::get_inverses()
{
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	std::set<invbind*> r;
	if(state)
		for(auto i : state->ibinds)
			r.insert(i.second);
	return r;
}

invbind* mapper::get_inverse(const std::string& command)
{
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(state && state->ibinds.count(command))
		return state->ibinds[command];
	else
		return NULL;
}

std::set<ctrlrkey*> mapper::get_controller_keys()
{
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	std::set<ctrlrkey*> r;
	if(state)
		for(auto i : state->ckeys)
			r.insert(i.second);
	return r;
}

ctrlrkey* mapper::get_controllerkey(const std::string& command)
{
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(state && state->ckeys.count(command))
		return state->ckeys[command];
	else
		return NULL;
}

void mapper::do_register(const std::string& name, invbind& ibind)
{
	threads::arlock u(get_keymap_lock());
	auto& state = mapper_internal_t::get(this);
	if(state.ibinds.count(name)) return;
	state.ibinds[name] = &ibind;
	//Search for matches.
	for(auto i : state.bindings)
		if(i.second == ibind.cmd) {
			ibind.specs.push_back(i.first.as_keyspec());
		}
}

void mapper::do_unregister(const std::string& name, invbind& ibind)
{
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(state && state->ibinds.count(name) && state->ibinds[name] == &ibind)
		state->ibinds.erase(name);
}

void mapper::do_register(const std::string& name, ctrlrkey& ckey)
{
	threads::arlock u(get_keymap_lock());
	auto& state = mapper_internal_t::get(this);
	if(state.ckeys.count(name)) return;
	state.ckeys[name] = &ckey;
}

void mapper::do_unregister(const std::string& name, ctrlrkey& ckey)
{
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(state && state->ckeys.count(name) && state->ckeys[name] == &ckey)
		state->ckeys.erase(name);
}

keyboard& mapper::get_keyboard() throw()
{
	return kbd;
}

mapper::mapper(keyboard& _kbd, command::group& _domain)
	: _listener(*this), kbd(_kbd), domain(_domain)
{
}

mapper::~mapper() throw()
{
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(!state) return;
	for(auto i : state->ibinds)
		i.second->mapper_died();
	for(auto i : state->invbind_set_cbs)
		i->drop_callback(_listener);
	mapper_internal_t::clear(this);
}

mapper::triplet::triplet(modifier_set _mod, modifier_set _mask, key& kkey,
	unsigned _subkey)
{
	mod = _mod;
	mask = _mask;
	_key = &kkey;
	subkey = _subkey;
	index = false;
}

mapper::triplet::triplet(key& kkey, unsigned _subkey)
{
	_key = &kkey;
	subkey = _subkey;
	index = true;
}

bool mapper::triplet::operator<(const struct triplet& a) const
{
	if((uint64_t)_key < (uint64_t)a._key)
		return true;
	if((uint64_t)_key > (uint64_t)a._key)
		return false;
	if(subkey < a.subkey)
		return true;
	if(subkey > a.subkey)
		return false;
	if(index && !a.index)
		return true;
	if(!index && a.index)
		return false;
	if(mask < a.mask)
		return true;
	if(a.mask < mask)
		return false;
	if(mod < a.mod)
		return true;
	if(a.mod < mod)
		return false;
	return false;
}

bool mapper::triplet::operator==(const struct triplet& a) const
{
	if(index != a.index)
		return false;
	if(_key != a._key)
		return false;
	if(subkey != a.subkey)
		return false;
	if(!(mod == a.mod))
		return false;
	if(!(mask == a.mask))
		return false;
	return true;
}

keyspec mapper::triplet::as_keyspec() const
{
	keyspec k;
	k.mod = mod;
	k.mask = mask;
	auto s = _key->get_subkeys();
	if(s.size() > subkey)
		k.key = _key->get_name() + s[subkey];
	else
		k.key = _key->get_name();
	return k;
}

std::list<keyspec> mapper::get_bindings()
{
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	std::list<keyspec> r;
	if(state)
		for(auto i : state->bindings)
			r.push_back(i.first.as_keyspec());
	return r;
}

command::group& mapper::get_command_group() throw()
{
	return domain;
}

void mapper::bind(std::string mod, std::string modmask, std::string keyname, std::string command)
{
	keyspec spec;
	spec.mod = mod;
	spec.mask = modmask;
	spec.key = keyname;
	triplet t(kbd, spec);
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(!state)
		throw std::runtime_error("Attempt to bind key before initializing mapper");
	if(state->bindings.count(t))
		throw std::runtime_error("Key is already bound");
	if(!listening.count(t._key)) {
		t._key->add_listener(*this, false);
		listening.insert(t._key);
	}
	std::string old_command;
	if(state->bindings.count(t))
		old_command = state->bindings[t];
	state->bindings[t] = command;
	change_command(spec, old_command, command);
}

void mapper::unbind(std::string mod, std::string modmask, std::string keyname)
{
	keyspec spec;
	spec.mod = mod;
	spec.mask = modmask;
	spec.key = keyname;
	triplet t(kbd, spec);
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(!state || !state->bindings.count(t))
		throw std::runtime_error("Key is not bound");
	//No harm at leaving listeners listening.
	std::string old_command;
	if(state->bindings.count(t))
		old_command = state->bindings[t];
	state->bindings.erase(t);
	change_command(spec, old_command, "");
}

std::string mapper::get(const keyspec& keyspec)
{
	triplet t(kbd, keyspec);
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(!state || !state->bindings.count(t))
		return "";
	return state->bindings[t];
}

void mapper::change_command(const keyspec& spec, const std::string& old, const std::string& newc)
{
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(!state) return;
	if(old != "" && state->ibinds.count(old)) {
		auto& i = state->ibinds[old];
		{
			i->specs.clear();
		}
		for(auto j : state->bindings)
			if(j.second == i->cmd && j.first.as_keyspec() != spec) {
				i->specs.push_back(j.first.as_keyspec());
			}
	}
	if(newc != "" && state->ibinds.count(newc)) {
		auto& i = state->ibinds[newc];
		i->specs.push_back(spec);
	}
}

void mapper::set(const keyspec& keyspec, const std::string& cmd)
{
	triplet t(kbd, keyspec);
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(!state) throw std::runtime_error("Attempt to set key in mapper before initializing");
	if(!listening.count(t._key)) {
		t._key->add_listener(*this, false);
		listening.insert(t._key);
	}
	std::string oldcmd;
	if(state->bindings.count(t))
		oldcmd = state->bindings[t];
	state->bindings[t] = cmd;
	change_command(keyspec, oldcmd, cmd);
}

void mapper::on_key_event(modifier_set& mods, key& key, event& event)
{
	auto mask = event.get_change_mask();
	unsigned i = 0;
	while(mask) {
		unsigned k = mask & 3;
		if(k & 2)
			on_key_event_subkey(mods, key, i, k == 3);
		mask >>= 2;
		i++;
	}
}

void mapper::on_key_event_subkey(modifier_set& mods, key& key, unsigned skey,
	bool polarity)
{
	std::list<std::string> cmd;
	{
		threads::arlock u(get_keymap_lock());
		auto state = mapper_internal_t::get_soft(this);
		if(!state) return;
		triplet llow(key, skey);
		triplet lhigh(key, skey + 1);
		auto low = state->bindings.lower_bound(llow);
		auto high = state->bindings.lower_bound(lhigh);
		for(auto i = low; i != high; i++) {
			if(!mods.triggers(i->first.mod, i->first.mask))
				continue;
			std::string xcmd = fixup_command_polarity(i->second, polarity);
			if(xcmd != "") cmd.push_back(xcmd);
		}
	}
	for(auto i : cmd)
		domain.invoke(i);
}

mapper::triplet::triplet(keyboard& k, const keyspec& spec)
{
	mod = modifier_set::construct(k, spec.mod);
	mask = modifier_set::construct(k, spec.mask);
	if(!mod.valid(mask))
		throw std::runtime_error("Bad modifiers");
	auto g = keymapper_lookup_subkey(k, spec.key, false);
	_key = g.first;
	subkey = g.second;
	index = false;
}

std::list<ctrlrkey*> mapper::get_controllerkeys_kbdkey(key* kbdkey)
{
	threads::arlock u(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	std::list<ctrlrkey*> r;
	if(state)
		for(auto i : state->ckeys) {
			for(unsigned j = 0;; j++) {
				auto k = i.second->get(j);
				if(!k.first)
					break;
				if(k.first == kbdkey)
					r.push_back(i.second);
			}
		}
	return r;
}

void mapper::add_invbind_set(invbind_set& set)
{
	threads::arlock u(get_keymap_lock());
	auto& state = mapper_internal_t::get(this);
	if(state.invbind_set_cbs.count(&set)) return;
	try {
		state.invbind_set_cbs.insert(&set);
		set.add_callback(_listener);
	} catch(...) {
		state.invbind_set_cbs.erase(&set);
	}
}

void mapper::drop_invbind_set(invbind_set& set)
{
	threads::arlock h(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(this);
	if(!state) return;
	//Drop the callback. This unregisters all.
	set.drop_callback(_listener);
	state->invbind_set_cbs.erase(&set);
}

mapper::listener::listener(mapper& _grp)
	: grp(_grp)
{
}

mapper::listener::~listener()
{
}

void mapper::listener::create(invbind_set& s, const std::string& name, invbind_info& ibinfo)
{
	threads::arlock h(get_keymap_lock());
	ibinfo.make(grp);
}

void mapper::listener::destroy(invbind_set& s, const std::string& name)
{
	threads::arlock h(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(&grp);
	if(state)
		state->ibinds.erase(name);
}

void mapper::listener::kill(invbind_set& s)
{
	threads::arlock h(get_keymap_lock());
	auto state = mapper_internal_t::get_soft(&grp);
	if(state)
		state->invbind_set_cbs.erase(&s);
}

invbind::invbind(mapper& kmapper, const std::string& _command, const std::string& _name, bool dynamic)
	: _mapper(&kmapper), cmd(_command), oname(_name)
{
	is_dynamic = dynamic;
	_mapper->do_register(cmd, *this);
}

invbind::~invbind() throw()
{
	threads::arlock h(get_keymap_lock());
	if(_mapper)
		_mapper->do_unregister(cmd, *this);
}

keyspec invbind::get(unsigned index)
{
	threads::arlock u(get_keymap_lock());
	if(index >= specs.size())
		return keyspec();
	return specs[index];
}

void invbind::clear(unsigned index)
{
	threads::arlock u(get_keymap_lock());
	keyspec unbind;
	{
		if(index >= specs.size())
			return;
		unbind = specs[index];
	}
	if(unbind && _mapper)
		_mapper->set(unbind, "");
}

void invbind::append(const keyspec& keyspec)
{
	threads::arlock u(get_keymap_lock());
	_mapper->set(keyspec, cmd);
}

std::string invbind::getname()
{
	return oname;
}

void invbind::mapper_died()
{
	threads::arlock u(get_keymap_lock());
	_mapper = NULL;
	if(is_dynamic) delete this;
}

invbind_info::invbind_info(invbind_set& set, const std::string& _command, const std::string& _name)
	: in_set(&set)
{
	command = _command;
	name = _name;
	in_set->do_register(command, *this);
}

invbind_info::~invbind_info() throw()
{
	threads::arlock u(get_keymap_lock());
	if(in_set)
		in_set->do_unregister(command, *this);
}

invbind* invbind_info::make(mapper& m)
{
	return new invbind(m, command, name, true);
}

void invbind_info::set_died()
{
	threads::arlock u(get_keymap_lock());
	in_set = NULL;
}

invbind_set::invbind_set()
{
}

invbind_set::~invbind_set()
{
	auto state = set_internal_t::get_soft(this);
	if(!state) return;
	threads::arlock u(get_keymap_lock());
	//Call all DCBs on all factories.
	for(auto i : state->invbinds)
		for(auto j : state->callbacks)
			j->destroy(*this, i.first);
	//Call all TCBs.
	for(auto j : state->callbacks)
		j->kill(*this);
	//Notify all factories that base set died.
	for(auto i : state->invbinds)
		i.second->set_died();
	//We assume factories look after themselves, so we don't destroy those.
	set_internal_t::clear(this);
}

void invbind_set::do_register(const std::string& name, invbind_info& info)
{
	threads::arlock u(get_keymap_lock());
	auto& state = set_internal_t::get(this);
	if(state.invbinds.count(name)) {
		std::cerr << "WARNING: Command collision for " << name << "!" << std::endl;
		return;
	}
	state.invbinds[name] = &info;
	//Call all CCBs on this.
	for(auto i : state.callbacks)
		i->create(*this, name, info);
}

void invbind_set::do_unregister(const std::string& name, invbind_info& info)
{
	threads::arlock u(get_keymap_lock());
	auto state = set_internal_t::get_soft(this);
	if(!state) return;
	if(!state->invbinds.count(name) || state->invbinds[name] != &info) return; //Not this.
	state->invbinds.erase(name);
	//Call all DCBs on this.
	for(auto i : state->callbacks)
		i->destroy(*this, name);
}

void invbind_set::add_callback(invbind_set::listener& listener)
{
	threads::arlock u(get_keymap_lock());
	auto& state = set_internal_t::get(this);
	state.callbacks.insert(&listener);
	//To avoid races, call CCBs on all factories for this.
	for(auto j : state.invbinds)
		listener.create(*this, j.first, *j.second);
}

void invbind_set::drop_callback(invbind_set::listener& listener)
{
	threads::arlock u(get_keymap_lock());
	auto state = set_internal_t::get_soft(this);
	if(!state) return;
	if(state->callbacks.count(&listener)) {
		//To avoid races, call DCBs on all factories for this.
		for(auto j : state->invbinds)
			listener.destroy(*this, j.first);
		state->callbacks.erase(&listener);
	}
}

ctrlrkey::ctrlrkey(mapper& kmapper, const std::string& _command, const std::string& _name,
	bool _axis)
	: _mapper(kmapper), cmd(_command), oname(_name)
{
	axis = _axis;
	_mapper.do_register(cmd, *this);
}

ctrlrkey::~ctrlrkey() throw()
{
	_mapper.do_unregister(cmd, *this);
}

std::pair<key*, unsigned> ctrlrkey::get(unsigned index) throw()
{
	threads::arlock u(get_keymap_lock());
	if(index >= keys.size())
		return std::make_pair(reinterpret_cast<key*>(NULL), 0);
	return keys[index];
}

std::string ctrlrkey::get_string(unsigned index)
{
	auto k = get(index);
	if(!k.first)
		return "";
	auto s = k.first->get_subkeys();
	if(k.second >= s.size() || axis)
		return k.first->get_name();
	return k.first->get_name() + s[k.second];
}

void ctrlrkey::append(key* _key, unsigned _subkey) throw()
{
	threads::arlock u(get_keymap_lock());
	//Search for duplicates.
	std::pair<key*, unsigned> mkey = std::make_pair(_key, _subkey);
	for(auto i : keys)
		if(i == mkey)
			return;
	//No dupes, add.
	_key->add_listener(*this, axis);
	keys.push_back(mkey);
}

void ctrlrkey::remove(key* _key, unsigned _subkey) throw()
{
	threads::arlock u(get_keymap_lock());
	std::pair<key*, unsigned> mkey = std::make_pair(_key, _subkey);
	for(auto i = keys.begin(); i != keys.end(); i++) {
		if(*i == mkey) {
			mkey.first->remove_listener(*this);
			keys.erase(i);
			return;
		}
	}
}

void ctrlrkey::append(const std::string& _key)
{
	auto g = keymapper_lookup_subkey(_mapper.get_keyboard(), _key, axis);
	append(g.first, g.second);
}

std::pair<key*, unsigned> keymapper_lookup_subkey(keyboard& kbd, const std::string& name,
	bool axis)
{
	threads::arlock u(get_keymap_lock());
	if(name == "")
		return std::make_pair((key*)NULL, 0);
	//Try direct lookup first.
	key* key = kbd.try_lookup_key(name);
	if(key)
		return std::make_pair(key, 0);
	//Axes only do direct lookup.
	if(axis)
		throw std::runtime_error("Invalid key");
	std::string prefix = name;
	char letter = prefix[prefix.length() - 1];
	prefix = prefix.substr(0, prefix.length() - 1);
	key = kbd.try_lookup_key(prefix);
	if(!key)
		throw std::runtime_error("Invalid key");
	auto s = key->get_subkeys();
	for(size_t i = 0; i < s.size(); i++)
		if(s[i].length() > 0 && letter == s[i][0])
			return std::make_pair(key, i);
	throw std::runtime_error("Invalid key");
}

void ctrlrkey::on_key_event(modifier_set& mods, key& key, event& event)
{
	if(axis) {
		//Axes work specially.
		_mapper.get_command_group().invoke((stringfmt() << cmd << " " << event.get_state()).str());
		return;
	}
	auto mask = event.get_change_mask();
	for(auto i : keys) {
		if(i.first != &key)
			continue;
		unsigned kmask = (mask >> (2 * i.second)) & 3;
		std::string cmd2;
		if(kmask & 2)
			cmd2 = mapper::fixup_command_polarity(cmd, kmask == 3);
		if(cmd2 != "")
			_mapper.get_command_group().invoke(cmd2);
	}
}
}
