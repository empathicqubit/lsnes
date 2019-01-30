#ifndef _library__fileimage_patch__hpp__included__
#define _library__fileimage_patch__hpp__included__

#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

namespace fileimage
{
std::vector<char> patch(const std::vector<char>& original, const std::vector<char>& patch,
	int32_t offset);

/**
 * ROM patcher.
 */
struct patcher
{
/**
 * Constructor.
 */
	patcher();
/**
 * Destructor.
 */
	virtual ~patcher() throw();
/**
 * Identify patch.
 *
 * Parameter patch: The patch.
 * Returns: True if my format, false if not.
 */
	virtual bool identify(const std::vector<char>& patch) throw() = 0;
/**
 * Do the patch.
 */
	virtual void dopatch(std::vector<char>& out, const std::vector<char>& original,
		const std::vector<char>& patch, int32_t offset) = 0;
};
}

#endif
