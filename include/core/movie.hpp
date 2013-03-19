#ifndef _movie__hpp__included__
#define _movie__hpp__included__

#include <string>
#include <cstdint>
#include <stdexcept>
#include "controllerframe.hpp"

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
	movie() throw(std::bad_alloc);

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
	void readonly_mode(bool enable) throw(std::bad_alloc);

/**
 * Returns the movie rerecord count (this is not the same thing as global rerecord count).
 *
 * returns: The movie rerecord count
 * throws std::bad_alloc: Not enough memory.
 */
	std::string rerecord_count() throw(std::bad_alloc);

/**
 * Sets the movie rerecord count (this is not the same thing as global rerecord count).
 *
 * parameter count: The new rerecord count
 * throws std::bad_alloc: Not enough memory.
 */
	void rerecord_count(const std::string& count) throw(std::bad_alloc);

/**
 * Read project ID
 *
 * returns: The project ID
 * throws std::bad_alloc: Not enough memory.
 */
	std::string project_id() throw(std::bad_alloc);

/**
 * brief Set project ID
 *
 * parameter id: New project ID.
 * throws std::bad_alloc: Not enough memory.
 */
	void project_id(const std::string& id) throw(std::bad_alloc);

/**
 * Get number of frames in movie
 *
 * returns: The number of frames.
 */
	uint64_t get_frame_count() throw();

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
	void next_frame() throw(std::bad_alloc);

/**
 * Reads the data ready flag. On new frame, all data ready flags are unset. On reading control, its data ready
 * flag is unset.
 *
 * parameter pid: Physical controller id.
 * parameter index: Control ID.
 * returns: The read value.
 * throws std::logic_error: Invalid control index.
 */
	bool get_DRDY(unsigned pid, unsigned index) throw(std::logic_error);

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
	short next_input(unsigned pid, unsigned index) throw(std::bad_alloc, std::logic_error);

/**
 * Set current control values. These are read in readwrite mode.
 *
 * parameter controls: The new controls.
 */
	void set_controls(controller_frame controls) throw();

/**
 * Get current control values in effect.
 *
 * returns: Controls
 */
	controller_frame get_controls() throw();

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
	void load(const std::string& rerecs, const std::string& project_id, controller_frame_vector& input)
		throw(std::bad_alloc, std::runtime_error);

/**
 * Saves the movie data.
 *
 * returns: The movie data.
 * throws std::bad_alloc: Not enough memory.
 */
	controller_frame_vector save() throw(std::bad_alloc);

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
		std::vector<uint32_t>& pcounters) throw(std::bad_alloc);

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
		controller_frame_vector* old_movie, const std::string& old_projectid) throw(std::bad_alloc,
		std::runtime_error);
/**
 * Reset the state of movie to initial state.
 */
	void reset_state() throw();
/**
 * Get reset status for current frame.
 *
 * returns: -1 if current frame doesn't have a reset. Otherwise number of cycles to wait for delayed reset (0 is
 *	immediate reset).
 */
	long get_reset_status() throw();

/**
 * Commit a reset (writes a reset into current frame in readwrite mode).
 *
 * parameter delay: The number of cycles to delay the reset.
 */
	void commit_reset(long delay) throw(std::bad_alloc);

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
	controller_frame read_subframe(uint64_t frame, uint64_t subframe) throw();
/**
 * Fast save.
 */
	void fast_save(uint64_t& _frame, uint64_t& _ptr, uint64_t& _lagc, std::vector<uint32_t>& counters);
/**
 * Fast load.
 */
	void fast_load(uint64_t& _frame, uint64_t& _ptr, uint64_t& _lagc, std::vector<uint32_t>& counters);
/**
 * Get the internal controller frame vector.
 */
	controller_frame_vector& get_frame_vector() throw() { return movie_data; }
/**
 * Flush caches.
 */
	void clear_caches() throw();
/**
 * Get sequence number (increments by 1 each time whole data is reloaded).
 */
	uint64_t get_seqno() throw() { return seqno; }
/**
 * Assignment.
 */
	movie& operator=(const movie& m);
/**
 * Get pollcounter vector.
 */
	pollcounter_vector& get_pollcounters() { return pollcounters; }
/**
 * Get first subframe of this frame.
 */
	uint64_t get_current_frame_first_subframe() { return current_frame_first_subframe; }
/**
 * Recount frames.
 */
	void recount_frames() { frames_in_movie = movie_data.count_frames(); }
/**
 * Adjust frame count.
 */
	void adjust_frame_count(int64_t adjust) { frames_in_movie += adjust; }
/**
 * Set reload mode flag (only effective in readonly mode).
 */
	void set_reload_mode(bool newstate) throw() { reload_mode = newstate; }
/**
 * Get reload mode flag.
 */
	bool get_reload_mode() const throw() { return reload_mode; }
/**
 * Read the current subframe.
 */
	controller_frame current_subframe() throw(std::bad_alloc);
private:
	//TRUE if readonly mode is active.
	bool readonly;
	//Movie (not global!) rerecord count.
	std::string rerecords;
	//Project ID.
	std::string _project_id;
	//The actual controller data.
	controller_frame_vector movie_data;
	//Current frame + 1 (0 before next_frame() has been called.
	uint64_t current_frame;
	//First subframe in current frame (movie_data.size() if no subframes have been stored).
	uint64_t current_frame_first_subframe;
	//How many times has each control been polled (bit 31 is data ready bit)?
	pollcounter_vector pollcounters;
	//Current state of buttons.
	controller_frame current_controls;
	//Number of known lag frames.
	uint64_t lag_frames;
	//Number of frames in movie.
	uint64_t frames_in_movie;
	//Cached subframes.
	uint64_t cached_frame;
	uint64_t cached_subframe;
	//Count present subframes in frame starting from first_subframe (returns 0 if out of movie).
	uint32_t count_changes(uint64_t first_subframe) throw();
	//Sequence number.
	uint64_t seqno;
	//Reload mode flag.
	bool reload_mode;
};

/**
 * Class encapsulating bridge logic between bsnes interface and movie code.
 */
class movie_logic
{
public:
/**
 * Create new bridge.
 */
	movie_logic() throw();

/**
 * Get the movie instance associated.
 *
 * returns: The movie instance.
 */
	movie& get_movie() throw();

/**
 * Notify about new frame starting.
 *
 * returns: Reset status for the new frame.
 */
	long new_frame_starting(bool dont_poll) throw(std::bad_alloc, std::runtime_error);

/**
 * Poll for input.
 *
 * parameter port: The port number.
 * parameter dev: The controller index.
 * parameter id: Control id.
 * returns: Value for polled input.
 * throws std::bad_alloc: Not enough memory.
 * throws std::runtime_error: Error polling for input.
 */
	short input_poll(bool port, unsigned dev, unsigned id) throw(std::bad_alloc, std::runtime_error);

/**
 * Called when movie code needs new controls snapshot.
 *
 * parameter subframe: True if this is for subframe update, false if for frame update.
 */
	controller_frame update_controls(bool subframe) throw(std::bad_alloc, std::runtime_error);
private:
	movie mov;
};

/**
 * Extended mode flag.
 */
extern unsigned extended_mode;

#endif
