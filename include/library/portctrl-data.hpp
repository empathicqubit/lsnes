#ifndef _library__portctrl_data__hpp__included__
#define _library__portctrl_data__hpp__included__

#include <cstring>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <set>
#include "json.hpp"
#include "threads.hpp"
#include "memtracker.hpp"

namespace binarystream
{
	class input;
	class output;
}

/**
 * Memory to allocate for controller frame.
 */
#define MAXIMUM_CONTROLLER_FRAME_SIZE 128
/**
 * Maximum amount of data frame::display() can write.
 */
#define MAX_DISPLAY_LENGTH 128
/**
 * Maximum amount of data frame::serialize() can write.
 */
#define MAX_SERIALIZED_SIZE 256
/**
 * Size of controller page.
 */
#define CONTROLLER_PAGE_SIZE 65500
/**
 * Special return value for deserialize() indicating no input was taken.
 */
#define DESERIALIZE_SPECIAL_BLANK 0xFFFFFFFFUL

namespace portctrl
{
extern const char* movie_page_id;
/**
 * Is not field terminator.
 *
 * Parameter ch: The character.
 * Returns: True if character is not terminator, false if character is terminator.
 */
inline bool is_nonterminator(char ch) throw()
{
	return (ch != '|' && ch != '\r' && ch != '\n' && ch != '\0');
}

/**
 * Read button value.
 *
 * Parameter buf: Buffer to read from.
 * Parameter idx: Index to buffer. Updated.
 * Returns: The read value.
 */
inline bool read_button_value(const char* buf, size_t& idx) throw()
{
	char ch = buf[idx];
	if(is_nonterminator(ch))
		idx++;
	return (ch != '|' && ch != '\r' && ch != '\n' && ch != '\0' && ch != '.' && ch != ' ' && ch != '\t');
}

/**
 * Read axis value.
 *
 * Parameter buf: Buffer to read from.
 * Parameter idx: Index to buffer. Updated.
 * Returns: The read value.
 */
short read_axis_value(const char* buf, size_t& idx) throw();

/**
 * Write axis value.
 *
 * Parameter buf: The buffer to write to.
 * Parameter _v: The axis value.
 * Returns: Number of bytes written.
 */
size_t write_axis_value(char* buf, short _v);


/**
 * Skip whitespace.
 *
 * Parameter buf: Buffer to read from.
 * Parameter idx: Index to buffer. Updated.
 */
inline void skip_field_whitespace(const char* buf, size_t& idx) throw()
{
	while(buf[idx] == ' ' || buf[idx] == '\t')
		idx++;
}

/**
 * Skip rest of the field.
 *
 * Parameter buf: Buffer to read from.
 * Parameter idx: Index to buffer. Updated.
 * Parameter include_pipe: If true, also skip the '|'.
 */
inline void skip_rest_of_field(const char* buf, size_t& idx, bool include_pipe) throw()
{
	while(is_nonterminator(buf[idx]))
		idx++;
	if(include_pipe && buf[idx] == '|')
		idx++;
}

/**
 * Serialize short.
 */
inline void serialize_short(unsigned char* buf, short val)
{
	buf[0] = static_cast<unsigned short>(val) >> 8;
	buf[1] = static_cast<unsigned short>(val);
}

/**
 * Serialize short.
 */
inline short unserialize_short(const unsigned char* buf)
{
	return static_cast<short>((static_cast<unsigned short>(buf[0]) << 8) | static_cast<unsigned short>(buf[1]));
}

class type;

/**
 * Index triple.
 *
 * Note: The index 0 has to be mapped to triple (0, 0, 0).
 */
struct index_triple
{
/**
 * If true, the other parameters are valid. Otherwise this index doesn't correspond to anything valid, but still
 * exists. The reason for having invalid entries is to be backward-compatible.
 */
	bool valid;
/**
 * The port number.
 */
	unsigned port;
/**
 * The controller number.
 */
	unsigned controller;
/**
 * The control number.
 */
	unsigned control;
};

/**
 * Controller index mappings
 */
struct index_map
{
/**
 * The poll indices.
 */
	std::vector<index_triple> indices;
/**
 * The logical controller mappings.
 */
	std::vector<std::pair<unsigned, unsigned>> logical_map;
/**
 * Legacy PCID mappings.
 */
	std::vector<std::pair<unsigned, unsigned>> pcid_map;
};


class type_set;

/**
 * A button or axis on controller
 */
struct button
{
/**
 * Type of button
 */
	enum _type
	{
		TYPE_NULL,	//Nothing (except takes the slot).
		TYPE_BUTTON,	//Button.
		TYPE_AXIS,	//Axis.
		TYPE_RAXIS,	//Relative Axis (mouse).
		TYPE_TAXIS,	//Throttle Axis (does not pair).
		TYPE_LIGHTGUN,	//Lightgun axis.
	};
	enum _type type;
	char32_t symbol;
	text name;
	bool shadow;
	int16_t rmin;		//Range min.
	int16_t rmax;		//Range max.
	bool centers;
	text macro;	//Name in macro (must be prefix-free).
	char msymbol;		//Symbol in movie.
/**
 * Is analog?
 */
	bool is_analog() const throw() { return type == (TYPE_AXIS) || (type == TYPE_RAXIS) || (type == TYPE_TAXIS)
		|| (type == TYPE_LIGHTGUN); }
};

/**
 * A controller.
 */
struct controller
{
	text cclass;				//Controller class.
	text type;				//Controller type.
	std::vector<button> buttons;	//Buttons.
/**
 * Count number of analog actions on this controller.
 */
	unsigned analog_actions() const;
/**
 * Get the axis numbers of specified analog action. If no valid axis exists, returns UINT_MAX.
 */
	std::pair<unsigned, unsigned> analog_action(unsigned i) const;
/**
 * Get specified button, or NULL if it doesn't exist.
 */
	struct button* get(unsigned i)
	{
		if(i >= buttons.size())
			return NULL;
		return &buttons[i];
	}
};

/**
 * A port controller set
 */
struct controller_set
{
	text iname;
	text hname;
	text symbol;
	std::vector<controller> controllers;	//Controllers.
	std::set<unsigned> legal_for;			//Ports this is legal for
/**
 * Get specified controller, or NULL if it doesn't exist.
 */
	struct controller* get(unsigned c) throw()
	{
		if(c >= controllers.size())
			return NULL;
		return &controllers[c];
	}
/**
 * Get specified button, or NULL if it doesn't exist.
 */
	struct button* get(unsigned c, unsigned i) throw();
};

/**
 * Type of controller.
 */
class type
{
public:
/**
 * Create a new port type.
 *
 * Parameter iname: Internal name of the port type.
 * Parameter hname: Human-readable name of the port type.
 * Parameter ssize: The storage size in bytes.
 * Throws std::bad_alloc: Not enough memory.
 */
	type(const text& iname, const text& hname, size_t ssize) throw(std::bad_alloc);
/**
 * Unregister a port type.
 */
	virtual ~type() throw();
/**
 * Writes controller data into compressed representation.
 *
 * Parameter buffer: The buffer storing compressed representation of controller state.
 * Parameter idx: Index of controller.
 * Parameter ctrl: The control to manipulate.
 * Parameter x: New value for control. Only zero/nonzero matters for buttons.
 */
	void (*write)(const type* _this, unsigned char* buffer, unsigned idx, unsigned ctrl, short x);
/**
 * Read controller data from compressed representation.
 *
 * Parameter buffer: The buffer storing compressed representation of controller state.
 * Parameter idx: Index of controller.
 * Parameter ctrl: The control to query.
 * Returns: The value of control. Buttons return 0 or 1.
 */
	short (*read)(const type* _this, const unsigned char* buffer, unsigned idx, unsigned ctrl);
/**
 * Take compressed controller data and serialize it into textual representation.
 *
 * - The initial '|' is also written.
 *
 * Parameter buffer: The buffer storing compressed representation of controller state.
 * Parameter textbuf: The text buffer to write to.
 * Returns: Number of bytes written.
 */
	size_t (*serialize)(const type* _this, const unsigned char* buffer, char* textbuf);
/**
 * Unserialize textual representation into compressed controller state.
 *
 * - Only stops reading on '|', NUL, CR or LF in the final read field. That byte is not read.
 *
 * Parameter buffer: The buffer storing compressed representation of controller state.
 * Parameter textbuf: The text buffer to read.
 * Returns: Number of bytes read.
 * Throws std::runtime_error: Bad serialization.
 */
	size_t (*deserialize)(const type* _this, unsigned char* buffer, const char* textbuf);
/**
 * Is the device legal for port?
 *
 * Parameter port: Port to query.
 * Returns: Nonzero if legal, zero if illegal.
 */
	int legal(unsigned port)
	{
		return controller_info->legal_for.count(port) ? 1 : 0;
	}
/**
 * Controller info.
 */
	controller_set* controller_info;
/**
 * Get number of used control indices on controller.
 *
 * Parameter controller: Number of controller.
 * Returns: Number of used control indices.
 */
	unsigned used_indices(unsigned controller)
	{
		auto c = controller_info->get(controller);
		return c ? c->buttons.size() : 0;
	}
/**
 * Human-readable name.
 */
	text hname;
/**
 * Number of bytes it takes to store this.
 */
	size_t storage_size;
/**
 * Name of port type.
 */
	text name;
/**
 * Is given controller present?
 */
	bool is_present(unsigned controller) const throw();
private:
	type(const type&);
	type& operator=(const type&);
};

/**
 * A set of port types.
 */
class type_set
{
public:
/**
 * Create empty port type set.
 */
	type_set() throw();
/**
 * Make a port type set with specified types. If called again with the same parameters, returns the same object.
 *
 * Parameter types: The types.
 * Parameter control_map: The control map
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Illegal port types.
 */
	static type_set& make(std::vector<type*> types, struct index_map control_map)
		throw(std::bad_alloc, std::runtime_error);
/**
 * Compare sets for equality.
 */
	bool operator==(const type_set& s) const throw() { return this == &s; }
/**
 * Compare sets for non-equality.
 */
	bool operator!=(const type_set& s) const throw() { return this != &s; }
/**
 * Get offset of specified port.
 *
 * Parameter port: The number of port.
 * Returns: The offset of port.
 * Throws std::runtime_error: Bad port number.
 */
	size_t port_offset(unsigned port) const throw(std::runtime_error)
	{
		if(port >= port_count)
			throw std::runtime_error("Invalid port index");
		return port_offsets[port];
	}
/**
 * Get type of specified port.
 *
 * Parameter port: The number of port.
 * Returns: The port type.
 * Throws std::runtime_error: Bad port number.
 */
	const class type& port_type(unsigned port) const throw(std::runtime_error)
	{
		if(port >= port_count)
			throw std::runtime_error("Invalid port index");
		return *(port_types[port]);
	}
/**
 * Get number of ports.
 *
 * Returns: The port count.
 */
	unsigned ports() const throw()
	{
		return port_count;
	}
/**
 * Get total size of controller data.
 *
 * Returns the size.
 */
	unsigned size() const throw()
	{
		return total_size;
	}
/**
 * Get total index count.
 */
	unsigned indices() const throw()
	{
		return _indices.size();
	}
/**
 * Look up the triplet for given control.
 *
 * Parameter index: The index to look up.
 * Returns: The triplet (may not be valid).
 * Throws std::runtime_error: Index out of range.
 */
	index_triple index_to_triple(unsigned index) const throw(std::runtime_error)
	{
		if(index >= _indices.size())
			throw std::runtime_error("Invalid index");
		return _indices[index];
	}
/**
 * Translate triplet into index.
 *
 * Parameter port: The port.
 * Parameter controller: The controller.
 * Parameter _index: The control index.
 * Returns: The index, or 0xFFFFFFFFUL if specified triple is not valid.
 */
	unsigned triple_to_index(unsigned port, unsigned controller, unsigned _index) const throw(std::runtime_error)
	{
		size_t place = port * port_multiplier + controller * controller_multiplier + _index;
		if(place >= indices_size)
			return 0xFFFFFFFFUL;
		unsigned pindex = indices_tab[place];
		if(pindex == 0xFFFFFFFFUL)
			return 0xFFFFFFFFUL;
		const struct index_triple& t = _indices[pindex];
		if(!t.valid || t.port != port || t.controller != controller || t.control != _index)
			return 0xFFFFFFFFUL;
		return pindex;
	}
/**
 * Return number of controllers connected.
 *
 * Returns: Number of controllers.
 */
	unsigned number_of_controllers() const throw()
	{
		return controllers.size();
	}
/**
 * Lookup physical controller index corresponding to logical one.
 *
 * Parameter lcid: Logical controller id.
 * Returns: Physical controller index (port, controller).
 * Throws std::runtime_error: No such controller.
 */
	std::pair<unsigned, unsigned> lcid_to_pcid(unsigned lcid) const throw(std::runtime_error)
	{
		if(lcid >= controllers.size())
			throw std::runtime_error("Bad logical controller");
		return controllers[lcid];
	}
/**
 * Return number of legacy PCIDs.
 */
	unsigned number_of_legacy_pcids() const throw()
	{
		return legacy_pcids.size();
	}
/**
 * Lookup (port,controller) pair corresponding to given legacy pcid.
 *
 * Parameter pcid: The legacy pcid.
 * Returns: The controller index.
 * Throws std::runtime_error: No such controller.
 */
	std::pair<unsigned, unsigned> legacy_pcid_to_pair(unsigned pcid) const throw(std::runtime_error)
	{
		if(pcid >= legacy_pcids.size())
			throw std::runtime_error("Bad legacy PCID");
		return legacy_pcids[pcid];
	}
private:
	type_set(std::vector<class type*> types, struct index_map control_map);
	size_t* port_offsets;
	class type** port_types;
	unsigned port_count;
	size_t total_size;
	std::vector<index_triple> _indices;
	std::vector<std::pair<unsigned, unsigned>> controllers;
	std::vector<std::pair<unsigned, unsigned>> legacy_pcids;

	size_t port_multiplier;
	size_t controller_multiplier;
	size_t indices_size;
	unsigned* indices_tab;
};

/**
 * Poll counter vector.
 */
class counters
{
public:
/**
 * Create new pollcounter vector filled with all zeroes and all DRDY bits clear.
 *
 * Throws std::bad_alloc: Not enough memory.
 */
	counters() throw(std::bad_alloc);
/**
 * Create new pollcounter vector suitably sized for given type set.
 *
 * Parameter p: The port types.
 * Throws std::bad_alloc: Not enough memory.
 */
	counters(const type_set& p) throw(std::bad_alloc);
/**
 * Destructor.
 */
	~counters() throw();
/**
 * Copy the counters.
 */
	counters(const counters& v) throw(std::bad_alloc);
/**
 * Assign the counters.
 */
	counters& operator=(const counters& v) throw(std::bad_alloc);
/**
 * Zero all poll counters and clear all DRDY bits. System flag is cleared.
 */
	void clear() throw();
/**
 * Set all DRDY bits.
 */
	void set_all_DRDY() throw();
/**
 * Clear specified DRDY bit.
 *
 * Parameter port: The port.
 * Parameter controller: The controller
 * Parameter ctrl: The control id.
 */
	void clear_DRDY(unsigned port, unsigned controller, unsigned ctrl) throw()
	{
		unsigned i = types->triple_to_index(port, controller, ctrl);
		if(i != 0xFFFFFFFFU)
			clear_DRDY(i);
	}
/**
 * Clear state of DRDY bit.
 *
 * Parameter idx: The control index.
 */
	void clear_DRDY(unsigned idx) throw();
/**
 * Get state of DRDY bit.
 *
 * Parameter port: The port.
 * Parameter controller: The controller
 * Parameter ctrl: The control id.
 * Returns: The DRDY state.
 */
	bool get_DRDY(unsigned port, unsigned controller, unsigned ctrl) throw()
	{
		unsigned i = types->triple_to_index(port, controller, ctrl);
		if(i != 0xFFFFFFFFU)
			return get_DRDY(i);
		else
			return true;
	}
/**
 * Get state of DRDY bit.
 *
 * Parameter idx: The control index.
 * Returns: The DRDY state.
 */
	bool get_DRDY(unsigned idx) throw();
/**
 * Is any poll count nonzero or is system flag set?
 *
 * Returns: True if at least one poll count is nonzero or if system flag is set. False otherwise.
 */
	bool has_polled() throw();
/**
 * Read the actual poll count on specified control.
 *
 * Parameter port: The port.
 * Parameter controller: The controller
 * Parameter ctrl: The control id.
 * Return: The poll count.
 */
	uint32_t get_polls(unsigned port, unsigned controller, unsigned ctrl) throw()
	{
		unsigned i = types->triple_to_index(port, controller, ctrl);
		if(i != 0xFFFFFFFFU)
			return get_polls(i);
		else
			return 0;
	}
/**
 * Read the actual poll count on specified control.
 *
 * Parameter idx: The control index.
 * Return: The poll count.
 */
	uint32_t get_polls(unsigned idx) throw();
/**
 * Increment poll count on specified control.
 *
 * Parameter port: The port.
 * Parameter controller: The controller
 * Parameter ctrl: The control id.
 * Return: The poll count pre-increment.
 */
	uint32_t increment_polls(unsigned port, unsigned controller, unsigned ctrl) throw()
	{
		unsigned i = types->triple_to_index(port, controller, ctrl);
		if(i != 0xFFFFFFFFU)
			return increment_polls(i);
		else
			return 0;
	}
/**
 * Increment poll count on specified index.
 *
 * Parameter idx: The index.
 * Return: The poll count pre-increment.
 */
	uint32_t increment_polls(unsigned idx) throw();
/**
 * Get highest poll counter value.
 *
 * - System flag counts as 1 poll.
 *
 * Returns: The maximum poll count (at least 1 if system flag is set).
 */
	uint32_t max_polls() throw();
/**
 * Save state to memory block.
 *
 * Parameter mem: The memory block to save to.
 * Throws std::bad_alloc: Not enough memory.
 */
	void save_state(std::vector<uint32_t>& mem) throw(std::bad_alloc);
/**
 * Load state from memory block.
 *
 * Parameter mem: The block from restore from.
 */
	void load_state(const std::vector<uint32_t>& mem) throw();
/**
 * Check if state can be loaded without errors.
 *
 * Returns: True if load is possible, false otherwise.
 */
	bool check(const std::vector<uint32_t>& mem) throw();
/**
 * Set/Clear the frame parameters polled flag.
 */
	void set_framepflag(bool value) throw();
/**
 * Get the frame parameters polled flag.
 */
	bool get_framepflag() const throw();
/**
 * Get raw pollcounter data.
 */
	const uint32_t* rawdata() const throw() { return ctrs; }
private:
	uint32_t* ctrs;
	const type_set* types;
	bool framepflag;
};

class frame_vector;

/**
 * Single (sub)frame of controls.
 */
class frame
{
public:
/**
 * Default constructor. Invalid port types, dedicated memory.
 */
	frame() throw();
/**
 * Create subframe of controls with specified controller types and dedicated memory.
 *
 * Parameter p: Types of ports.
 */
	frame(const type_set& p) throw(std::runtime_error);
/**
 * Create subframe of controls with specified controller types and specified memory.
 *
 * Parameter memory: The backing memory.
 * Parameter p: Types of ports.
 * Parameter host: Host frame vector.
 *
 * Throws std::runtime_error: NULL memory.
 */
	frame(unsigned char* memory, const type_set& p, frame_vector* host = NULL)
		throw(std::runtime_error);
/**
 * Copy construct a frame. The memory will be dedicated.
 *
 * Parameter obj: The object to copy.
 */
	frame(const frame& obj) throw();
/**
 * Assign a frame. The types must either match or memory must be dedicated.
 *
 * Parameter obj: The object to copy.
 * Returns: Reference to this.
 * Throws std::runtime_error: The types don't match and memory is not dedicated.
 */
	frame& operator=(const frame& obj) throw(std::runtime_error);
/**
 * Get type of port.
 *
 * Parameter port: Number of port.
 * Returns: The type of port.
 */
	const type& get_port_type(unsigned port) throw()
	{
		return types->port_type(port);
	}
/**
 * Get port count.
 */
	unsigned get_port_count() throw()
	{
		return types->ports();
	}
/**
 * Get index count.
 */
	unsigned get_index_count() throw()
	{
		return types->indices();
	}
/**
 * Set types of ports.
 *
 * Parameter ptype: New port types.
 * Throws std::runtime_error: Memory is mapped.
 */
	void set_types(const type_set& ptype) throw(std::runtime_error)
	{
		if(memory != backing)
			throw std::runtime_error("Can't change type of mapped frame");
		types = &ptype;
	}
/**
 * Get blank dedicated frame of same port types.
 *
 * Return blank frame.
 */
	frame blank_frame() throw()
	{
		return frame(*types);
	}
/**
 * Check that types match.
 *
 * Parameter obj: Another object.
 * Returns: True if types match, false otherwise.
 */
	bool types_match(const frame& obj) const throw()
	{
		return types == obj.types;
	}
/**
 * Perform XOR between controller frames.
 *
 * Parameter another: The another object.
 * Returns: The XOR result (dedicated memory).
 * Throws std::runtime_error: Type mismatch.
 */
	frame operator^(const frame& another) throw(std::runtime_error)
	{
		frame x(*this);
		if(types != another.types)
				throw std::runtime_error("frame::operator^: Type mismatch");
		for(size_t i = 0; i < types->size(); i++)
			x.backing[i] ^= another.backing[i];
		return x;
	}
/**
 * Set the sync flag.
 *
 * Parameter x: The value to set the sync flag to.
 */
	inline void sync(bool x) throw();
/**
 * Get the sync flag.
 *
 * Return value: Value of sync flag.
 */
	bool sync() throw()
	{
		return ((backing[0] & 1) != 0);
	}
/**
 * Quick get sync flag for buffer.
 */
	static bool sync(const unsigned char* mem) throw()
	{
		return ((mem[0] & 1) != 0);
	}
/**
 * Get size of frame.
 *
 * Returns: The number of bytes it takes to store frame of this type.
 */
	size_t size()
	{
		return types->size();
	}
/**
 * Set axis/button value.
 *
 * Parameter port: The port.
 * Parameter controller: The controller
 * Parameter ctrl: The control id.
 * Parameter x: The new value.
 */
	void axis3(unsigned port, unsigned controller, unsigned ctrl, short x) throw()
	{
		if(port >= types->ports())
			return;
		auto& t = types->port_type(port);
		if(!port && !controller && !ctrl && host)
			sync(x != 0);
		else
			t.write(&t, backing + types->port_offset(port), controller, ctrl, x);
	}
/**
 * Set axis/button value.
 *
 * Parameter idx: Control index.
 * Parameter x: The new value.
 */
	void axis2(unsigned idx, short x) throw()
	{
		index_triple t = types->index_to_triple(idx);
		if(t.valid)
			axis3(t.port, t.controller, t.control, x);
	}
/**
 * Get axis/button value.
 *
 * Parameter port: The port.
 * Parameter controller: The controller
 * Parameter ctrl: The control id.
 * Return value: The axis value.
 */
	short axis3(unsigned port, unsigned controller, unsigned ctrl) throw()
	{
		if(port >= types->ports())
			return 0;
		auto& t = types->port_type(port);
		return t.read(&t, backing + types->port_offset(port), controller, ctrl);
	}

/**
 * Get axis/button value.
 *
 * Parameter idx: Index of control.
 * Return value: The axis value.
 */
	short axis2(unsigned idx) throw()
	{
		index_triple t = types->index_to_triple(idx);
		if(t.valid)
			return axis3(t.port, t.controller, t.control);
		else
			return 0;
	}
/**
 * Get controller display.
 *
 * Parameter port: The port.
 * Parameter controller: The controller
 * Parameter buf: Buffer to write nul-terminated display to.
 */
	void display(unsigned port, unsigned controller, char32_t* buf) throw();
/**
 * Is device present?
 *
 * Parameter port: The port.
 * Parameter controller: The controller
 * Returns: True if present, false if not.
 */
	bool is_present(unsigned port, unsigned controller) throw()
	{
		if(port >= types->ports())
			return false;
		return types->port_type(port).is_present(controller);
	}
/**
 * Deserialize frame from text format.
 *
 * Parameter buf: The buffer containing text representation. Terminated by NUL, CR or LF.
 * Throws std::runtime_error: Bad serialized representation.
 */
	inline void deserialize(const char* buf) throw(std::runtime_error);
/**
 * Serialize frame to text format.
 *
 * Parameter buf: The buffer to write NUL-terminated text representation to.
 */
	void serialize(char* buf) throw()
	{
		size_t offset = 0;
		for(size_t i = 0; i < types->ports(); i++) {
			auto& t = types->port_type(i);
			offset += t.serialize(&t, backing + types->port_offset(i), buf + offset);
		}
		buf[offset++] = '\0';
	}
/**
 * Return copy with dedicated memory.
 *
 * Parameter sync: If set, the frame will have sync flag set, otherwise it will have sync flag clear.
 * Returns: Copy of this frame.
 */
	frame copy(bool sync)
	{
		frame c(*this);
		c.sync(sync);
		return c;
	}
/**
 * Compare two frames.
 *
 * Parameter obj: Another frame.
 * Returns: True if equal, false if not.
 */
	bool operator==(const frame& obj) const throw()
	{
		if(!types_match(obj))
			return false;
		return !memcmp(backing, obj.backing, types->size());
	}
/**
 * Compare two frames.
 *
 * Parameter obj: Another frame.
 * Returns: True if not equal, false if equal.
 */
	bool operator!=(const frame& obj) const throw()
	{
		return !(*this == obj);
	}
/**
 * Get the port type set.
 */
	const type_set& porttypes()
	{
		return *types;
	}
private:
	unsigned char memory[MAXIMUM_CONTROLLER_FRAME_SIZE];
	unsigned char* backing;
	frame_vector* host;
	const type_set* types;
};

/**
 * Vector of controller frames.
 */
class frame_vector
{
public:
/**
 * Framecount change listener.
 */
	class fchange_listener
	{
	public:
/**
 * Destructor.
 */
		virtual ~fchange_listener();
/**
 * Notify a change.
 */
		virtual void notify(frame_vector& src, uint64_t old) = 0;
	};
/**
 * Construct new controller frame vector.
 */
	frame_vector() throw();
/**
 * Construct new controller frame vector.
 *
 * Parameter p: The port types.
 */
	frame_vector(const type_set& p) throw();
/**
 * Destroy controller frame vector
 */
	~frame_vector() throw();
/**
 * Copy controller frame vector.
 *
 * Parameter obj: The object to copy.
 * Throws std::bad_alloc: Not enough memory.
 */
	frame_vector(const frame_vector& vector) throw(std::bad_alloc);
/**
 * Assign controller frame vector.
 *
 * Parameter obj: The object to copy.
 * Returns: Reference to this.
 * Throws std::bad_alloc: Not enough memory.
 */
	frame_vector& operator=(const frame_vector& vector) throw(std::bad_alloc);
/**
 * Blank vector and change the type of ports.
 *
 * Parameter p: The port types.
 */
	void clear(const type_set& p) throw(std::runtime_error);
/**
 * Blank vector.
 */
	void clear() throw()
	{
		clear(*types);
	}
/**
 * Get number of subframes.
 */
	size_t size() const
	{
		return frames;
	}
/**
 * Get the typeset.
 */
	const type_set& get_types()
	{
		return *types;
	}
/**
 * Access specified subframe.
 *
 * Parameter x: The frame number.
 * Returns: The controller frame.
 * Throws std::runtime_error: Invalid frame index.
 */
	frame operator[](size_t x)
	{
		size_t page = x / frames_per_page;
		size_t pageoffset = frame_size * (x % frames_per_page);
		if(x >= frames)
			throw std::runtime_error("frame_vector::operator[]: Illegal index");
		if(page != cache_page_num) {
			cache_page = &pages[page];
			cache_page_num = page;
		}
		return frame(cache_page->content + pageoffset, *types, this);
	}
/**
 * Append a subframe.
 *
 * Parameter frame: The frame to append.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Port type mismatch.
 */
	void append(frame frame) throw(std::bad_alloc, std::runtime_error);
/**
 * Change length of vector.
 *
 * - Reducing length of vector will discard extra elements.
 * - Extending length of vector will add all-zero elements.
 *
 * Parameter newsize: New size of vector.
 * Throws std::bad_alloc: Not enough memory.
 */
	void resize(size_t newsize) throw(std::bad_alloc);
/**
 * Walk the indexes of sync subframes.
 *
 * - If frame is in range and there is at least one more sync subframe after it, the index of first sync subframe
 *   after given frame.
 * - If frame is in range, but there are no more sync subframes after it, the length of vector is returned.
 * - If frame is out of range, the given frame is returned.
 *
 * Parameter frame: The frame number to start search from.
 * Returns: Index of next sync frame.
 */
	size_t walk_sync(size_t frame) throw()
	{
		return walk_helper(frame, true);
	}
/**
 * Get number of subframes in frame. The given subframe is assumed to be sync subframe.
 *
 * - The return value is the same as (walk_sync(frame) - frame).
 *
 * Parameter frame: The frame number to start search from.
 * Returns: Number of subframes in this frame.
 */
	size_t subframe_count(size_t frame) throw()
	{
		return walk_helper(frame, false);
	}
/**
 * Count number of subframes in vector with sync flag set.
 *
 * Returns: The number of frames.
 */
	size_t count_frames() throw() { return real_frame_count; }
/**
 * Recount number of frames.
 *
 * This is to be used after direct editing of pointers obtained by get_page_buffer().
 *
 * Returns: The number of frames.
 */
	size_t recount_frames() throw();
/**
 * Return blank controller frame with correct type and dedicated memory.
 *
 * Parameter sync: If set, the frame will have sync flag set, otherwise it will have sync flag clear.
 * Returns: Blank frame.
 */
	frame blank_frame(bool sync)
	{
		frame c(*types);
		c.sync(sync);
		return c;
	}
/**
 * Return number of pages in movie.
 */
	size_t get_page_count() const { return pages.size(); }
/**
 * Return the stride.
 */
	size_t get_stride() const { return frame_size; }
/**
 * Return number of frames per page.
 */
	size_t get_frames_per_page() const { return frames_per_page; }
/**
 * Get content of given page.
 */
	unsigned char* get_page_buffer(size_t page) { return pages[page].content; }
/**
 * Get content of given page.
 */
	const unsigned char* get_page_buffer(size_t page) const { return pages.find(page)->second.content; }
/**
 * Get binary save size.
 *
 * Returns: The number of bytes for binary save.
 */
	uint64_t binary_size() const throw();
/**
 * Save in binary form.
 *
 * Parameter stream: The stream to save to.
 * Throws std::runtime_error: Error saving.
 */
	void save_binary(binarystream::output& stream) const throw(std::runtime_error);
/**
 * Load from binary form. May partially overwrite on failure.
 *
 * Parameter stream: The stream to load from.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Error saving.
 */
	void load_binary(binarystream::input& stream) throw(std::bad_alloc, std::runtime_error);
/**
 * Check that the movies are compatible up to a point.
 *
 * Parameter with: The 2nd frame vector to check.
 * Parameter frame: The frame number (1-based) to check to.
 * Parameter polls: The poll counters within frame to check to.
 * Returns: True if compatible, false if not.
 */
	bool compatible(frame_vector& with, uint64_t frame, const uint32_t* polls);
/**
 * Find subframe number corresponding to given frame (1-based).
 */
	int64_t find_frame(uint64_t n);
/**
 * Notify sync flag polarity change.
 *
 * Parameter polarity: 1 if positive edge, -1 if negative edge. 0 is ignored.
 */
	void notify_sync_change(short polarity) {
		uint64_t old_frame_count = real_frame_count;
		real_frame_count = real_frame_count + polarity;
		if(!freeze_count) call_framecount_notification(old_frame_count);
	}
/**
 * Set where to deliver frame count change notifications to.
 *
 * Parameter cb: Callback to register.
 * Returns: Handle for callback.
 */
	void set_framecount_notification(fchange_listener& cb)
	{
		threads::alock h(mlock);
		on_framecount_change.insert(&cb);
	}
/**
 * Clear framecount change notification.
 *
 * Parameter handle: Handle to clear.
 */
	void clear_framecount_notification(fchange_listener& cb)
	{
		threads::alock h(mlock);
		on_framecount_change.erase(&cb);
	}
/**
 * Call framecount change notification.
 */
	void call_framecount_notification(uint64_t oldcount)
	{
		std::set<fchange_listener*> tmp;
		{
			threads::alock h(mlock);
			tmp = on_framecount_change;
		}
		for(auto i : tmp)
			try { i->notify(*this, oldcount); } catch(...) {}
	}
/**
 * Swap frame data.
 *
 * Only the frame data is swapped, not the notifications.
 */
	void swap_data(frame_vector& v) throw();

/**
 * Freeze framecount notifications.
 */
	struct notify_freeze
	{
		notify_freeze(frame_vector& parent)
			: frozen(parent)
		{
			frozen.freeze_count++;
			if(frozen.freeze_count == 1)
				frozen.frame_count_at_freeze = frozen.real_frame_count;
		}
		~notify_freeze()
		{
			frozen.freeze_count--;
			if(frozen.freeze_count == 0)
				frozen.call_framecount_notification(frozen.frame_count_at_freeze);
		}
	private:
		notify_freeze(const notify_freeze&);
		notify_freeze& operator=(const notify_freeze&);
		frame_vector& frozen;
	};
private:
	friend class notify_freeze;
	class page
	{
	public:
		page() {
			memtracker::singleton()(movie_page_id, CONTROLLER_PAGE_SIZE + 36);
			memset(content, 0, CONTROLLER_PAGE_SIZE);
		}
		~page() { memtracker::singleton()(movie_page_id, -CONTROLLER_PAGE_SIZE - 36); }
		unsigned char content[CONTROLLER_PAGE_SIZE];
	};
	size_t frames_per_page;
	size_t frame_size;
	size_t frames;
	const type_set* types;
	size_t cache_page_num;
	page* cache_page;
	std::map<size_t, page> pages;
	uint64_t real_frame_count;
	uint64_t frame_count_at_freeze;
	size_t freeze_count;
	std::set<fchange_listener*> on_framecount_change;
	size_t walk_helper(size_t frame, bool sflag) throw();
	threads::lock mlock;
	void clear_cache()
	{
		cache_page_num = 0;
		cache_page_num--;
		cache_page = NULL;
	}
	memtracker::autorelease tracker;
};

void frame::sync(bool x) throw()
{
	short old = (backing[0] & 1);
	if(x)
		backing[0] |= 1;
	else
		backing[0] &= ~1;
	if(host) host->notify_sync_change((backing[0] & 1) - old);
}

void frame::deserialize(const char* buf) throw(std::runtime_error)
{
	short old = sync();
	size_t offset = 0;
	for(size_t i = 0; i < types->ports(); i++) {
		size_t s;
		auto& t = types->port_type(i);
		s = t.deserialize(&t, backing + types->port_offset(i), buf + offset);
		if(s != DESERIALIZE_SPECIAL_BLANK) {
			offset += s;
			while(is_nonterminator(buf[offset]))
				offset++;
			if(buf[offset] == '|')
				offset++;
		}
	}
	if(host) host->notify_sync_change(sync() - old);
}


//Parse a controller macro.
struct macro_data
{
	struct axis_transform
	{
		axis_transform() { coeffs[0] = coeffs[3] = 1; coeffs[1] = coeffs[2] = coeffs[4] = coeffs[5] = 0; }
		axis_transform(const text& expr);
		double coeffs[6];
		int16_t transform(const button& b, int16_t v);
		std::pair<int16_t, int16_t> transform(const button& b1,
			const button& b2, int16_t v1, int16_t v2);
		static double unscale_axis(const button& b, int16_t v);
		static int16_t scale_axis(const button& b, double v);
	};
	enum apply_mode
	{
		AM_OVERWRITE,
		AM_OR,
		AM_XOR
	};
	macro_data() { buttons = 0; }
	macro_data(const text& spec, const JSON::node& desc, unsigned i);
	macro_data(const JSON::node& ser, unsigned i);
	void serialize(JSON::node& v);
	static JSON::node make_descriptor(const controller& ctrl);
	const JSON::node& get_descriptor() { return _descriptor; }
	static bool syntax_check(const text& spec, const JSON::node& ctrl);
	void write(frame& frame, unsigned port, unsigned controller, int64_t nframe, apply_mode amode);
	text dump(const controller& ctrl); //Mainly for debugging.
	size_t get_frames() { return data.size() / get_stride(); }
	size_t get_stride() { return buttons; }
	size_t buttons;
	std::vector<unsigned char> data;
	std::vector<std::pair<unsigned, unsigned>> aaxes;
	std::vector<unsigned> btnmap;
	std::vector<axis_transform> adata;
	text orig;
	JSON::node _descriptor;
	bool enabled;
	bool autoterminate;
};

struct macro
{
	macro_data::apply_mode amode;
	std::map<unsigned, macro_data> macros;
	void write(frame& frame, int64_t nframe);
	macro() { amode = macro_data::AM_XOR; }
	macro(const JSON::node& ser);
	JSON::node serialize();
};


/**
 * Get generic default system port type.
 */
type& get_default_system_port_type();
}

#endif
