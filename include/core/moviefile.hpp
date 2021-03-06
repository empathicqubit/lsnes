#ifndef _moviefile__hpp__included__
#define _moviefile__hpp__included__

#include <string>
#include <vector>
#include <stdexcept>
#include <map>
#include "core/controllerframe.hpp"
#include "core/rom-small.hpp"
#include "core/subtitles.hpp"
#include "interface/romtype.hpp"
#include "library/rrdata.hpp"
#include "library/zip.hpp"

class loaded_rom;

/**
 * Dynamic state parts of movie file.
 */
struct dynamic_state
{
/**
 * Ctor.
 */
	dynamic_state();
/**
 * Contents of SRAM on time of savestate (if is_savestate is true).
 */
	std::map<std::string, std::vector<char>> sram;
/**
 * Core savestate (if is_savestate is true).
 */
	std::vector<char> savestate;		//Savestate to load (if is_savestate is true).
/**
 * Host memory (if is_savestate is true).
 */
	std::vector<char> host_memory;
/**
 * Screenshot (if is_savestate is true).
 */
	std::vector<char> screenshot;
/**
 * Current frame (if is_savestate is true).
 */
	uint64_t save_frame;
/**
 * Number of lagged frames (if is_savestate is true).
 */
	uint64_t lagged_frames;
/**
 * Poll counters (if is_savestate is true).
 */
	std::vector<uint32_t> pollcounters;
/**
 * Poll flag.
 */
	unsigned poll_flag;
/**
 * Current RTC second.
 */
	int64_t rtc_second;
/**
 * Current RTC subsecond.
 */
	int64_t rtc_subsecond;
/**
 * Active macros at savestate.
 */
	std::map<std::string, uint64_t> active_macros;
/**
 * Clear the state to power-on defaults.
 */
	void clear(int64_t sec, int64_t ssec, const std::map<std::string, std::vector<char>>& initsram);
/**
 * Swap the dynamic state with another.
 */
	void swap(dynamic_state& s) throw();
};

/**
 * This structure gives parsed representationg of movie file, as result of decoding or for encoding.
 */
struct moviefile
{
/**
 * Brief information
 */
	struct brief_info
	{
		brief_info() { current_frame = 0; rerecords = 0; }
		brief_info(const std::string& filename);
		std::string sysregion;
		std::string corename;
		std::string projectid;
		std::string hash[ROM_SLOT_COUNT];
		std::string hashxml[ROM_SLOT_COUNT];
		std::string hint[ROM_SLOT_COUNT];
		uint64_t current_frame;
		uint64_t rerecords;
	private:
		void load(zip::reader& r);
		void binary_io(int s);
	};
/**
 * Extract branches.
 */
	struct branch_extractor
	{
		branch_extractor(const std::string& filename);
		virtual ~branch_extractor();
		virtual std::set<std::string> enumerate() { return real->enumerate(); }
		virtual void read(const std::string& name, portctrl::frame_vector& v) { real->read(name, v); }
	protected:
		branch_extractor() { real = NULL; }
	private:
		branch_extractor* real;
	};
/**
 * Extract SRAMs.
 */
	struct sram_extractor
	{
		sram_extractor(const std::string& filename);
		virtual ~sram_extractor();
		virtual std::set<std::string> enumerate() { return real->enumerate(); }
		virtual void read(const std::string& name, std::vector<char>& v) { real->read(name, v); }
	protected:
		sram_extractor() { real = NULL; }
	private:
		sram_extractor* real;
	};
/**
 * Identify if file is movie/savestate file or not.
 */
	static bool is_movie_or_savestate(const std::string& filename);
/**
 * This constructor construct movie structure with default settings.
 *
 * throws std::bad_alloc: Not enough memory.
 */
	moviefile();

/**
 * This constructor loads a movie/savestate file and fills structure accordingly.
 *
 * parameter filename: The file to load.
 * parameter romtype: Type of ROM.
 * throws std::bad_alloc: Not enough memory.
 * throws std::runtime_error: Can't load the movie file
 */
	moviefile(const std::string& filename, core_type& romtype);

/**
 * Fill a stub movie with specified loaded ROM.
 *
 * Parameter rom: The rom.
 * Parameter settings: The settings.
 * Parameter rtc_sec: The RTC seconds value.
 * Parameter rtc_subsec: The RTC subseconds value.
 */
	moviefile(loaded_rom& rom, std::map<std::string, std::string>& c_settings, uint64_t rtc_sec,
		uint64_t rtc_subsec);

/**
 * Reads this movie structure and saves it into file.
 *
 * parameter filename: The file to save to.
 * parameter compression: The compression level 0-9. 0 is uncompressed.
 * parameter binary: Save in binary form if true.
 * parameter rrd: The rerecords data.
 * throws std::bad_alloc: Not enough memory.
 * throws std::runtime_error: Can't save the movie file.
 */
	void save(const std::string& filename, unsigned compression, bool binary, rrdata_set& rrd, bool as_state);
/**
 * Reads this movie structure and saves it to stream (uncompressed ZIP).
 */
	void save(std::ostream& outstream, rrdata_set& rrd, bool as_state);
/**
 * Force loading as corrupt.
 */
	bool force_corrupt;
/**
 * What is the ROM type and region?
 */
	core_sysregion* gametype;
/**
 * Settings.
 */
	std::map<std::string, std::string> settings;
/**
 * Emulator Core version string.
 */
	std::string coreversion;
/**
 * Name of the game
 */
	std::string gamename;
/**
 * Project ID (used to identify if two movies are from the same project).
 */
	std::string projectid;
/**
 * Rerecord count (only saved).
 */
	std::string rerecords;
/**
 * Rerecord count (memory saves only).
 */
	uint64_t rerecords_mem;
/**
 * SHA-256 of ROM (empty string if none).
 */
	std::string romimg_sha256[ROM_SLOT_COUNT];
/**
 * SHA-256 of ROM XML (empty string if none).
 */
	std::string romxml_sha256[ROM_SLOT_COUNT];
/**
 * ROM name hint (empty string if none).
 */
	std::string namehint[ROM_SLOT_COUNT];
/**
 * Authors of the run, first in each pair is full name, second is nickname.
 */
	std::vector<std::pair<std::string, std::string>> authors;
/**
 * Contents of SRAM on time of initial powerup.
 */
	std::map<std::string, std::vector<char>> movie_sram;
/**
 * Contents of RAM on time of initial powerup.
 */
	std::map<std::string, std::vector<char>> ramcontent;
/**
 * Anchoring core savestate (if not empty).
 */
	std::vector<char> anchor_savestate;
/**
 * Compressed rrdata.
 */
	std::vector<char> c_rrdata;
/**
 * Input for each (sub)frame (points to active branch).
 */
	portctrl::frame_vector* input;
/**
 * Branches.
 */
	std::map<std::string, portctrl::frame_vector> branches;
/**
 * Movie starting RTC second.
 */
	int64_t movie_rtc_second;
/**
 * Movie starting RTC subsecond.
 */
	int64_t movie_rtc_subsecond;
/**
 * Start paused flag.
 */
	bool start_paused;
/**
 * Lazy project create flag.
 */
	bool lazy_project_create;
/**
 * Subtitles.
 */
	std::map<moviefile_subtiming, std::string> subtitles;
/**
 * Dynamic state.
 */
	dynamic_state dyn;
/**
 * Get number of frames in movie.
 *
 * returns: Number of frames.
 */
	uint64_t get_frame_count() throw();
/**
 * Get length of the movie
 *
 * returns: Length of the movie in milliseconds.
 */
	uint64_t get_movie_length() throw();
/**
 * Return reference to memory slot.
 */
	static moviefile*& memref(const std::string& slot);

/**
 * Copy data.
 */
	void copy_fields(const moviefile& mv);

/**
 * Create a default branch.
 */
	void create_default_branch(portctrl::type_set& ports);
/**
 * Get name of current branch.
 */
	const std::string& current_branch();
/**
 * Fork a branch.
 */
	void fork_branch(const std::string& oldname, const std::string& newname);
/**
 * Fixup input pointer post-copy.
 */
	void fixup_current_branch(const moviefile& mv);
/**
 * Clear the dynamic state to power-on defaults.
 */
	void clear_dynstate();
/**
 * Get the filename of the movie.
 */
	std::string get_filename() { return filename; }
private:
	moviefile(const moviefile&);
	moviefile& operator=(const moviefile&);
	void binary_io(int stream, rrdata_set& rrd, bool as_state);
	void binary_io(int stream, struct core_type& romtype);
	void save(zip::writer& w, rrdata_set& rrd, bool as_state);
	void load(zip::reader& r, core_type& romtype);
	memtracker::autorelease tracker;
	std::string filename;
};

void emerg_save_movie(const moviefile& mv, rrdata_set& rrd);

#endif
