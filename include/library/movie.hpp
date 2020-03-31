#ifndef _library__movie__hpp__included__
#define _library__movie__hpp__included__

#include <string>
#include <cstdint>
#include <stdexcept>
#include "portctrl-data.hpp"
#include "memtracker.hpp"

/**
 * Movie being played back or recorded
 */
class movie
{
public:
/**
 * Construct new empty movie.
 *
 * throws std::bad_alloc: Not enough memory.
 */
	movie();

/**
 * Dtor.
 */
	~movie();

/**
 * Is the movie in readonly mode?
 *
 * returns: True if in read-only mode, false if in read-write mode.
 */
	bool readonly_mode() throw();

/**
 * Switches movie to read-only or read-write mode. If switching to read-write mode, the movie is truncated.
 *
 * parameter enable: If true, switch to read-only mode, else to read-write mode.
 * throws std::bad_alloc: Not enough memory.
 */
	void readonly_mode(bool enable);

/**
 * Returns the movie rerecord count (this is not the same thing as global rerecord count).
 *
 * returns: The movie rerecord count
 * throws std::bad_alloc: Not enough memory.
 */
	std::string rerecord_count();

/**
 * Sets the movie rerecord count (this is not the same thing as global rerecord count).
 *
 * parameter count: The new rerecord count
 * throws std::bad_alloc: Not enough memory.
 */
	void rerecord_count(const std::string& count);

/**
 * Read project ID
 *
 * returns: The project ID
 * throws std::bad_alloc: Not enough memory.
 */
	std::string project_id();

/**
 * brief Set project ID
 *
 * parameter id: New project ID.
 * throws std::bad_alloc: Not enough memory.
 */
	void project_id(const std::string& id);

/**
 * Get number of frames in movie
 *
 * returns: The number of frames.
 */
	uint64_t get_frame_count() throw() { return movie_data->count_frames(); }

/**
 * Get number of current frame in movie
 *
 * The first frame in movie is 1. 0 is "before first frame" value.
 *
 * returns: The number of frame
 */
	uint64_t get_current_frame() throw();

/**
 * Get number of lag frames so far
 *
 * returns: The number of lag frames.
 */
	uint64_t get_lag_frames() throw();

/**
 * This function advances to next frame in movie, discarding subframes not used. If the frame is lag frame, it is
 * counted as lag frame and subframe entry for it is made (if in readwrite mode).
 *
 * throws std::bad_alloc: Not enough memory.
 */
	void next_frame();

/**
 * Reads the data ready flag. On new frame, all data ready flags are unset. On reading control, its data ready
 * flag is unset.
 *
 * parameter pid: Physical controller id.
 * parameter index: Control ID.
 * returns: The read value.
 * throws std::logic_error: Invalid control index.
 */
	bool get_DRDY(unsigned port, unsigned controller, unsigned index);

/**
 * Set all data ready flags
 */
	void set_all_DRDY() throw();

/**
 * Poll a control by (port, controller, index) tuple.
 *
 * parameter pid: Physical controller ID.
 * parameter index: The index of control in controller (0 to 11)
 * returns: The read value
 * throws std::bad_alloc: Not enough memory.
 * throws std::logic_error: Invalid port, controller or index or before movie start.
 */
	short next_input(unsigned port, unsigned controller, unsigned index);

/**
 * Set current control values. These are read in readwrite mode.
 *
 * parameter controls: The new controls.
 */
	void set_controls(portctrl::frame controls) throw();

/**
 * Get current control values in effect.
 *
 * returns: Controls
 */
	portctrl::frame get_controls() throw();

/**
 * Loads a movie plus some other parameters. The playback pointer is positioned to start of movie and readonly
 * mode is enabled.
 *
 * parameter rerecs: Movie rerecord count.
 * parameter project_id: Project ID of movie.
 * parameter input: The input track.
 * throws std::bad_alloc: Not enough memory.
 * throws std::runtime_error: Bad movie data.
 */
	void load(const std::string& rerecs, const std::string& project_id, portctrl::frame_vector& input);

/**
 * This method serializes the state of movie code.
 *
 * Parameter proj_id: The project ID is written here.
 * Parameter curframe: Current frame is written here.
 * Parameter lagframes: Lag counter is written here.
 * Parameter pcounters: Poll counters are written here.
 * throws std::bad_alloc: Not enough memory.
 */
	void save_state(std::string& proj_id, uint64_t& curframe, uint64_t& lagframes,
		std::vector<uint32_t>& pcounters);

/**
 * Given previous serialized state from this movie, restore the state.
 *
 * Parameter curframe: Current frame.
 * Parameter lagframe: Lag counter.
 * Parameter pcounters: Poll counters.
 * Parameter ro: If true, restore in readonly mode.
 * Parameter old_movie: Old movie to check for compatiblity against.
 * Parameter old_projectid: Old project ID to check against.
 * Returns: ???
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Movie check failure.
 */
	size_t restore_state(uint64_t curframe, uint64_t lagframe, const std::vector<uint32_t>& pcounters, bool ro,
		portctrl::frame_vector* old_movie, const std::string& old_projectid);
/**
 * Reset the state of movie to initial state.
 */
	void reset_state() throw();
/**
 * Get how manyth poll in the frame next poll would be?
 *
 * returns: Poll number.
 */
	unsigned next_poll_number();
/**
 * Get how many subframes there are in specified frame.
 *
 * parameter frame: The frame number.
 * returns: Number of subframes (0 if outside movie).
 */
	uint64_t frame_subframes(uint64_t frame) throw();
/**
 * Read controls from specified subframe of specified frame.
 *
 * parameter frame: The frame number.
 * parameter subframe: Subframe within frame (first is 0).
 * returns: The controls for subframe. If subframe is too great, reads last present subframe. If frame is outside
 *	movie, reads all released.
 */
	portctrl::frame read_subframe(uint64_t frame, uint64_t subframe) throw();
/**
 * Fast save.
 */
	void fast_save(uint64_t& _frame, uint64_t& _ptr, uint64_t& _lagc, std::vector<uint32_t>& counters);
/**
 * Fast load.
 */
	void fast_load(uint64_t& _frame, uint64_t& _ptr, uint64_t& _lagc, std::vector<uint32_t>& counters);
/**
 * Poll flag handling.
 */
	class poll_flag
	{
	public:
		virtual ~poll_flag();
	/**
	 * Get the poll flag.
	 */
		virtual int get_pflag() = 0;
	/**
	 * Set the poll flag.
	 */
		virtual void set_pflag(int flag) = 0;
	};
/**
 * Set the poll flag handler.
 */
	void set_pflag_handler(poll_flag* handler);
/**
 * Flush caches.
 */
	void clear_caches() throw();
/**
 * Get sequence number (increments by 1 each time whole data is reloaded).
 */
	uint64_t get_seqno() throw() { return seqno; }
/**
 * Get pollcounter vector.
 */
	portctrl::counters& get_pollcounters() { return pollcounters; }
/**
 * Get first subframe of this frame.
 */
	uint64_t get_current_frame_first_subframe() { return current_frame_first_subframe; }
/**
 * Read specified triple at specified subframe of current frame. Only works in readonly mode.
 */
	int16_t read_subframe_at_index(uint32_t subframe, unsigned port, unsigned controller, unsigned index);
/**
 * Write specified triple at specified subframe of current frame (if possible). Only works in readonly mode.
 */
	void write_subframe_at_index(uint32_t subframe, unsigned port, unsigned controller, unsigned index,
		int16_t x);
/**
 * Call portctrl::frame_vector::compatible() with correct frame and pollcounters.
 *
 * Parameter with: The second vector.
 * Returns: True if compatible, false if not.
 */
	bool compatible(portctrl::frame_vector& with)
	{
		return movie_data->compatible(with, current_frame, pollcounters.rawdata());
	}
/**
 * Set external movie data.
 *
 * Parameter data: New movie data.
 */
	void set_movie_data(portctrl::frame_vector* data);
/**
 * Set callback to frob with data.
 *
 * Parameter func: New callback.
 */
	void set_frob_with_value(std::function<void(unsigned,unsigned,unsigned,short&)> func);
private:
	class fchange_listener : public portctrl::frame_vector::fchange_listener
	{
	public:
		fchange_listener(movie& m);
		~fchange_listener();
		void notify(portctrl::frame_vector& src, uint64_t old);
	private:
		movie& mov;
	} _listener;
	movie(const movie& mov);
	movie& operator=(const movie& m);
	//Frob with value.
	std::function<void(unsigned,unsigned,unsigned,short&)> frob_with_value;
	//Sequence number.
	uint64_t seqno;
	//The poll flag handling.
	poll_flag* pflag_handler;
	//TRUE if readonly mode is active.
	bool readonly;
	//TRUE if movie is latched to end.
	bool latch_end;
	//Movie (not global!) rerecord count.
	std::string rerecords;
	//Project ID.
	std::string _project_id;
	//The actual controller data.
	portctrl::frame_vector* movie_data;
	//Current frame + 1 (0 before next_frame() has been called.
	uint64_t current_frame;
	//First subframe in current frame (movie_data.size() if no subframes have been stored).
	uint64_t current_frame_first_subframe;
	//How many times has each control been polled (bit 31 is data ready bit)?
	portctrl::counters pollcounters;
	//Current state of buttons.
	portctrl::frame current_controls;
	//Number of known lag frames.
	uint64_t lag_frames;
	//Cached subframes.
	uint64_t cached_frame;
	uint64_t cached_subframe;
	//Count present subframes in frame starting from first_subframe (returns 0 if out of movie).
	uint32_t count_changes(uint64_t first_subframe) throw();
	//Tracker.
	memtracker::autorelease tracker;
};

#endif
