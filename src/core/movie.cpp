#include "lsnes.hpp"

#include "core/movie.hpp"

#include <stdexcept>
#include <cassert>
#include <cstring>
#include <fstream>

movie_logic::movie_logic() throw()
{
	frob_with_value = [](unsigned a, unsigned b, unsigned c, unsigned d) {};
	mf = NULL;
	mov = NULL;
	rrd = NULL;
}

void movie_logic::set_movie(movie& _mov, bool free_old) throw()
{
	auto tmp = mov;
	mov = &_mov;
	mov->set_frob_with_value(frob_with_value);
	if(free_old) delete tmp;
}

movie& movie_logic::get_movie()
{
	if(!mov)
		throw std::runtime_error("No movie");
	return *mov;
}

void movie_logic::set_mfile(moviefile& _mf, bool free_old) throw()
{
	auto tmp = mf;
	mf = &_mf;
	if(free_old) delete tmp;
}

moviefile& movie_logic::get_mfile()
{
	if(!mf)
		throw std::runtime_error("No movie");
	return *mf;
}

void movie_logic::set_rrdata(rrdata_set& _rrd, bool free_old) throw()
{
	auto tmp = rrd;
	rrd = &_rrd;
	if(free_old) delete tmp;
}

rrdata_set& movie_logic::get_rrdata()
{
	if(!rrd)
		throw std::runtime_error("No movie");
	return *rrd;
}

void movie_logic::new_frame_starting(bool dont_poll)
{
	mov->next_frame();
	portctrl::frame c = update_controls(false);
	if(!mov->readonly_mode()) {
		mov->set_controls(c);
		if(!dont_poll)
			mov->set_all_DRDY();
	} else if(!dont_poll)
		mov->set_all_DRDY();
}

short movie_logic::input_poll(unsigned port, unsigned dev, unsigned id)
{
	if(!mov)
		return 0;
	//If this is for something else than 0-0-x, drop out of poll advance if any.
	bool force = false;
	if(port || dev) force = notify_user_poll();
	if(!mov->get_DRDY(port, dev, id) || force) {
		mov->set_controls(update_controls(true, force));
		mov->set_all_DRDY();
	}
	return mov->next_input(port, dev, id);
}

void movie_logic::release_memory()
{
	delete rrd;
	rrd = NULL;
	delete mov;
	mov = NULL;
	delete mf;
	mf = NULL;
}

void movie_logic::set_frob_with_value(std::function<void(unsigned,unsigned,unsigned,short&)> func)
{
	frob_with_value = func;
	if(mov) mov->set_frob_with_value(func);
}
