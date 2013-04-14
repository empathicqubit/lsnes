#include "core/window.hpp"

#include <wx/thread.h>

struct wxw_mutex : public mutex
{
	wxw_mutex() throw(std::bad_alloc);
	~wxw_mutex() throw();
	void lock() throw();
	void unlock() throw();
	wxMutex* m;
};

struct wxw_rec_mutex : public mutex
{
	wxw_rec_mutex() throw(std::bad_alloc);
	~wxw_rec_mutex() throw();
	void lock() throw();
	void unlock() throw();
	wxMutex* m;
	volatile bool locked;
	uint32_t owner;
	uint32_t count;
};

wxw_mutex::wxw_mutex() throw(std::bad_alloc)
{
	m = new wxMutex();
}

wxw_mutex::~wxw_mutex() throw()
{
	delete m;
}

void wxw_mutex::lock() throw()
{
	m->Lock();
}

void wxw_mutex::unlock() throw()
{
	m->Unlock();
}

wxw_rec_mutex::wxw_rec_mutex() throw(std::bad_alloc)
{
	m = new wxMutex();
	locked = false;
	owner = 0;
	count = 0;
}

wxw_rec_mutex::~wxw_rec_mutex() throw()
{
	delete m;
}

void wxw_rec_mutex::lock() throw()
{
	uint32_t our_id = wxThread::GetCurrentId();
	if(locked && owner == our_id) {
		//Owned by us, increment lock count.
		++count;
		return;
	}
	m->Lock();
	locked = true;
	owner = our_id;
	count = 1;
}

void wxw_rec_mutex::unlock() throw()
{
	uint32_t our_id = wxThread::GetCurrentId();
	if(!locked || owner != our_id)
		std::cerr << "Warning: Trying to unlock recursive lock locked by another thread!" << std::endl;
	if(!--count) {
		locked = false;
		owner = 0;
		m->Unlock();
	}
}

mutex& mutex::aquire() throw(std::bad_alloc)
{
	return *new wxw_mutex;
}

mutex& mutex::aquire_rec() throw(std::bad_alloc)
{
	return *new wxw_rec_mutex;
}

struct wxw_condition : public condition
{
	wxw_condition(mutex& m) throw(std::bad_alloc);
	~wxw_condition() throw();
	bool wait(uint64_t x) throw();
	void signal() throw();
	wxCondition* c;
};

wxw_condition::wxw_condition(mutex& m) throw(std::bad_alloc)
	: condition(m)
{
	wxw_mutex* m2 = dynamic_cast<wxw_mutex*>(&m);
	c = new wxCondition(*m2->m);
}

wxw_condition::~wxw_condition() throw()
{
	delete c;
}

bool wxw_condition::wait(uint64_t x) throw()
{
	wxCondError e = c->WaitTimeout((x + 999) / 1000);
	return (e == wxCOND_NO_ERROR);
}

void wxw_condition::signal() throw()
{
	c->Broadcast();
}

condition& condition::aquire(mutex& m) throw(std::bad_alloc)
{
	return *new wxw_condition(m);
}

struct wxw_thread_id : public thread_id
{
	wxw_thread_id() throw();
	~wxw_thread_id() throw();
	bool is_me() throw();
	uint32_t id;
};

wxw_thread_id::wxw_thread_id() throw()
{
	id = wxThread::GetCurrentId();
}

wxw_thread_id::~wxw_thread_id() throw()
{
}

bool wxw_thread_id::is_me() throw()
{
	return (id == wxThread::GetCurrentId());
}

thread_id& thread_id::me() throw(std::bad_alloc)
{
	return *new wxw_thread_id;
}

struct wxw_thread;

struct wxw_thread_inner : public wxThread
{
	wxw_thread_inner(wxw_thread* _up);
	void* (*entry)(void* arg);
	void* entry_arg;
	wxThread::ExitCode Entry();
	wxw_thread* up;
};

struct wxw_thread : public thread
{
	wxw_thread(void* (*fn)(void* arg), void* arg) throw(std::bad_alloc, std::runtime_error);
	~wxw_thread() throw();
	void _join() throw();
	bool has_waited;
	wxw_thread_inner* inner;
	void notify_quit2(void* ret);
};

wxw_thread_inner::wxw_thread_inner(wxw_thread* _up)
	: wxThread(wxTHREAD_JOINABLE)
{
	up = _up;
}

void wxw_thread::notify_quit2(void* ret)
{
	notify_quit(ret);
}

wxThread::ExitCode wxw_thread_inner::Entry()
{
	up->notify_quit2(entry(entry_arg));
	return 0;
}

wxw_thread::wxw_thread(void* (*fn)(void* arg), void* arg) throw(std::bad_alloc, std::runtime_error)
{
	has_waited = false;
	inner = new wxw_thread_inner(this);
	inner->entry = fn;
	inner->entry_arg = arg;
	wxThreadError e = inner->Create(8 * 1024 * 1024);
	if(e != wxTHREAD_NO_ERROR)
		throw std::bad_alloc();
	e = inner->Run();
	if(e != wxTHREAD_NO_ERROR)
		throw std::bad_alloc();
}

wxw_thread::~wxw_thread() throw()
{
	if(inner) {
		inner->Wait();
		inner = NULL;
	}
}

void wxw_thread::_join() throw()
{
	if(inner) {
		inner->Wait();
		inner = NULL;
	}
}

thread& thread::create(void* (*entrypoint)(void* arg), void* arg) throw(std::bad_alloc, std::runtime_error)
{
	return *new wxw_thread(entrypoint, arg);
}