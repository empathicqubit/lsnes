#ifndef _movie__hpp__included__
#define _movie__hpp__included__

#include <string>
#include <cstdint>
#include <stdexcept>
#include "core/controllerframe.hpp"
#include "core/moviefile.hpp"
#include "library/rrdata.hpp"
#include "library/movie.hpp"

/**
 * Class encapsulating bridge logic between core interface and movie code.
 */
class movie_logic
{
public:
/**
 * Create new bridge.
 */
	movie_logic() throw();
/**
 * Has movie?
 */
	operator bool() throw() { return mov; }
	bool operator!() throw() { return !mov; }
/**
 * Get the movie instance associated.
 *
 * returns: The movie instance.
 */
	movie& get_movie();

/**
 * Set the movie instance associated.
 */
	void set_movie(movie& _mov, bool free_old = false) throw();

/**
 * Get the current movie file.
 */
	moviefile& get_mfile();

/**
 * Set the current movie file.
 */
	void set_mfile(moviefile& _mf, bool free_old = false) throw();
/**
 * Get current rrdata.
 */
	rrdata_set& get_rrdata();

/**
 * Set current rrdata.
 */
	void set_rrdata(rrdata_set& _rrd, bool free_old = false) throw();

/**
 * Notify about new frame starting.
 */
	void new_frame_starting(bool dont_poll);

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
	short input_poll(unsigned port, unsigned dev, unsigned id);

/**
 * Called when movie code needs new controls snapshot.
 *
 * parameter subframe: True if this is for subframe update, false if for frame update.
 */
	portctrl::frame update_controls(bool subframe, bool forced = false);

/**
 * Notify user poll (exit poll advance).
 *
 * returns: If true, update_controls is forced.
 */
	bool notify_user_poll();
/**
 * Release memory for mov, mf and rrd.
 */
	void release_memory();
/**
 * Set frob with data routine.
 */
	 void set_frob_with_value(std::function<void(unsigned,unsigned,unsigned,short&)> func);
private:
	std::function<void(unsigned,unsigned,unsigned,short&)> frob_with_value;
	movie_logic(const movie_logic&);
	movie_logic& operator=(const movie_logic&);
	movie* mov;
	moviefile* mf;
	rrdata_set* rrd;
};

#endif
