#include "core/advdumper.hpp"
#include "core/audioapi.hpp"
#include "core/audioapi-driver.hpp"
#include "core/dispatch.hpp"
#include "core/framerate.hpp"
#include "core/instance.hpp"
#include "library/minmax.hpp"
#include "library/threads.hpp"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <set>

#define MUSIC_BUFFERS 8
#define MAX_VOICE_ADJUST 200

namespace
{
	std::set<audioapi_instance*> instances;
	threads::lock instances_lock;
	unsigned current_rrate = 0;
	unsigned current_prate = 0;

	void dummy_init() throw()
	{
	}

	void dummy_quit() throw()
	{
	}

	void dummy_enable(bool enable) throw()
	{
	}

	bool dummy_initialized()
	{
		return true;
	}

	void dummy_set_device(const std::string& pdev, const std::string& rdev)
	{
		if(pdev != "null")
			throw std::runtime_error("Bad sound device '" + pdev + "'");
		if(rdev != "null")
			throw std::runtime_error("Bad sound device '" + rdev + "'");
	}

	std::string dummy_get_device(bool rec)
	{
		return "null";
	}

	std::map<std::string, std::string> dummy_get_devices(bool rec)
	{
		std::map<std::string, std::string> ret;
		ret["null"] = "NULL sound output";
		return ret;
	}

	const char* dummy_name() { return "Dummy sound plugin"; }

	_audioapi_driver driver = {
		.init = dummy_init,
		.quit = dummy_quit,
		.enable = dummy_enable,
		.initialized = dummy_initialized,
		.set_device = dummy_set_device,
		.get_device = dummy_get_device,
		.get_devices = dummy_get_devices,
		.name = dummy_name
	};
}

audioapi_driver::audioapi_driver(struct _audioapi_driver _driver)
{
	driver = _driver;
}

void audioapi_driver_init() throw()
{
	driver.init();
}

void audioapi_driver_quit() throw()
{
	driver.quit();
}

void audioapi_driver_enable(bool _enable) throw()
{
	driver.enable(_enable);
}

bool audioapi_driver_initialized()
{
	return driver.initialized();
}

void audioapi_driver_set_device(const std::string& pdev, const std::string& rdev)
{
	driver.set_device(pdev, rdev);
}

std::string audioapi_driver_get_device(bool rec)
{
	return driver.get_device(rec);
}

std::map<std::string, std::string> audioapi_driver_get_devices(bool rec)
{
	return driver.get_devices(rec);
}

const char* audioapi_driver_name() throw()
{
	return driver.name();
}

void audioapi_connect_instance(audioapi_instance& instance)
{
	threads::alock h(instances_lock);
	instance.voice_rate(current_rrate, current_prate);
	instances.insert(&instance);
}

void audioapi_disconnect_instance(audioapi_instance& instance)
{
	threads::alock h(instances_lock);
	instance.voice_rate(0, 0);
	instances.erase(&instance);
}

void audioapi_send_rate_change(unsigned rrate, unsigned prate)
{
	threads::alock h(instances_lock);
	current_rrate = rrate;
	current_prate = prate;
	for(auto i: instances)
		i->voice_rate(current_rrate, current_prate);
}

void audioapi_get_mixed(int16_t* samples, size_t count, bool stereo)
{
	size_t tcount = count * (stereo ? 2 : 1);
	threads::alock h(instances_lock);
	int32_t mixbuf[tcount];
	memset(mixbuf, 0, sizeof(mixbuf[0]) * tcount);
	//Collect all samples.
	for(auto i: instances) {
		int16_t tmp[tcount];
		i->get_mixed(tmp, count, stereo);
		for(size_t i = 0; i < tcount; i++)
			mixbuf[i] += (int32_t)tmp[i];
	}
	//Downcast result with saturation.
	for(size_t i = 0; i < tcount; i++)
		samples[i] = (int16_t)clip(mixbuf[i], -32768, 32767);
}

void audioapi_put_voice(float* samples, size_t count)
{
	threads::alock h(instances_lock);
	//Broadcast to all instances.
	for(auto i: instances)
		i->put_voice(samples, count);
}
