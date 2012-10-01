#include <cstdint>
#ifdef WITH_OPUS_CODEC
#include "library/filesys.hpp"
#include "library/minmax.hpp"
#include "library/workthread.hpp"
#include "library/serialization.hpp"
#include "library/string.hpp"
#include "core/audioapi.hpp"
#include "core/command.hpp"
#include "core/framerate.hpp"
#include "core/inthread.hpp"
#include "core/keymapper.hpp"
#include "core/misc.hpp"
#include <cmath>
#include <list>
#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/time.h>
//Fsck it.
#define OPUS_BUILD
#include "opus/opus.h"
#include "opus/opus_defines.h"

//Farther than this, packets can be fastskipped.
#define OPUS_CONVERGE_MAX 5760
//Maximum size of PCM output for one packet.
#define OPUS_MAX_OUT 5760
//Output block size.
#define OUTPUT_BLOCK 1440
//Main sampling rate.
#define OPUS_SAMPLERATE 48000
//Opus block size
#define OPUS_BLOCK_SIZE 960
//Threshold for decoding additional block
#define BLOCK_THRESHOLD 1200
//Maximum output block size.
#define OUTPUT_SIZE (BLOCK_THRESHOLD + OUTPUT_BLOCK)
//Amount of microseconds per interation.
#define ITERATION_TIME 15000
//Opus bitrate to use.
#define OPUS_BITRATE 48000
//Record buffer size threshold divider.
#define REC_THRESHOLD_DIV 40
//Playback buffer size threshold divider.
#define PLAY_THRESHOLD_DIV 30

namespace
{
	class opus_playback_stream;
	class opus_stream;
	class stream_collection;

	//Recording active flag.
	volatile bool active_flag = false;
	//Last seen frame number.
	uint64_t last_frame_number = 0;
	//Last seen rate.
	double last_rate = 0;
	//Mutex protecting current_time and time_jump.
	mutex_class time_mutex;
	//The current time.
	uint64_t current_time;
	//Time jump flag. Set if time jump is detected.
	//If time jump is detected, all current playing streams are stopped, stream locks are cleared and
	//apropriate streams are restarted. If time jump is false, all unlocked streams coming into range
	//are started.
	bool time_jump;
	//Lock protecting active_playback_streams.
	mutex_class active_playback_streams_lock; 
	//List of streams currently playing.
	std::list<opus_playback_stream*> active_playback_streams;
	//The collection of streams.
	stream_collection* current_collection;
	//Lock protecting current collection.
	mutex_class current_collection_lock;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//Information about individual opus packet in stream.
	struct opus_packetinfo
	{
		//Length is in units of 1/400th of a second.
		opus_packetinfo(uint16_t datasize, uint8_t length, uint64_t offset)
		{
			descriptor = (offset & 0xFFFFFFFFFFULL) | (static_cast<uint64_t>(length) << 40) |
				(static_cast<uint64_t>(datasize) << 48);
		}
		//Get the data size of the packet.
		uint16_t size() { return descriptor >> 48; }
		//Calculate the length of packet in samples.
		uint16_t length() { return 120 * ((descriptor >> 40) & 0xFF); }
		//Calculate the true offset.
		uint64_t offset() { return descriptor & 0xFFFFFFFFFFULL; }
		//Read the packet.
		//Can throw.
		std::vector<unsigned char> packet(filesystem_ref from_sys);
	private:
		uint64_t descriptor;
	};

	std::vector<unsigned char> opus_packetinfo::packet(filesystem_ref from_sys)
	{
		std::vector<unsigned char> ret;
		uint64_t off = offset();
		uint32_t sz = size();
		uint32_t cluster = off / CLUSTER_SIZE;
		uint32_t coff = off % CLUSTER_SIZE;
		ret.resize(sz);
		size_t r = from_sys.read_data(cluster, coff, &ret[0], sz);
		if(r != sz)
			throw std::runtime_error("Incomplete read");
		return ret;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//Information about opus stream.
	struct opus_stream
	{
		//Create new empty stream with specified base time.
		opus_stream(uint64_t base, filesystem_ref filesys);
		//Read stream with specified base time and specified start clusters.
		//Can throw.
		opus_stream(uint64_t base, filesystem_ref filesys, uint32_t ctrl_cluster, uint32_t data_cluster);
		//Import a stream with specified base time.
		//Can throw.
		opus_stream(uint64_t base, filesystem_ref filesys, std::ifstream& data, bool compressed);
		//Delete this stream (also puts a ref)
		void delete_stream() { deleting = true; put_ref(); }
		//Export a stream.
		//Can throw.
		void export_stream(std::ofstream& data, bool compressed);
		//Get length of specified packet in samples.
		uint16_t packet_length(uint32_t seqno)
		{
			return (seqno < packets.size()) ? packets[seqno].length() : 0;
		}
		//Get data of specified packet.
		//Can throw.
		std::vector<unsigned char> packet(uint32_t seqno)
		{
			return (seqno < packets.size()) ? packets[seqno].packet(fs) : std::vector<unsigned char>();
		}
		//Get base time in samples for stream.
		uint64_t timebase() { return s_timebase; }
		//Set base time in samples for stream.
		void timebase(uint64_t ts) { s_timebase = ts; }
		//Get length of stream in samples.
		uint64_t length() { return total_len; }
		//Get number of packets in stream.
		uint32_t blocks() { return packets.size(); }
		//Is this stream locked?
		bool islocked() { return locked; }
		//Lock a stream.
		void lock() { locked = true; }
		//Unlock a stream.
		void unlock() { locked = false; }
		//Increment reference count.
		void get_ref() { umutex_class m(reflock); refcount++; }
		//Decrement reference count, destroying object if it hits zero.
		void put_ref() { umutex_class m(reflock); refcount--; if(!refcount) destroy(); }
		//Add new packet into stream.
		//Not safe to call simultaneously with packet_length() or packet().
		//Can throw.
		void write(uint8_t len, const unsigned char* payload, size_t payload_len);
		//Get clusters.
		std::pair<uint32_t, uint32_t> get_clusters() { return std::make_pair(ctrl_cluster, data_cluster); }
	private:
		opus_stream(const opus_stream&);
		opus_stream& operator=(const opus_stream&);
		void destroy();
		filesystem_ref fs;
		std::vector<opus_packetinfo> packets;
		uint64_t total_len;
		uint64_t s_timebase;
		uint32_t next_cluster;
		uint32_t next_offset;
		uint32_t next_mcluster;
		uint32_t next_moffset;
		uint32_t ctrl_cluster;
		uint32_t data_cluster;
		bool locked;
		mutex_class reflock;
		unsigned refcount;
		bool deleting;
	};

	opus_stream::opus_stream(uint64_t base, filesystem_ref filesys)
		: fs(filesys)
	{
		refcount = 1;
		deleting = false;
		total_len = 0;
		s_timebase = base;
		locked = false;
		next_cluster = 0;
		next_mcluster = 0;
		next_offset = 0;
		next_moffset = 0;
		ctrl_cluster = 0;
		data_cluster = 0;
	}

	opus_stream::opus_stream(uint64_t base, filesystem_ref filesys, uint32_t _ctrl_cluster,
		uint32_t _data_cluster)
		: fs(filesys)
	{
		refcount = 1;
		deleting = false;
		total_len = 0;
		s_timebase = base;
		locked = false;
		next_cluster = data_cluster = _data_cluster;
		next_mcluster = ctrl_cluster = _ctrl_cluster;
		next_offset = 0;
		next_moffset = 0;
		//Read the data buffers.
		char buf[CLUSTER_SIZE];
		uint32_t last_cluster_seen = next_mcluster;
		uint64_t total_size = 0;
		uint64_t total_frames = 0;
		while(true) {
			last_cluster_seen = next_mcluster;
			size_t r = fs.read_data(next_mcluster, next_moffset, buf, CLUSTER_SIZE);
			if(!r) {
				//The stream ends here.
				break;
			}
			//Find the first unused entry if any.
			for(unsigned i = 0; i < CLUSTER_SIZE; i += 4)
				if(!buf[i + 3]) {
					//This entry is unused, end of stream.
					next_moffset = i;
					goto out_parsing;
				} else {
					uint16_t psize = read16ube(buf + i);
					uint8_t plen = read8ube(buf + i + 2);
					total_size += psize;
					total_len += 120 * plen;
					opus_packetinfo p(psize, plen, 1ULL * next_cluster * CLUSTER_SIZE +
						next_offset);
					size_t r2 = fs.skip_data(next_cluster, next_offset, psize);
					if(r2 < psize)
						throw std::runtime_error("Incomplete data stream");
					packets.push_back(p);
					total_frames++;
				}
		}
out_parsing:
		;
	}

	opus_stream::opus_stream(uint64_t base, filesystem_ref filesys, std::ifstream& data, bool compressed)
		: fs(filesys)
	{
		refcount = 1;
		deleting = false;
		total_len = 0;
		s_timebase = base;
		locked = false;
		next_cluster = 0;
		next_mcluster = 0;
		next_offset = 0;
		next_moffset = 0;
		ctrl_cluster = 0;
		data_cluster = 0;
		int err;
		unsigned char tmpi[65536];
		float tmp[OPUS_MAX_OUT];
		if(compressed) {
			OpusDecoder* dec = opus_decoder_create(48000, 1, &err);
			while(data) {
				char head[8];
				data.read(head, 8);
				if(!data)
					continue;
				uint32_t psize = read32ube(head);
				uint32_t pstate = read32ube(head + 4);
				if(psize > sizeof(tmpi)) {
					if(ctrl_cluster) fs.free_cluster_chain(ctrl_cluster);
					if(data_cluster) fs.free_cluster_chain(data_cluster);
					opus_decoder_destroy(dec);
					throw std::runtime_error("Packet too large to decode");
				}
				data.read(reinterpret_cast<char*>(tmpi), psize);
				if(!data) {
					if(ctrl_cluster) fs.free_cluster_chain(ctrl_cluster);
					if(data_cluster) fs.free_cluster_chain(data_cluster);
					opus_decoder_destroy(dec);
					throw std::runtime_error("Error reading opus packet");
				}
				int r = opus_decode_float(dec, tmpi, psize, tmp,
					OPUS_MAX_OUT, 0);
				if(r < 0) {
					if(ctrl_cluster) fs.free_cluster_chain(ctrl_cluster);
					if(data_cluster) fs.free_cluster_chain(data_cluster);
					opus_decoder_destroy(dec);
					(stringfmt() << "Error decoding opus packet: " << opus_strerror(r)).throwex();
				}
				uint32_t cstate;
				opus_decoder_ctl(dec, OPUS_GET_FINAL_RANGE(&cstate));
				if(cstate != pstate) {
					if(ctrl_cluster) fs.free_cluster_chain(ctrl_cluster);
					if(data_cluster) fs.free_cluster_chain(data_cluster);
					opus_decoder_destroy(dec);
					throw std::runtime_error("Opus packet checksum mismatch");
				}
				r = opus_decoder_get_nb_samples(dec, tmpi, psize);
				if(r < 0 || r % 120) {
					if(ctrl_cluster) fs.free_cluster_chain(ctrl_cluster);
					if(data_cluster) fs.free_cluster_chain(data_cluster);
					opus_decoder_destroy(dec);
					throw std::runtime_error("Error getting length of opus packet");
				}
				uint8_t plen = r / 120;
				try {
					write(plen, tmpi, psize);
				} catch(...) {
					if(ctrl_cluster) fs.free_cluster_chain(ctrl_cluster);
					if(data_cluster) fs.free_cluster_chain(data_cluster);
					opus_decoder_destroy(dec);
					throw;
				}
			}
			opus_decoder_destroy(dec);
		} else {
			char header[260];
			data.read(header, 32);
			if(!data)
				throw std::runtime_error("Can't read .sox header");
			if(read32ule(header + 0) != 0x586F532EULL)
				throw std::runtime_error("Bad .sox header magic");
			if(read8ube(header + 4) > 28)
				data.read(header + 32, read8ube(header + 4) - 28);
			if(!data)
				throw std::runtime_error("Can't read .sox header");
			if(read64ule(header + 16) != 4676829883349860352ULL)
				throw std::runtime_error("Bad .sox sampling rate");
			if(read32ule(header + 24) != 1)
				throw std::runtime_error("Only mono streams are supported");
			uint64_t samples = read64ule(header + 8);
			OpusEncoder* enc = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &err);
			opus_encoder_ctl(enc, OPUS_SET_BITRATE(OPUS_BITRATE));
			for(uint64_t i = 0; i < samples; i += OPUS_BLOCK_SIZE) {
				size_t bs = OPUS_BLOCK_SIZE;
				if(i + bs > samples)
					bs = samples - i;
				data.read(reinterpret_cast<char*>(tmpi), 4 * bs);
				if(!data) {
					if(ctrl_cluster) fs.free_cluster_chain(ctrl_cluster);
					if(data_cluster) fs.free_cluster_chain(data_cluster);
					opus_encoder_destroy(enc);
					throw std::runtime_error("Can't read .sox data");
				}
				for(size_t j = 0; j < bs; j++)
					tmp[j] = static_cast<float>(read32sle(tmpi + 4 * j)) / 268435456;
				for(size_t j = bs; j < OPUS_BLOCK_SIZE; j++)
					tmp[j] = 0;
				int r = opus_encode_float(enc, tmp, OPUS_BLOCK_SIZE, tmpi, sizeof(tmpi));
				if(r < 0) {
					if(ctrl_cluster) fs.free_cluster_chain(ctrl_cluster);
					if(data_cluster) fs.free_cluster_chain(data_cluster);
					opus_encoder_destroy(enc);
					(stringfmt() << "Error encoding opus packet: " << opus_strerror(r)).throwex();
				}
				try {
					write(OPUS_BLOCK_SIZE / 120, tmpi, r);
				} catch(...) {
					if(ctrl_cluster) fs.free_cluster_chain(ctrl_cluster);
					if(data_cluster) fs.free_cluster_chain(data_cluster);
					opus_encoder_destroy(enc);
					throw;
				}
			}
			opus_encoder_destroy(enc);
		}
	}

	void opus_stream::destroy()
	{
		if(deleting) {
			//We catch the errors and print em, because otherwise put_ref could throw, which would
			//be too much.
			try {
				fs.free_cluster_chain(ctrl_cluster);
			} catch(std::exception& e) {
				messages << "Failed to delete stream control file: " << e.what();
			}
			try {
				fs.free_cluster_chain(data_cluster);
			} catch(std::exception& e) {
				messages << "Failed to delete stream data file: " << e.what();
			}
		}
		delete this;
	}

	void opus_stream::export_stream(std::ofstream& data, bool compressed)
	{
		int err;
		OpusDecoder* dec = opus_decoder_create(48000, 1, &err);
		std::vector<unsigned char> p;
		float tmp[OPUS_MAX_OUT];
		if(compressed) {
			for(size_t i = 0; i < packets.size(); i++) {
				char head[8];
				unsigned state;
				try {
					p = packet(i);
				} catch(std::exception& e) {
					opus_decoder_destroy(dec);
					(stringfmt() << "Error reading opus packet: " << e.what()).throwex();
				}
				int r = opus_decode_float(dec, &p[0], p.size(), tmp, OPUS_MAX_OUT, 0);
				if(r < 0) {
					opus_decoder_destroy(dec);
					(stringfmt() << "Error decoding opus packet: " << opus_strerror(r)).throwex();
				}
				opus_decoder_ctl(dec, OPUS_GET_FINAL_RANGE(&state));
				write32ube(head + 0, p.size());
				write32ube(head + 4, state);
				data.write(head, 8);
				data.write(reinterpret_cast<char*>(&p[0]), p.size());
				if(!data) {
					opus_decoder_destroy(dec);
					throw std::runtime_error("Error writing opus packet");
				}
			}
		} else {
			char header[32];
			write64ule(header, 0x1C586F532EULL);			//Magic and header size.
			write64ule(header + 16, 4676829883349860352ULL);	//Sampling rate.
			write32ule(header + 24, 1);
			uint64_t tlen = 0;
			data.write(header, 32);
			if(!data) {
				opus_decoder_destroy(dec);
				throw std::runtime_error("Error writing PCM data.");
			}
			for(size_t i = 0; i < packets.size(); i++) {
				char blank[4] = {0, 0, 0, 0};
				std::vector<unsigned char> p;
				try {
					p = packet(i);
				} catch(std::exception& e) {
					opus_decoder_destroy(dec);
					(stringfmt() << "Error reading opus packet: " << e.what()).throwex();
				}
				uint32_t len = packet_length(i);
				int r = opus_decode_float(dec, &p[0], p.size(), tmp, OPUS_MAX_OUT, 0);
				tlen += len;
				if(r < 0) {
					opus_decoder_destroy(dec);
					(stringfmt() << "Error decoding opus packet: " << opus_strerror(r)).throwex();
				} else {
					for(uint32_t j = 0; j < len; j++) {
						int32_t s = (int32_t)(tmp[j] * 268435456.0);
						write32sle(blank, s);
						data.write(blank, 4);
						if(!data)
							throw std::runtime_error("Error writing PCM data.");
					}
				}
			}
			data.seekp(0, std::ios_base::beg);
			write64ule(header + 8, tlen);
			data.write(header, 32);
			if(!data) {
				opus_decoder_destroy(dec);
				throw std::runtime_error("Error writing PCM data.");
			}
		}
		opus_decoder_destroy(dec);
	}

	void opus_stream::write(uint8_t len, const unsigned char* payload, size_t payload_len)
	{
		try {
			char descriptor[4];
			uint32_t used_cluster, used_offset;
			uint32_t used_mcluster, used_moffset;
			if(!next_cluster)
				next_cluster = data_cluster = fs.allocate_cluster();
			if(!next_mcluster)
				next_mcluster = ctrl_cluster = fs.allocate_cluster();
			write16ube(descriptor, payload_len);
			write8ube(descriptor + 2, len);
			write8ube(descriptor + 3, 1);
			fs.write_data(next_cluster, next_offset, payload, payload_len, used_cluster, used_offset);
			fs.write_data(next_mcluster, next_moffset, descriptor, 4, used_mcluster, used_moffset);
			uint64_t off = static_cast<uint64_t>(used_cluster) * CLUSTER_SIZE + used_offset;
			opus_packetinfo p(payload_len, len, off);
			total_len += p.length();
			packets.push_back(p);
		} catch(std::exception& e) {
			(stringfmt() << "Can't write opus packet: " << e.what()).throwex();
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//Playing opus stream.
	struct opus_playback_stream
	{
		//Create a new playing stream from given opus stream.
		opus_playback_stream(opus_stream& data);
		//Destroy playing opus stream.
		~opus_playback_stream();
		//Read samples from stream.
		//Can throw.
		void read(float* data, size_t samples);
		//Skip samples from stream.
		//Can throw.
		void skip(uint64_t samples);
		//Has the stream already ended?
		bool eof();
	private:
		opus_playback_stream(const opus_playback_stream&);
		opus_playback_stream& operator=(const opus_playback_stream&);
		//Can throw.
		void decode_block();
		float output[OPUS_MAX_OUT];
		unsigned output_left;
		OpusDecoder* decoder;
		opus_stream& stream;
		uint32_t next_block;
		uint32_t blocks;
	};

	opus_playback_stream::opus_playback_stream(opus_stream& data)
		: stream(data)
	{
		int err;
		stream.get_ref();
		stream.lock();
		next_block = 0;
		output_left = 0;
		blocks = stream.blocks();
		decoder = opus_decoder_create(OPUS_SAMPLERATE, 1, &err);
		if(!decoder)
			throw std::bad_alloc();
	}

	opus_playback_stream::~opus_playback_stream()
	{
		//No, we don't unlock the stream.
		stream.put_ref();
		opus_decoder_destroy(decoder);
	}

	bool opus_playback_stream::eof()
	{
		return (next_block >= blocks && !output_left);
	}

	void opus_playback_stream::decode_block()
	{
		if(next_block >= blocks)
			return;
		if(output_left >= OPUS_MAX_OUT)
			return;
		unsigned plen = stream.packet_length(next_block);
		if(plen + output_left > OPUS_MAX_OUT)
			return;
		std::vector<unsigned char> pdata = stream.packet(next_block);
		int c = opus_decode_float(decoder, &pdata[0], pdata.size(), output + output_left,
			OPUS_MAX_OUT - output_left, 0);
		if(c > 0)
			output_left = min(output_left + c, static_cast<unsigned>(OPUS_MAX_OUT));
		else {
			//Bad packet, insert silence.
			for(unsigned i = 0; i < plen; i++)
				output[output_left++] = 0;
		}
		next_block++;
	}

	void opus_playback_stream::read(float* data, size_t samples)
	{
		while(samples > 0) {
			decode_block();
			if(next_block >= blocks && !output_left) {
				//Zerofill remainder.
				for(size_t i = 0; i < samples; i++)
					data[i] = 0;
				return;
			}
			unsigned maxcopy = min(static_cast<unsigned>(samples), output_left);
			memcpy(data, output, maxcopy * sizeof(float));
			if(maxcopy < output_left)
				memmove(output, output + maxcopy, (output_left - maxcopy) * sizeof(float));
			output_left -= maxcopy;
			samples -= maxcopy;
			data += maxcopy;
		}
	}

	void opus_playback_stream::skip(uint64_t samples)
	{
		//First, skip inside decoded samples.
		if(samples < output_left) {
			//Skipping less than amount in output buffer. Just discard from output buffer and try
			//to decode a new block.
			memmove(output, output + samples, (output_left - samples) * sizeof(float));
			output_left -= samples;
			decode_block();
			return;
		} else {
			//Skipping at least the amount of samples in output buffer. First, blank the output buffer
			//and count those towards samples discarded.
			samples -= output_left;
			output_left = 0;
		}
		//While number of samples is so great that adequate convergence period can be ensured without
		//decoding this packet, just skip the samples from the packet.
		while(samples > OPUS_CONVERGE_MAX) {
			samples -= stream.packet_length(next_block++);
			//Did we hit EOF?
			if(next_block >= blocks)
				return;
		}
		//Okay, we are near the point. Start decoding packets.
		while(samples > 0) {
			decode_block();
			//Did we hit EOF?
			if(next_block >= blocks && !output_left)
				return;
			//Skip as many samples as possible.
			unsigned maxskip = min(static_cast<unsigned>(samples), output_left);
			if(maxskip < output_left)
				memmove(output, output + maxskip, (output_left - maxskip) * sizeof(float));
			output_left -= maxskip;
			samples -= maxskip;
		}
		//Just to be nice, decode a extra block.
		decode_block();
	}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//Collection of streams.
	struct stream_collection
	{
	public:
		//Create a new collection.
		//Can throw.
		stream_collection(filesystem_ref filesys);
		//Destroy a collection. All streams are destroyed but not deleted.
		~stream_collection();
		//Get list of streams active at given point.
		std::list<uint64_t> streams_at(uint64_t point);
		//Add a stream into collection.
		//Can throw.
		uint64_t add_stream(opus_stream& stream);
		//Get the filesystem this collection is for.
		filesystem_ref get_filesystem() { return fs; }
		//Unlock all streams in collection.
		void unlock_all();
		//Get stream with given index (NULL if not found).
		opus_stream* get_stream(uint64_t index)
		{
			umutex_class m(mutex);
			if(streams.count(index)) {
				streams[index]->get_ref();
				return streams[index];
			}
			return NULL;
		}
		//Delete a stream.
		//Can throw.
		void delete_stream(uint64_t index);
		//Alter stream timebase.
		//Can throw.
		void alter_stream_timebase(uint64_t index, uint64_t newts);
		//Enumerate all valid stream indices, in time order.
		std::list<uint64_t> all_streams();
		//Export the entiere superstream.
		//Can throw.
		void export_superstream(std::ofstream& out);
	private:
		filesystem_ref fs;
		uint64_t next_index;
		unsigned next_stream;
		mutex_class mutex;
		std::set<uint64_t> free_indices;
		std::map<uint64_t, uint64_t> entries;
		std::multimap<uint64_t, uint64_t> streams_by_time;
		//FIXME: Something more efficient.
		std::map<uint64_t, opus_stream*> streams;
	};

	stream_collection::stream_collection(filesystem_ref filesys)
		: fs(filesys)
	{
		next_stream = 0;
		next_index = 0;
		//The stream index table is in cluster 2.
		uint32_t next_cluster = 2;
		uint32_t next_offset = 0;
		uint32_t i = 0;
		try {
			while(true) {
				char buffer[16];
				size_t r = fs.read_data(next_cluster, next_offset, buffer, 16);
				if(r < 16)
					break;
				uint64_t timebase = read64ube(buffer);
				uint32_t ctrl_cluster = read32ube(buffer + 8);
				uint32_t data_cluster = read32ube(buffer + 12);
				if(ctrl_cluster) {
					opus_stream* x = new opus_stream(timebase, fs, ctrl_cluster, data_cluster);
					entries[next_index] = i;
					streams_by_time.insert(std::make_pair(timebase, next_index));
					streams[next_index++] = x;
				} else
					free_indices.insert(i);
				next_stream = ++i;
			}
		} catch(std::exception& e) {
			for(auto i : streams)
				i.second->put_ref();
			(stringfmt() << "Failed to parse LSVS: " << e.what()).throwex();
		}
	}

	stream_collection::~stream_collection()
	{
		umutex_class m(mutex);
		for(auto i : streams)
			i.second->put_ref();
		streams.clear();
	}

	std::list<uint64_t> stream_collection::streams_at(uint64_t point)
	{
		umutex_class m(mutex);
		std::list<uint64_t> s;
		for(auto i : streams) {
			uint64_t start = i.second->timebase();
			uint64_t end = start + i.second->length();
			if(point >= start && point < end) {
				i.second->get_ref();
				s.push_back(i.first);
			}
		}
		return s;
	}

	uint64_t stream_collection::add_stream(opus_stream& stream)
	{
		try {
			umutex_class m(mutex);
			//Lock the added stream so it doesn't start playing back immediately.
			stream.lock();
			uint64_t idx = next_index++;
			streams[idx] = &stream;
			char buffer[16];
			write64ube(buffer, stream.timebase());
			auto r = stream.get_clusters();
			write32ube(buffer + 8, r.first);
			write32ube(buffer + 12, r.second);
			uint64_t entry_number = 0;
			if(free_indices.empty())
				entry_number = next_stream++;
			else {
				entry_number = *free_indices.begin();
				free_indices.erase(entry_number);
			}
			uint32_t write_cluster = 2;
			uint32_t write_offset = 0;
			uint32_t dummy1, dummy2;
			fs.skip_data(write_cluster, write_offset, 16 * entry_number);
			fs.write_data(write_cluster, write_offset, buffer, 16, dummy1, dummy2);
			streams_by_time.insert(std::make_pair(stream.timebase(), idx));
			entries[idx] = entry_number;
			return idx;
		} catch(std::exception& e) {
			(stringfmt() << "Failed to add stream: " << e.what()).throwex();
		}
	}

	void stream_collection::unlock_all()
	{
		umutex_class m(mutex);
		for(auto i : streams)
			i.second->unlock();
	}

	void stream_collection::delete_stream(uint64_t index)
	{
		umutex_class m(mutex);
		if(!entries.count(index))
			return;
		uint64_t entry_number = entries[index];
		uint32_t write_cluster = 2;
		uint32_t write_offset = 0;
		uint32_t dummy1, dummy2;
		char buffer[16] = {0};
		fs.skip_data(write_cluster, write_offset, 16 * entry_number);
		fs.write_data(write_cluster, write_offset, buffer, 16, dummy1, dummy2);
		auto itr = streams_by_time.lower_bound(streams[index]->timebase());
		auto itr2 = streams_by_time.upper_bound(streams[index]->timebase());
		for(auto x = itr; x != itr2; x++)
			if(x->second == index) {
				streams_by_time.erase(x);
				break;
			}
		streams[index]->delete_stream();
		streams.erase(index);
	}

	void stream_collection::alter_stream_timebase(uint64_t index, uint64_t newts)
	{
		try {
			umutex_class m(mutex);
			if(!streams.count(index))
				return;
			if(entries.count(index)) {
				char buffer[8];
				uint32_t write_cluster = 2;
				uint32_t write_offset = 0;
				uint32_t dummy1, dummy2;
				write64ube(buffer, newts);
				fs.skip_data(write_cluster, write_offset, 16 * entries[index]);
				fs.write_data(write_cluster, write_offset, buffer, 8, dummy1, dummy2);
			}
			auto itr = streams_by_time.lower_bound(streams[index]->timebase());
			auto itr2 = streams_by_time.upper_bound(streams[index]->timebase());
			for(auto x = itr; x != itr2; x++)
				if(x->second == index) {
					streams_by_time.erase(x);
					break;
				}
			streams[index]->timebase(newts);
			streams_by_time.insert(std::make_pair(newts, index));
		} catch(std::exception& e) {
			(stringfmt() << "Failed to alter stream timebase: " << e.what()).throwex();
		}
	}

	std::list<uint64_t> stream_collection::all_streams()
	{
		umutex_class m(mutex);
		std::list<uint64_t> s;
		for(auto i : streams_by_time)
			s.push_back(i.second);
		return s;
	}

	void stream_collection::export_superstream(std::ofstream& out)
	{
		std::list<uint64_t> slist = all_streams();
		//Find the total length of superstream.
		uint64_t len = 0;
		for(auto i : slist) {
			opus_stream* s = get_stream(i);
			if(s) {
				len = max(len, s->timebase() + s->length());
				s->put_ref();
			}
		}
		char header[32];
		write64ule(header, 0x1C586F532EULL);			//Magic and header size.
		write64ule(header + 8, len);
		write64ule(header + 16, 4676829883349860352ULL);	//Sampling rate.
		write64ule(header + 24, 1);
		out.write(header, 32);
		if(!out)
			throw std::runtime_error("Error writing PCM output");

		//Find the first valid stream.
		auto next_i = slist.begin();
		opus_stream* next_stream = NULL;
		while(next_i != slist.end()) {
			next_stream = get_stream(*next_i);
			next_i++;
			if(next_stream)
				break;
		}
		uint64_t next_ts;
		next_ts = next_stream ? next_stream->timebase() : len;

		std::list<opus_playback_stream*> active;
		try {
			for(uint64_t s = 0; s < len;) {
				if(s == next_ts) {
					active.push_back(new opus_playback_stream(*next_stream));
					next_stream->put_ref();
					next_stream = NULL;
					while(next_i != slist.end()) {
						next_stream = get_stream(*next_i);
						next_i++;
						if(!next_stream)
							continue;
						uint64_t next_ts = next_stream->timebase();
						if(next_ts > s)
							break;
						//Okay, this starts too...
						active.push_back(new opus_playback_stream(*next_stream));
						next_stream->put_ref();
						next_stream = NULL;
					};
					next_ts = next_stream ? next_stream->timebase() : len;
				}
				uint64_t maxsamples = min(next_ts - s, static_cast<uint64_t>(OUTPUT_BLOCK));
				maxsamples = min(maxsamples, len - s);
				char outbuf[4 * OUTPUT_BLOCK];
				float buf1[OUTPUT_BLOCK];
				float buf2[OUTPUT_BLOCK];
				for(size_t t = 0; t < maxsamples; t++)
					buf1[t] = 0;
				for(auto t : active) {
					t->read(buf2, maxsamples);
					for(size_t u = 0; u < maxsamples; u++)
						buf1[u] += buf2[u];
				}
				for(auto t = active.begin(); t != active.end();) {
					if((*t)->eof()) {
						auto todel = t;
						t++;
						delete *todel;
						active.erase(todel);
					} else
						t++;
				}
				for(size_t t = 0; t < maxsamples; t++)
					write32sle(outbuf + 4 * t, buf1[t] * 268435456);
				out.write(outbuf, 4 * maxsamples);
				if(!out)
					throw std::runtime_error("Failed to write PCM");
				s += maxsamples;
			}
		} catch(std::exception& e) {
			(stringfmt() << "Failed to export PCM: " << e.what()).throwex();
		}
		for(auto t = active.begin(); t != active.end();) {
			if((*t)->eof()) {
				auto todelete = t;
				t++;
				delete *todelete;
				active.erase(todelete);
			} else
				t++;
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	void start_management_stream(opus_stream& s)
	{
		opus_playback_stream* p = new opus_playback_stream(s);
		umutex_class m(active_playback_streams_lock);
		active_playback_streams.push_back(p);
	}

	void advance_time(uint64_t newtime)
	{
		umutex_class m2(current_collection_lock);
		if(!current_collection) {
			//Clear all.
			umutex_class m(active_playback_streams_lock);
			for(auto i : active_playback_streams)
				delete i;
			active_playback_streams.clear();
			return;
		}
		std::list<uint64_t> sactive = current_collection->streams_at(newtime);
		for(auto j : sactive) {
			opus_stream* i = current_collection->get_stream(j);
			if(!i)
				continue;
			//Don't play locked streams in order to avoid double playing.
			umutex_class m(active_playback_streams_lock);
			try {
				if(!i->islocked())
					active_playback_streams.push_back(new opus_playback_stream(*i));
			} catch(std::exception& e) {
				messages << "Can't start stream: " << e.what() << std::endl;
			}
			i->put_ref();
		}
	}

	void jump_time(uint64_t newtime)
	{
		umutex_class m2(current_collection_lock);
		if(!current_collection) {
			//Clear all.
			umutex_class m(active_playback_streams_lock);
			for(auto i : active_playback_streams)
				delete i;
			active_playback_streams.clear();
			return;
		}
		//Close all currently playing streams.
		{
			umutex_class m(active_playback_streams_lock);
			for(auto i : active_playback_streams)
				delete i;
			active_playback_streams.clear();
		}
		//Unlock all streams, so they will play.
		current_collection->unlock_all();
		//Reopen all streams that should be open (with seeking)
		std::list<uint64_t> sactive = current_collection->streams_at(newtime);
		for(auto j : sactive) {
			opus_stream* i = current_collection->get_stream(j);
			if(!i)
				continue;
			//No need to check for locks, because we just busted all of those.
			uint64_t p = newtime - i->timebase();
			opus_playback_stream* s;
			try {
				s = new opus_playback_stream(*i);
			} catch(std::exception& e) {
				messages << "Can't start stream: " << e.what() << std::endl;
			}
			i->put_ref();
			if(!s)
				continue;
			s->skip(p);
			umutex_class m(active_playback_streams_lock);
			active_playback_streams.push_back(s);
		}
	}

	//Resample.
	void do_resample(audioapi_resampler& r, float* srcbuf, size_t& srcuse, float* dstbuf, size_t& dstuse,
		size_t dstmax, double ratio)
	{
		if(srcuse == 0 || dstuse >= dstmax)
			return;
		float* in = srcbuf;
		size_t in_u = srcuse;
		float* out = dstbuf + dstuse;
		size_t out_u = dstmax - dstuse;
		r.resample(in, in_u, out, out_u, ratio, false);
		size_t offset = in - srcbuf;
		if(offset < srcuse)
			memmove(srcbuf, srcbuf + offset, sizeof(float) * (srcuse - offset));
		srcuse -= offset;
		dstuse = dstmax - out_u;
	}

	//Drain the input buffer.
	void drain_input()
	{
		while(audioapi_voice_r_status() > 0) {
			float buf[256];
			unsigned size = min(audioapi_voice_r_status(), 256u);
			audioapi_record_voice(buf, size);
		}
	}

	//Read the input buffer.
	void read_input(float* buf, size_t& use, size_t maxuse)
	{
		size_t rleft = audioapi_voice_r_status();
		unsigned toread = min(rleft, max(maxuse, use) - use);
		if(toread > 0) {
			audioapi_record_voice(buf + use, toread);
			use += toread;
		}
	}

	//Compress Opus block.
	void compress_opus_block(OpusEncoder* e, float* buf, size_t& use, opus_stream& active_stream,
		double& total_compressed, double& total_blocks)
	{
		const size_t opus_out_max = 1276;
		unsigned char opus_output[opus_out_max];
		size_t cblock = 0;
		if(use >= 960)
			cblock = 960;
		else if(use >= 480)
			cblock = 480;
		else if(use >= 240)
			cblock = 240;
		else if(use >= 120)
			cblock = 120;
		else
			return;		//No valid data to compress.

		int c = opus_encode_float(e, buf, cblock, opus_output, opus_out_max);
		if(c > 0) {
			//Successfully compressed a block.
			size_t opus_output_len = c;
			total_compressed += c;
			total_blocks++;
			try {
				active_stream.write(cblock / 120, opus_output, opus_output_len);
			} catch(std::exception& e) {
				messages << "Error writing data: " << e.what() << std::endl;
			}
		} else
			messages << "Error from Opus encoder: " << opus_strerror(c) << std::endl;
		use -= cblock;
	}

	void update_time()
	{
		uint64_t sampletime;
		bool jumping;
		{
			umutex_class m(time_mutex);
			sampletime = current_time;
			jumping = time_jump;
			time_jump = false;
		}
		if(jumping)
			jump_time(sampletime);
		else
			advance_time(sampletime);
	}

	void decompress_active_streams(float* out, size_t& use)
	{
		size_t base = use;
		use += OUTPUT_BLOCK;
		for(unsigned i = 0; i < OUTPUT_BLOCK; i++)
			out[i + base] = 0;
		//Do it this way to minimize the amount of time playback streams lock
		//is held.
		std::list<opus_playback_stream*> stmp;
		{
			umutex_class m(active_playback_streams_lock);
			stmp = active_playback_streams;
		}
		std::set<opus_playback_stream*> toerase;
		for(auto i : stmp) {
			float tmp[OUTPUT_BLOCK];
			try {
				i->read(tmp, OUTPUT_BLOCK);
			} catch(std::exception& e) {
				messages << "Failed to decompress: " << e.what() << std::endl;
				for(unsigned j = 0; j < OUTPUT_BLOCK; j++)
					tmp[j] = 0;
			}
			for(unsigned j = 0; j < OUTPUT_BLOCK; j++)
				out[j + base] += tmp[j];
			if(i->eof())
				toerase.insert(i);
		}
		{
			umutex_class m(active_playback_streams_lock);
			for(auto i = active_playback_streams.begin(); i != active_playback_streams.end();) {
				if(toerase.count(*i)) {
					auto toerase = i;
					i++;
					delete *toerase;
					active_playback_streams.erase(toerase);
				} else
					i++;
			}
		}
	}

	void handle_tangent_positive_edge(OpusEncoder* e, opus_stream*& active_stream,
		double& total_compressed, double& total_blocks)
	{
		umutex_class m2(current_collection_lock);
		if(!current_collection)
			return;
		static unsigned output_seq = 0;
		opus_encoder_ctl(e, OPUS_RESET_STATE);
		total_compressed = 0;
		total_blocks = 0;
		uint64_t ctime;
		{
			umutex_class m(time_mutex);
			ctime = current_time;
		}
		active_stream = NULL;
		try {
			active_stream = new opus_stream(ctime, current_collection->get_filesystem());
		} catch(std::exception& e) {
			messages << "Can't start stream: " << e.what() << std::endl;
			return;
		}
		messages << "Tangent positive edge." << std::endl;
	}

	void handle_tangent_negative_edge(opus_stream*& active_stream, double total_compressed,
		double total_blocks)
	{
		umutex_class m2(current_collection_lock);
		messages << "Tangent negative edge. "
			<< total_compressed << " bytes in " << total_blocks << " blocks, "
			<< (0.4 * total_compressed / total_blocks) << " kbps" << std::endl;
		if(current_collection) {
			try {
				current_collection->add_stream(*active_stream);
			} catch(std::exception& e) {
				messages << "Can't add stream: " << e.what() << std::endl;
				active_stream->put_ref();
			}
		} else
			active_stream->put_ref();
		active_stream = NULL;
	}

	class inthread_th : public worker_thread
	{
	public:
		inthread_th()
		{
			rptr = 0;
			fire();
		}
	protected:
		void entry()
		{
			try {
				entry2();
			} catch(std::bad_alloc& e) {
				OOM_panic();
			} catch(std::exception& e) {
				messages << "AIEEE... Fatal exception in voice thread: " << e.what() << std::endl;
			}
		}
		void entry2()
		{
			const size_t f = sizeof(float);
			double position = 0;
			int err;
			OpusEncoder* oenc = opus_encoder_create(OPUS_SAMPLERATE, 1, OPUS_APPLICATION_VOIP, &err);
			opus_encoder_ctl(oenc, OPUS_SET_BITRATE(OPUS_BITRATE));
			audioapi_resampler rin;
			audioapi_resampler rout;
			const unsigned buf_max = 6144;	//These buffers better be large.
			size_t buf_in_use = 0;
			size_t buf_inr_use = 0;
			size_t buf_outr_use = 0;
			size_t buf_out_use = 0;
			float buf_in[buf_max];
			float buf_inr[OPUS_BLOCK_SIZE];
			float buf_outr[OUTPUT_SIZE];
			float buf_out[buf_max];
			double total_compressed = 0;
			double total_blocks = 0;
			opus_stream* active_stream = NULL;

			drain_input();
			while(1) {
				uint64_t ticks = get_utime();
				//Handle tangent edgets.
				if(active_flag && !active_stream) {
					drain_input();
					buf_in_use = 0;
					buf_inr_use = 0;
					handle_tangent_positive_edge(oenc, active_stream, total_compressed,
						total_blocks);
				}
				else if(!active_flag && active_stream)
					handle_tangent_negative_edge(active_stream, total_compressed, total_blocks);

				//Read input, up to 25ms.
				unsigned rate = audioapi_voice_rate();
				size_t dbuf_max = min(buf_max, rate / REC_THRESHOLD_DIV);
				read_input(buf_in, buf_in_use, dbuf_max);

				//Resample up to full opus block.
				do_resample(rin, buf_in, buf_in_use, buf_inr, buf_inr_use, OPUS_BLOCK_SIZE,
					1.0 * OPUS_SAMPLERATE / rate);

				//If we have full opus block and recording is enabled, compress it.
				if(buf_inr_use >= OPUS_BLOCK_SIZE && active_stream)
					compress_opus_block(oenc, buf_inr, buf_inr_use, *active_stream,
						total_compressed, total_blocks);

				//Update time, starting/ending streams.
				update_time();

				//Decompress active streams.
				if(buf_outr_use < BLOCK_THRESHOLD)
					decompress_active_streams(buf_outr, buf_outr_use);

				//Resample to output rate.
				do_resample(rout, buf_outr, buf_outr_use, buf_out, buf_out_use, buf_max,
					1.0 * rate / OPUS_SAMPLERATE);

				//Output stuff.
				if(buf_out_use > 0 && audioapi_voice_p_status2() < rate / PLAY_THRESHOLD_DIV) {
					audioapi_play_voice(buf_out, buf_out_use);
					buf_out_use = 0;
				}

				//Sleep a bit to save CPU use.
				uint64_t ticks_spent = get_utime() - ticks;
				if(ticks_spent < ITERATION_TIME)
					usleep(ITERATION_TIME - ticks_spent);
			}
			opus_encoder_destroy(oenc);
			delete current_collection;
		}
	private:
		size_t rptr;
		double position;
	};

	//The tangent function.
	function_ptr_command<> ptangent("+tangent", "Voice tangent",
		"Syntax: +tangent\nVoice tangent.\n",
		[]() throw(std::bad_alloc, std::runtime_error) {
			active_flag = true;
		});
	function_ptr_command<> ntangent("-tangent", "Voice tangent",
		"Syntax: -tangent\nVoice tangent.\n",
		[]() throw(std::bad_alloc, std::runtime_error) {
			active_flag = false;
		});
	
}

void voice_frame_number(uint64_t newframe, double rate)
{
	if(rate == last_rate && last_frame_number == newframe)
		return;
	umutex_class m(time_mutex);
	current_time = newframe / rate * OPUS_SAMPLERATE;
	if(fabs(rate - last_rate) > 1e-6 || last_frame_number + 1 != newframe)
		time_jump = true;
	last_frame_number = newframe;
	last_rate = rate;
}

void voicethread_task()
{
	new inthread_th;
}

uint64_t voicesub_parse_timebase(const std::string& n)
{
	std::string x = n;
	if(x.length() > 0 && x[x.length() - 1] == 's') {
		x = x.substr(0, x.length() - 1);
		return 48000 * parse_value<double>(x);
	} else
		return parse_value<uint64_t>(x);
}

namespace
{
	function_ptr_command<> list_streams("list-streams", "List streams ", "list-streams\nList known voice streams",
		[]() throw(std::bad_alloc, std::runtime_error) {
			umutex_class m2(current_collection_lock);
			if(!current_collection) {
				messages << "No voice streams loaded." << std::endl;
				return;
			}
			messages << "-----------------------" << std::endl;
			for(auto i : current_collection->all_streams()) {
				opus_stream* s = current_collection->get_stream(i);
				if(!s)
					continue;
				messages << "ID #" << i << ": base=" << s->timebase() << " ("
					<< (s->timebase() / 48000.0) << "s), length=" << s->length() << " ("
					<< (s->length() / 48000.0) << "s)" << std::endl;
				s->put_ref();
			}
			messages << "-----------------------" << std::endl;
		});

	function_ptr_command<const std::string&> delete_stream("delete-stream", "Delete a stream",
		"delete-stream <id>\nDelete a voice stream with given ID.",
		[](const std::string& x) throw(std::bad_alloc, std::runtime_error) {
			umutex_class m2(current_collection_lock);
			uint64_t id = parse_value<uint64_t>(x);
			if(!current_collection) {
				messages << "No voice streams loaded." << std::endl;
				return;
			}
			opus_stream* s = current_collection->get_stream(id);
			if(!s) {
				messages << "Error, no such stream found." << std::endl;
				return;
			}
			s->put_ref();
			current_collection->delete_stream(id);
			messages << "Deleted stream #" << id << "." << std::endl;
		});

	function_ptr_command<const std::string&> play_stream("play-stream", "Play a stream", "play-stream <id>\n"
		"Play a voice stream with given ID.",
		[](const std::string& x) throw(std::bad_alloc, std::runtime_error) {
			umutex_class m2(current_collection_lock);
			uint64_t id = parse_value<uint64_t>(x);
			if(!current_collection) {
				messages << "No voice streams loaded." << std::endl;
				return;
			}
			opus_stream* s = current_collection->get_stream(id);
			if(!s) {
				messages << "Error, no such stream found." << std::endl;
				return;
			}
			try {
				start_management_stream(*s);
			} catch(...) {
				s->put_ref();
				throw;
			}
			s->put_ref();
			messages << "Playing stream #" << id << "." << std::endl;
		});

	function_ptr_command<const std::string&> change_timebase("change-timebase", "Change stream timebase",
		"change-timebase <id> <newbase>\nChange timebase of given stream",
		[](const std::string& x) throw(std::bad_alloc, std::runtime_error) {
			umutex_class m2(current_collection_lock);
			if(!current_collection) {
				messages << "No voice streams loaded." << std::endl;
				return;
			}
			auto r = regex("([0-9]+)[ \t]+([^ \t]*)", x);
			if(!r) {
				messages << "Syntax: change-timebase <id> <timebase>" << std::endl;
				return;
			}
			uint64_t id = parse_value<uint64_t>(r[1]);
			uint64_t tbase = voicesub_parse_timebase(r[2]);
			opus_stream* s = current_collection->get_stream(id);
			if(!s) {
				messages << "Error, no such stream found." << std::endl;
				return;
			}
			s->put_ref();
			current_collection->alter_stream_timebase(id, tbase);
			messages << "Timebase of stream #" << id << " is now " << (tbase / 48000.0) << "s"
				<< std::endl;
		});

	void import_cmd_common(const std::string& x, const char* postfix, bool mode)
	{
		umutex_class m2(current_collection_lock);
		if(!current_collection) {
			messages << "No voice streams loaded." << std::endl;
			return;
		}
		auto r = regex("([^ \t]+)[ \t]+(.+)", x);
		if(!r) {
			messages << "Syntax: import-stream-" << postfix << " <timebase> <filename>" << std::endl;
			return;
		}
		uint64_t tbase = voicesub_parse_timebase(r[1]);
		std::string fname = r[2];
		std::ifstream s(fname, std::ios_base::in | std::ios_base::binary);
		if(!s) {
			messages << "Can't open '" << fname << "'" << std::endl;
			return;
		}
		opus_stream* st = new opus_stream(tbase, current_collection->get_filesystem(), s, mode);
		uint64_t id;
		try {
			id = current_collection->add_stream(*st);
		} catch(...) {
			st->delete_stream();
			throw;
		}
		st->unlock();	//Not locked.
		messages << "Imported stream (" << st->length() / 48000.0 << "s) as ID #" << id << std::endl;
	}

	function_ptr_command<const std::string&> import_stream_c("import-stream-opus", "Import a opus stream",
		"import-stream-opus <timebase> <filename>\nImport opus stream from <filename>, starting at "
		"<timebase>",
		[](const std::string& x) throw(std::bad_alloc, std::runtime_error) {
			import_cmd_common(x, "opus", true);
		});

	function_ptr_command<const std::string&> import_stream_p("import-stream-pcm", "Import a PCM stream",
		"import-stream-pcm <timebase> <filename>\nImport PCM stream from <filename>, starting at <timebase>",
		[](const std::string& x) throw(std::bad_alloc, std::runtime_error) {
			import_cmd_common(x, "pcm", false);
		});

	void export_cmd_common(const std::string& x, const char* postfix, bool mode)
	{
		umutex_class m2(current_collection_lock);
		if(!current_collection) {
			messages << "No voice streams loaded." << std::endl;
			return;
		}
		auto r = regex("([0-9]+)[ \t]+(.+)", x);
		if(!r) {
			messages << "Syntax: export-stream-" << postfix << " <id> <filename>" << std::endl;
			return;
		}
		uint64_t id = parse_value<uint64_t>(r[1]);
		std::string fname = r[2];
		std::ofstream s(fname, std::ios_base::out | std::ios_base::binary);
		if(!s) {
			messages << "Can't open '" << fname << "'" << std::endl;
			return;
		}
		opus_stream* st = current_collection->get_stream(id);
		if(!st) {
			messages << "Error, stream #" << id << " does not exist." << std::endl;
			return;
		}
		try {
			st->export_stream(s, mode);
			messages << "Exported stream #" << id << " (" << st->length() / 48000.0 << "s)" << std::endl;
		} catch(std::exception& e) {
			messages << "Export failed: " << e.what();
		}
		st->put_ref();
	}

	function_ptr_command<const std::string&> export_stream_c("export-stream-opus", "Export a opus stream",
		"export-stream-opus <id> <filename>\nExport opus stream <id> to <filename>",
		[](const std::string& x) throw(std::bad_alloc, std::runtime_error) {
			export_cmd_common(x, "opus", true);
		});

	function_ptr_command<const std::string&> export_stream_p("export-stream-pcm", "Export a PCM stream",
		"export-stream-pcm <id> <filename>\nExport PCM stream <id> to <filename>",
		[](const std::string& x) throw(std::bad_alloc, std::runtime_error) {
			export_cmd_common(x, "pcm", false);
		});

	function_ptr_command<const std::string&> export_sstream("export-superstream", "Export superstream",
		"export-superstream <filename>\nExport PCM superstream to <filename>",
		[](const std::string& x) throw(std::bad_alloc, std::runtime_error) {
			umutex_class m2(current_collection_lock);
			if(!current_collection)
				return;
			std::ofstream s(x, std::ios_base::out | std::ios_base::binary);
			if(!s) {
				messages << "Can't open '" << x << "'" << std::endl;
				return;
			}
			current_collection->export_superstream(s);
			messages << "Superstream exported." << std::endl;
		});

	function_ptr_command<const std::string&> load_collection("load-collection", "Load voice subtitling "
		"collection", "load-collection <filename>\nLoad voice subtitling collection from <filename>",
		[](const std::string& x) throw(std::bad_alloc, std::runtime_error) {
			umutex_class m2(current_collection_lock);
			filesystem_ref newfs;
			stream_collection* newc;
			try {
				newfs = filesystem_ref(x);
				newc = new stream_collection(newfs);
			} catch(std::exception& e) {
				messages << "Can't load '" << x << "': " << e.what();
				return;
			}
			if(current_collection)
				delete current_collection;
			current_collection = newc;
			messages << "Loaded '" << x << "'" << std::endl;
		});

	function_ptr_command<> unload_collection("unload-collection", "Unload voice subtitling collection",
		"unload-collection\nUnload voice subtitling collection",
		[]() throw(std::bad_alloc, std::runtime_error) {
			umutex_class m2(current_collection_lock);
			if(current_collection)
				delete current_collection;
			current_collection = NULL;
			messages << "Collection unloaded" << std::endl;
		});

	inverse_key itangent("+tangent", "Movie‣Voice tangent");
}

bool voicesub_collection_loaded()
{
	umutex_class m2(current_collection_lock);
	return (current_collection != NULL);
}

std::list<playback_stream_info> voicesub_get_stream_info()
{
	umutex_class m2(current_collection_lock);
	std::list<playback_stream_info> in;
	if(!current_collection)
		return in;
	for(auto i : current_collection->all_streams()) {
		opus_stream* s = current_collection->get_stream(i);
		playback_stream_info pi;
		if(!s)
			continue;
		pi.id = i;
		pi.base = s->timebase();
		pi.length = s->length();
		try {
			in.push_back(pi);
		} catch(...) {
		}
		s->put_ref();
	}
	return in;
}

void voicesub_play_stream(uint64_t id)
{
	umutex_class m2(current_collection_lock);
	if(!current_collection)
		throw std::runtime_error("No collection loaded");
	opus_stream* s = current_collection->get_stream(id);
	if(!s)
		return;
	try {
		start_management_stream(*s);
	} catch(...) {
		s->put_ref();
		throw;
	}
	s->put_ref();
}

void voicesub_export_stream(uint64_t id, const std::string& filename, bool opus)
{
	umutex_class m2(current_collection_lock);
	if(!current_collection)
		throw std::runtime_error("No collection loaded");
	opus_stream* st = current_collection->get_stream(id);
	if(!st)
		return;
	std::ofstream s(filename, std::ios_base::out | std::ios_base::binary);
	if(!s) {
		st->put_ref();
		throw std::runtime_error("Can't open output file");
	}
	try {
		st->export_stream(s, opus);
	} catch(std::exception& e) {
		st->put_ref();
		(stringfmt() << "Export failed: " << e.what()).throwex();
	}
	st->put_ref();
}

uint64_t voicesub_import_stream(uint64_t ts, const std::string& filename, bool opus)
{
	umutex_class m2(current_collection_lock);
	if(!current_collection)
		throw std::runtime_error("No collection loaded");
	
	std::ifstream s(filename, std::ios_base::in | std::ios_base::binary);
	if(!s)
		throw std::runtime_error("Can't open input file");
	opus_stream* st = new opus_stream(ts, current_collection->get_filesystem(), s, opus);
	uint64_t id;
	try {
		id = current_collection->add_stream(*st);
	} catch(...) {
		st->delete_stream();
		throw;
	}
	st->unlock();	//Not locked.
	return id;
}

void voicesub_delete_stream(uint64_t id)
{
	umutex_class m2(current_collection_lock);
	if(!current_collection)
		throw std::runtime_error("No collection loaded");
	current_collection->delete_stream(id);
}

void voicesub_export_superstream(const std::string& filename)
{
	umutex_class m2(current_collection_lock);
	if(!current_collection)
		throw std::runtime_error("No collection loaded");
	std::ofstream s(filename, std::ios_base::out | std::ios_base::binary);
	if(!s)
		throw std::runtime_error("Can't open output file");
	current_collection->export_superstream(s);
}

void voicesub_load_collection(const std::string& filename)
{
	umutex_class m2(current_collection_lock);
	filesystem_ref newfs;
	stream_collection* newc;
	newfs = filesystem_ref(filename);
	newc = new stream_collection(newfs);
	if(current_collection)
		delete current_collection;
	current_collection = newc;
}

void voicesub_unload_collection()
{
	umutex_class m2(current_collection_lock);
	if(current_collection)
		delete current_collection;
	current_collection = NULL;
}

void voicesub_alter_timebase(uint64_t id, uint64_t ts)
{
	umutex_class m2(current_collection_lock);
	if(!current_collection)
		throw std::runtime_error("No collection loaded");
	current_collection->alter_stream_timebase(id, ts);
}

double voicesub_ts_seconds(uint64_t ts)
{
	return ts / 48000.0;
}
#else
void voicethread_task()
{
}

void voice_frame_number(uint64_t newframe, double rate)
{
}

#endif