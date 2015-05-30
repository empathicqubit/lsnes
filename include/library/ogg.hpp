#ifndef _library__ogg__hpp__included__
#define _library__ogg__hpp__included__

#include <cstdint>
#include <stdexcept>
#include <vector>
#include <map>

namespace ogg
{
/**
 * A packet in Ogg bitstream.
 */
class packet
{
public:
/**
 * Constructor.
 */
	packet() {}
/**
 * Constructor.
 */
	packet(uint64_t granule, bool first, bool last, bool spans, bool eos, bool bos,
		const std::vector<uint8_t>& d);
/**
 * Is the packet first within its page?
 */
	bool get_first_page() const throw() { return first_page; }
/**
 * Is the packet last within its page?
 */
	bool get_last_page() const throw() { return last_page; }
/**
 * Is the packet spanning multiple pages?
 */
	bool get_spans_page() const throw() { return spans_page; }
/**
 * Is atomic (first, last and on one page)?
 */
	bool get_atomic() const throw() { return first_page && last_page && !spans_page; }
/**
 * Get the granule position within page the packet finished at.
 */
	uint64_t get_granulepos() const throw() { return granulepos; }
/**
 * Does the page this ends on have EOS set?
 */
	bool get_on_eos_page() const throw() { return eos_page; }
/**
 * Does the page this starts on have BOS set?
 */
	bool get_on_bos_page() const throw() { return bos_page; }
/**
 * Get the packet data.
 */
	const std::vector<uint8_t>& get_vector() const throw() { return data; }
/**
 * Get length of packet data.
 */
	size_t get_length() const throw() { return data.size(); }
/**
 * Get packet data.
 */
	const uint8_t* get_data() const throw() { return &data[0]; }
private:
	bool first_page;
	bool last_page;
	bool spans_page;
	bool eos_page;
	bool bos_page;
	uint64_t granulepos;
	std::vector<uint8_t> data;
};

/**
 * A page in Ogg bitstream.
 */
class page
{
public:
/**
 * Create a new blank page.
 */
	page() throw();
/**
 * Create a page, reading a buffer.
 *
 * Parameter buffer: The buffer to read.
 * Parameter advance: The number of bytes in packet is stored here.
 * Throws std::runtime_error: Bad packet.
 */
	page(const char* buffer, size_t& advance) throw(std::runtime_error);
/**
 * Scan a buffer for pages.
 *
 * Parameter buffer: The buffer to scan.
 * Parameter bufferlen: The length of buffer. Should be at least 65307, unless there's not that much available.
 * Parameter eof: If set, assume physical stream ends after this buffer.
 * Parameter advance: Amount to advance the pointer is stored here.
 * Returns: True if packet was found, false if not.
 */
	static bool scan(const char* buffer, size_t bufferlen, bool eof, size_t& advance) throw();
/**
 * Get the continue flag of packet.
 */
	bool get_continue() const throw() { return flag_continue; }
/**
 * Set the continue flag of packet.
 */
	void set_continue(bool c) throw() { flag_continue = c; }
/**
 * Get the BOS flag of packet.
 */
	bool get_bos() const throw() { return flag_bos; }
/**
 * Set the BOS flag of packet.
 */
	void set_bos(bool b) throw() { flag_bos = b; }
/**
 * Get the EOS flag of packet.
 */
	bool get_eos() const throw() { return flag_eos; }
/**
 * Set the EOS flag of packet.
 */
	void set_eos(bool e) throw() { flag_eos = e; }
/**
 * Get the granulepos of packet.
 */
	uint64_t get_granulepos() const throw() { return granulepos; }
/**
 * Set the granulepos of packet.
 */
	void set_granulepos(uint64_t g) throw() { granulepos = g; }
/**
 * Get stream identifier.
 */
	uint32_t get_stream() const throw() { return stream; }
/**
 * Set stream identifier.
 */
	void set_stream(uint32_t s) throw() { stream = s; }
/**
 * Get stream identifier.
 */
	uint32_t get_sequence() const throw() { return sequence; }
/**
 * Set stream identifier.
 */
	void set_sequence(uint32_t s) throw() { sequence = s; }
/**
 * Get number of packets.
 */
	uint8_t get_packet_count() const throw() { return packet_count; }
/**
 * Get the packet.
 */
	std::pair<const uint8_t*, size_t> get_packet(size_t packetno) const throw()
	{
		if(packetno >= packet_count)
			return std::make_pair(reinterpret_cast<const uint8_t*>(NULL), 0);
		else
			return std::make_pair(data + packets[packetno], packets[packetno + 1] - packets[packetno]);
	}
/**
 * Get the last packet incomplete flag.
 */
	bool get_last_packet_incomplete() const throw() { return last_incomplete; }
/**
 * Get amount of data that can be written as a complete packet.
 */
	size_t get_max_complete_packet() const throw();
/**
 * Append a complete packet to page.
 *
 * Parameter d: The data to append.
 * Parameter dlen: The length of data to append.
 * Returns: True on success, false on failure.
 */
	bool append_packet(const uint8_t* d, size_t dlen) throw();
/**
 * Append a possibly incomplete packet to page.
 *
 * Parameter d: The data to append. Adjusted.
 * Parameter dlen: The length of data to append. Adjusted
 * Returns: True if write was complete, false if incomplete.
 */
	bool append_packet_incomplete(const uint8_t*& d, size_t& dlen) throw();
/**
 * Get number of octets it takes to serialize this.
 */
	size_t serialize_size() const throw() { return 27 + segment_count + data_count; }
/**
 * Serialize this packet.
 *
 * Parameter buffer: Buffer to serialize to (use serialize_size() to find the size).
 */
	void serialize(char* buffer) const throw();
/**
 * Get debugging info for stream this page is from.
 */
	text stream_debug_id() const throw(std::bad_alloc);
/**
 * Get debugging info for this page.
 */
	text page_debug_id() const throw(std::bad_alloc);
/**
 * The special granule pos for nothing.
 */
	const static uint64_t granulepos_none;
private:
	uint8_t version;
	bool flag_continue;
	bool flag_bos;
	bool flag_eos;
	bool last_incomplete;
	uint64_t granulepos;
	uint32_t stream;
	uint32_t sequence;
	uint8_t segment_count;
	uint8_t packet_count;
	uint16_t data_count;
	uint8_t data[65025];
	uint8_t segments[255];
	uint16_t packets[256];
};

/**
 * Ogg demuxer.
 */
class demuxer
{
public:
/**
 * Create a new demuxer.
 */
	demuxer(std::ostream& _errors_to);
/**
 * Demuxer wants a page in?
 */
	bool wants_page_in() const throw() { return (dpacket == packets && !ended); }
/**
 * Demuxer wants a packet out?
 */
	bool wants_packet_out() const throw() { return (dpacket < packets); }
/**
 * Input a page.
 */
	bool page_in(const page& p);
/**
 * Output a packet.
 */
	void packet_out(packet& pkt);
/**
 * Discard a packet.
 */
	void discard_packet();
private:
	uint32_t inc1(uint32_t x) { return x + 1; }
	uint64_t page_fullseq(uint32_t seq);
	void update_pageseq(uint32_t new_seq);
	bool complain_lost_page(uint32_t new_seq, uint32_t stream);
	void complain_continue_errors(unsigned flags, uint32_t seqno, uint32_t stream, uint32_t pkts,
		uint64_t granule);
	std::ostream& errors_to;
	std::vector<uint8_t> partial;
	bool started_bos;
	bool seen_page;
	bool damaged_packet;
	uint32_t imprint_stream;
	uint32_t page_seq;
	uint32_t page_era;
	page last_page;
	uint32_t dpacket;
	uint32_t packets;
	uint64_t last_granulepos;
	bool ended;
};

/**
 * Ogg muxer.
 */
class muxer
{
public:
/**
 * Create a muxer.
 */
	muxer(uint32_t streamid, uint64_t _seq = 0);
/**
 * Wants a packet?
 */
	bool wants_packet_in() const throw();
/**
 * Packet of this size fits on current page?
 */
	bool packet_fits(size_t pktsize) const throw();
/**
 * Has a page to output?
 */
	bool has_page_out() const throw();
/**
 * Input a packet.
 */
	void packet_in(const std::vector<uint8_t>& data, uint64_t granule);
/**
 * Signal end of stream.
 */
	void signal_eos();
/**
 * Output a page.
 */
	void page_out(page& p);
private:
	uint32_t strmid;
	page buffer;
	std::vector<uint8_t> buffered;
	size_t written;
	uint64_t seq;
	bool eos_asserted;
	uint64_t granulepos;
};

/**
 * Ogg stream reader.
 */
class stream_reader
{
public:
/**
 * Constructor.
 */
	stream_reader() throw();
/**
 * Destructor.
 */
	virtual ~stream_reader() throw();
/**
 * Read some data.
 *
 * Parameter buffer: The buffer to store the data to.
 * Parameter size: The maximum size to read.
 * Returns: The number of bytes actually read.
 */
	virtual size_t read(char* buffer, size_t size) throw(std::exception) = 0;
/**
 * Read a page from stream.
 *
 * Parameter page: The page is assigned here if successful.
 * Returns: True if page was obtained, false if not.
 */
	bool get_page(page& page) throw(std::exception);
/**
 * Set stream to report errors to.
 *
 * Parameter strm: The stream.
 */
	void set_errors_to(std::ostream& os);
/**
 * Starting offset of last packet returned.
 */
	uint64_t get_last_offset() { return last_offset; }
private:
	stream_reader(const stream_reader&);
	stream_reader& operator=(const stream_reader&);
	void fill_buffer();
	void discard_buffer(size_t amount);
	bool eof;
	char buffer[65536];
	size_t left;
	uint64_t last_offset;
	uint64_t start_offset;
	std::ostream* errors_to;
};

/**
 * Ogg stream reader based on std::istream.
 */
class stream_reader_iostreams : public stream_reader
{
public:
/**
 * Constructor.
 *
 * Parameter stream: The stream to read the data from.
 */
	stream_reader_iostreams(std::istream& stream);
/**
 * Destructor.
 */
	~stream_reader_iostreams() throw();

	size_t read(char* buffer, size_t size) throw(std::exception);
private:
	std::istream& is;
};

/**
 * Ogg stream writer.
 */
class stream_writer
{
public:
/**
 * Constructor.
 */
	stream_writer() throw();
/**
 * Destructor.
 */
	virtual ~stream_writer() throw();
/**
 * Write data.
 *
 * Parameter data: The data to write.
 * Parameter size: The size to write.
 */
	virtual void write(const char* buffer, size_t size) throw(std::exception) = 0;
/**
 * Write a page to stream.
 *
 * Parameter page: The page to write.
 */
	void put_page(const page& page) throw(std::exception);
private:
	stream_writer(const stream_writer&);
	stream_writer& operator=(const stream_writer&);
};

/**
 * Ogg stream writer based on std::istream.
 */
class stream_writer_iostreams : public stream_writer
{
public:
/**
 * Constructor.
 *
 * Parameter stream: The stream to read the data from.
 */
	stream_writer_iostreams(std::ostream& stream);
/**
 * Destructor.
 */
	~stream_writer_iostreams() throw();

	void write(const char* buffer, size_t size) throw(std::exception);
private:
	std::ostream& os;
};
}

#endif
