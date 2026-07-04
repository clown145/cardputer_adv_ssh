#include "util/string_utils.h"

namespace adv {

std::string truncate_for_display(const std::string& value, size_t max_chars)
{
    if (value.size() <= max_chars) {
        return value;
    }
    if (max_chars <= 3) {
        return value.substr(0, max_chars);
    }
    return value.substr(0, max_chars - 3) + "...";
}

}  // namespace adv
