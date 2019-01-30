#ifndef _library__messagebuffer__hpp__included__
#define _library__messagebuffer__hpp__included__

#include <stdexcept>
#include <map>
#include <set>
#include <cstdint>
#include <string>

class messagebuffer
{
public:
/**
 * Update handler.
 */
	class update_handler
	{
	public:
/**
 * Destructor.
 */
		virtual ~update_handler() throw();
/**
 * Handle update.
 */
		virtual void messagebuffer_update() = 0;
	};
/**
 * Create new message buffer with specified maximum message count.
 *
 * Parameter maxmessages: The maximum number of messages.
 * Parameter windowsize: The initial window size.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::logic_error: Windowsize is greater than maxmessages or maxmessages is zero.
 */
	messagebuffer(size_t maxmessages, size_t windowsize);

/**
 * Add a new message to the buffer.
 *
 * Parameter msg: The new message to add.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Thrown through from update handler.
 */
	void add_message(const std::string& msg);

/**
 * Read a message.
 *
 * Parameter msgnum: Number of message to read.
 * Returns: The read message.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::logic_error: Invalid message number.
 */
	const std::string& get_message(size_t msgnum);

/**
 * Get the number of first message present.
 *
 * Returns: The number of first message present.
 */
	size_t get_msg_first() throw();

/**
 * Get the number of messages present.
 *
 * Returns: The number of messages present.
 */
	size_t get_msg_count() throw();

/**
 * Get the number of first message visible.
 *
 * Returns: The number of first message visible.
 */
	size_t get_visible_first() throw();

/**
 * Get the number of messages visible.
 *
 * Returns: The number of messages visible.
 */
	size_t get_visible_count() throw();

/**
 * Is there more messages after the current window?
 *
 * Returns: True if there is, false if not.
 */
	bool is_more_messages() throw();

/**
 * Freeze scrolling
 */
	void freeze_scrolling() throw();

/**
 * Unfreeze scrolling
 */
	void unfreeze_scrolling() throw();

/**
 * Freeze updates
 */
	void freeze_updates() throw();

/**
 * Unfreeze updates
 *
 * Returns: True if update is needed.
 */
	bool unfreeze_updates() throw();

/**
 * Scroll to beginning.
 *
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Thrown through from update handler.
 */
	void scroll_beginning();

/**
 * Scroll up one page.
 *
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Thrown through from update handler.
 */
	void scroll_up_page();

/**
 * Scroll up one line.
 *
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Thrown through from update handler.
 */
	void scroll_up_line();

/**
 * Scroll down one line.
 *
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Thrown through from update handler.
 */
	void scroll_down_line();

/**
 * Scroll down one page.
 *
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Thrown through from update handler.
 */
	void scroll_down_page();

/**
 * Scroll to beginning.
 *
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Thrown through from update handler.
 */
	void scroll_end();

/**
 * Register an update handler.
 *
 * Parameter handler: The new handler.
 * Throws std::bad_alloc: Not enough memory.
 */
	void register_handler(update_handler& handler);

/**
 * Unregister an update handler.
 *
 * Parameter handler: The handler to remove.
 * Throws std::bad_alloc: Not enough memory.
 */
	void unregister_handler(update_handler& handler) throw();

/**
 * Change the window size.
 *
 * Parameter windowsize: The new window size.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::logic_error: Windowsize is greater than maxmessages or maxmessages is zero.
 */
	void set_max_window_size(size_t windowsize);

/**
 * Read the window size.
 */
	size_t get_max_window_size() throw();
/**
 * Read the last message.
 */
	std::string get_last_message();
private:
	void send_notifications();
	std::map<uint64_t, std::string> messages_buf;
	uint64_t first_present_message;
	uint64_t next_message_number;
	uint64_t window_start;
	size_t max_messages;
	size_t window_size;
	bool scroll_frozen;
	bool updates_frozen;
	uint64_t window_start_at_freeze;
	uint64_t window_size_at_freeze;
	uint64_t next_message_number_at_freeze;
	std::set<update_handler*> handlers;
};

#endif
