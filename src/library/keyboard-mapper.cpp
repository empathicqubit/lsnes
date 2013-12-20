#include "command.hpp"
#include "keyboard-mapper.hpp"
#include "register-queue.hpp"
#include "string.hpp"

namespace keyboard
{
std::string mapper::fixup_command_polarity(std::string cmd, bool polarity) throw(std::bad_alloc)
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

keyspec::keyspec() throw(std::bad_alloc)
{
}

keyspec::keyspec(const std::string& keyspec) throw(std::bad_alloc, std::runtime_error)
{
	regex_results r = regex("([^/]*)/([^|]*)\\|(.*)", keyspec, "Invalid keyspec");
	mod = r[1];
	mask = r[2];
	key = r[3];
}

keyspec::operator std::string() throw(std::bad_alloc)
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

std::set<invbind*> mapper::get_inverses() throw(std::bad_alloc)
{
	umutex_class u(mutex);
	std::set<invbind*> r;
	for(auto i : ibinds)
		r.insert(i.second);
	return r;
}

invbind* mapper::get_inverse(const std::string& command) throw(std::bad_alloc)
{
	umutex_class u(mutex);
	if(ibinds.count(command))
		return ibinds[command];
	else
		return NULL;
}

std::set<ctrlrkey*> mapper::get_controller_keys() throw(std::bad_alloc)
{
	umutex_class u(mutex);
	std::set<ctrlrkey*> r;
	for(auto i : ckeys)
		r.insert(i.second);
	return r;
}

ctrlrkey* mapper::get_controllerkey(const std::string& command) throw(std::bad_alloc)
{
	umutex_class u(mutex);
	if(ckeys.count(command))
		return ckeys[command];
	else
		return NULL;
}

void mapper::do_register_inverse(const std::string& name, invbind& ibind) throw(std::bad_alloc)
{
	umutex_class u(mutex);
	ibinds[name] = &ibind;
	//Search for matches.
	for(auto i : bindings)
		if(i.second == ibind.cmd) {
			umutex_class u2(ibind.mutex);
			ibind.specs.push_back(i.first.as_keyspec());
		}
}

void mapper::do_unregister_inverse(const std::string& name) throw(std::bad_alloc)
{
	umutex_class u(mutex);
	ibinds.erase(name);
}

void mapper::do_register_ckey(const std::string& name, ctrlrkey& ckey) throw(std::bad_alloc)
{
	umutex_class u(mutex);
	ckeys[name] = &ckey;
}

void mapper::do_unregister_ckey(const std::string& name) throw(std::bad_alloc)
{
	umutex_class u(mutex);
	ckeys.erase(name);
}

keyboard& mapper::get_keyboard() throw()
{
	return kbd;
}

mapper::mapper(keyboard& _kbd, command::group& _domain) throw(std::bad_alloc)
	: inverse_proxy(*this), controllerkey_proxy(*this), kbd(_kbd), domain(_domain)
{
	register_queue<_inverse_proxy, invbind>::do_ready(inverse_proxy, true);
	register_queue<_controllerkey_proxy, ctrlrkey>::do_ready(controllerkey_proxy, true);
}

mapper::~mapper() throw()
{
	register_queue<_inverse_proxy, invbind>::do_ready(inverse_proxy, false);
	register_queue<_controllerkey_proxy, ctrlrkey>::do_ready(controllerkey_proxy, false);
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

keyspec mapper::triplet::as_keyspec() const throw(std::bad_alloc)
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

std::list<keyspec> mapper::get_bindings() throw(std::bad_alloc)
{
	umutex_class u(mutex);
	std::list<keyspec> r;
	for(auto i : bindings)
		r.push_back(i.first.as_keyspec());
	return r;
}

command::group& mapper::get_command_group() throw()
{
	return domain;
}

void mapper::bind(std::string mod, std::string modmask, std::string keyname, std::string command)
	throw(std::bad_alloc, std::runtime_error)
{
	keyspec spec;
	spec.mod = mod;
	spec.mask = modmask;
	spec.key = keyname;
	triplet t(kbd, spec);
	umutex_class u(mutex);
	if(bindings.count(t))
		throw std::runtime_error("Key is already bound");
	if(!listening.count(t._key)) {
		t._key->add_listener(*this, false);
		listening.insert(t._key);
	}
	std::string old_command;
	if(bindings.count(t))
		old_command = bindings[t];
	bindings[t] = command;
	change_command(spec, old_command, command);
}

void mapper::unbind(std::string mod, std::string modmask, std::string keyname) throw(std::bad_alloc,
	std::runtime_error)
{
	keyspec spec;
	spec.mod = mod;
	spec.mask = modmask;
	spec.key = keyname;
	triplet t(kbd, spec);
	umutex_class u(mutex);
	if(!bindings.count(t))
		throw std::runtime_error("Key is not bound");
	//No harm at leaving listeners listening.
	std::string old_command;
	if(bindings.count(t))
		old_command = bindings[t];
	bindings.erase(t);
	change_command(spec, old_command, "");
}

std::string mapper::get(const keyspec& keyspec) throw(std::bad_alloc)
{
	triplet t(kbd, keyspec);
	umutex_class u(mutex);
	if(!bindings.count(t))
		return "";
	return bindings[t];
}

void mapper::change_command(const keyspec& spec, const std::string& old, const std::string& newc)
{
	if(old != "" && ibinds.count(old)) {
		auto& i = ibinds[old];
		{
			umutex_class u2(i->mutex);
			i->specs.clear();
		}
		for(auto j : bindings)
			if(j.second == i->cmd && j.first.as_keyspec() != spec) {
				umutex_class u2(i->mutex);
				i->specs.push_back(j.first.as_keyspec());
			}
	}
	if(newc != "" && ibinds.count(newc)) {
		auto& i = ibinds[newc];
		umutex_class u2(i->mutex);
		i->specs.push_back(spec);
	}
}

void mapper::set(const keyspec& keyspec, const std::string& cmd) throw(std::bad_alloc,
	std::runtime_error)
{
	triplet t(kbd, keyspec);
	umutex_class u(mutex);
	if(!listening.count(t._key)) {
		t._key->add_listener(*this, false);
		listening.insert(t._key);
	}
	std::string oldcmd;
	if(bindings.count(t))
		oldcmd = bindings[t];
	bindings[t] = cmd;
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
	triplet llow(key, skey);
	triplet lhigh(key, skey + 1);
	auto low = bindings.lower_bound(llow);
	auto high = bindings.lower_bound(lhigh);
	for(auto i = low; i != high; i++) {
		if(!mods.triggers(i->first.mod, i->first.mask))
			continue;
		std::string cmd = fixup_command_polarity(i->second, polarity);
		if(cmd != "")
			domain.invoke(cmd);
	}
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
	throw(std::bad_alloc)
{
	umutex_class u(mutex);
	std::list<ctrlrkey*> r;
	for(auto i : ckeys) {
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

invbind::invbind(mapper& kmapper, const std::string& _command, const std::string& _name)
	throw(std::bad_alloc)
	: _mapper(kmapper), cmd(_command), oname(_name)
{
	register_queue<mapper::_inverse_proxy, invbind>::do_register(_mapper.inverse_proxy, cmd, *this);
}

invbind::~invbind() throw()
{
	register_queue<mapper::_inverse_proxy, invbind>::do_unregister(_mapper.inverse_proxy, cmd);
}

keyspec invbind::get(unsigned index) throw(std::bad_alloc)
{
	umutex_class u(mutex);
	if(index >= specs.size())
		return keyspec();
	return specs[index];
}

void invbind::clear(unsigned index) throw(std::bad_alloc)
{
	keyspec unbind;
	{
		umutex_class u(mutex);
		if(index >= specs.size())
			return;
		unbind = specs[index];
	}
	if(unbind)
		_mapper.set(unbind, "");
}

void invbind::append(const keyspec& keyspec) throw(std::bad_alloc)
{
	_mapper.set(keyspec, cmd);
}

std::string invbind::getname() throw(std::bad_alloc)
{
	return oname;
}

ctrlrkey::ctrlrkey(mapper& kmapper, const std::string& _command, const std::string& _name,
	bool _axis) throw(std::bad_alloc)
	: _mapper(kmapper), cmd(_command), oname(_name)
{
	register_queue<mapper::_controllerkey_proxy, ctrlrkey>::do_register(_mapper.controllerkey_proxy,
		cmd, *this);
	axis = _axis;
}

ctrlrkey::~ctrlrkey() throw()
{
	register_queue<mapper::_controllerkey_proxy, ctrlrkey>::do_unregister(_mapper.controllerkey_proxy, cmd);
}

std::pair<key*, unsigned> ctrlrkey::get(unsigned index) throw()
{
	umutex_class u(mutex);
	if(index >= keys.size())
		return std::make_pair(reinterpret_cast<key*>(NULL), 0);
	return keys[index];
}

std::string ctrlrkey::get_string(unsigned index) throw(std::bad_alloc)
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
	umutex_class u(mutex);
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
	umutex_class u(mutex);
	std::pair<key*, unsigned> mkey = std::make_pair(_key, _subkey);
	for(auto i = keys.begin(); i != keys.end(); i++) {
		if(*i == mkey) {
			mkey.first->remove_listener(*this);
			keys.erase(i);
			return;
		}
	}
}

void ctrlrkey::append(const std::string& _key) throw(std::bad_alloc, std::runtime_error)
{
	auto g = keymapper_lookup_subkey(_mapper.get_keyboard(), _key, axis);
	append(g.first, g.second);
}

std::pair<key*, unsigned> keymapper_lookup_subkey(keyboard& kbd, const std::string& name,
	bool axis) throw(std::bad_alloc, std::runtime_error)
{
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