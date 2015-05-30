#ifndef _library__memorywatch_list__hpp__included__
#define _library__memorywatch_list__hpp__included__

#include "memorywatch.hpp"
#include "mathexpr.hpp"
#include <string>
#include <functional>

namespace memorywatch
{
struct output_list : public item_printer
{
	output_list();
	~output_list();
	void set_output(std::function<void(const text& n, const text& v)> _fn);
	void show(const text& iname, const text& val);
	void reset();
	bool cond_enable;
	GC::pointer<mathexpr::mathexpr> enabled;
	//State variables.
	std::function<void(const text& n, const text& v)> fn;
};
}

#endif
