#include "core/filedownload.hpp"
#include "core/misc.hpp"
#include "library/string.hpp"
#include "library/zip.hpp"
#include "core/moviedata.hpp"
#include "core/rom.hpp"
#include "interface/romtype.hpp"
#include <fstream>

namespace
{
	void file_download_thread_trampoline(file_download* d)
	{
		d->_do_async();
	}

	class file_download_handler : public http_request::output_handler
	{
	public:
		file_download_handler(const std::string& filename)
		{
			fp.open(filename, std::ios::binary);
			tsize = 0;
		}
		~file_download_handler()
		{
		}
		void header(const std::string& name, const std::string& content)
		{
			//Ignore headers.
		}
		void write(const char* source, size_t srcsize)
		{
			fp.write(source, srcsize);
			tsize += srcsize;
		}
	private:
		std::ofstream fp;
		size_t tsize;
	};
}

file_download::file_download()
{
	finished = false;
	req.ohandler = NULL;
}

file_download::~file_download()
{
	if(req.ohandler) delete req.ohandler;
}

void file_download::cancel()
{
	req.cancel();
	errormsg = "Canceled";
	finished = true;
}

void file_download::do_async()
{
	tempname = get_temp_file();
	req.ihandler = NULL;
	req.ohandler = new file_download_handler(tempname);
	req.verb = "GET";
	req.url = url;
	try {
		req.lauch_async();
		(new thread_class(file_download_thread_trampoline, this))->detach();
	} catch(std::exception& e) {
		req.cancel();
		umutex_class h(m);
		errormsg = e.what();
		finished = true;
		cond.notify_all();
	}
}

std::string file_download::statusmsg()
{
	int64_t dn, dt, un, ut;
	if(finished)
		return (stringfmt() << "Downloading finished").str();
	req.get_xfer_status(dn, dt, un, ut);
	if(dn == 0)
		return "Connecting...";
	else if(dt == 0)
		return (stringfmt() << "Downloading (" << dn << "/<unknown>)").str();
	else if(dn < dt)
		return (stringfmt() << "Downloading (" << (100 * dn / dt) << "%)").str();
	else
		return (stringfmt() << "Downloading finished").str();
}

void file_download::_do_async()
{
	while(!req.finished) {
		umutex_class h(req.m);
		req.finished_cond.wait(h);
		if(!req.finished)
			continue;
		if(req.errormsg != "") {
			remove(tempname.c_str());
			umutex_class h(m);
			errormsg = req.errormsg;
			finished = true;
			cond.notify_all();
			return;
		}
	}
	delete req.ohandler;
	req.ohandler = NULL;
	if(req.http_code > 299) {
		umutex_class h(m);
		errormsg = (stringfmt() << "Got HTTP error " << req.http_code).str();
		finished = true;
		cond.notify_all();
	}
	//Okay, we got the file.
	std::istream* s = NULL;
	try {
		zip_reader r(tempname);
		unsigned count = 0;
		for(auto i : r) {
			count++;
		}
		if(count == 1) {
			std::istream& s = r[*r.begin()];
			std::ofstream out(tempname2 = get_temp_file(), std::ios::binary);
			while(s) {
				char buf[4096];
				s.read(buf, sizeof(buf));
				out.write(buf, s.gcount());
			}
			delete &s;
		} else {
			tempname2 = tempname;
		}
	} catch(...) {
		if(s) delete s;
		tempname2 = tempname;
	}
	if(tempname != tempname2) remove(tempname.c_str());
	try {
		core_type* gametype = NULL;
		if(!our_rom.rtype->isnull())
			gametype = our_rom.rtype;
		else {
			moviefile::brief_info info(tempname2);
			auto sysregs = core_sysregion::find_matching(info.sysregion);
			for(auto i : sysregs)
				gametype = &i->get_type();
		}
		moviefile::memref(target_slot) = moviefile(tempname2, *gametype);
		remove(tempname2.c_str());
	} catch(std::exception& e) {
		remove(tempname2.c_str());
		umutex_class h(m);
		errormsg = e.what();
		finished = true;
		cond.notify_all();
		return;
	}
	//We are done!
	umutex_class h(m);
	finished = true;
	cond.notify_all();
}

