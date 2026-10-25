#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nfg2glm {

/// Remove leading and trailing whitespace.
std::string trim(const std::string& text);

/// ASCII lower-case copy (repository names are compared case-insensitively).
std::string toLowerAscii(const std::string& text);

/// Case-insensitive ASCII comparison.
bool equalsIgnoreCase(const std::string& left, const std::string& right);

/// True when @p text starts with @p prefix (case sensitive).
bool startsWith(const std::string& text, std::string_view prefix);

/// True when @p text ends with @p suffix (case sensitive).
bool endsWith(const std::string& text, std::string_view suffix);

/// Split on a delimiter, keeping empty fields.
std::vector<std::string> split(const std::string& text, char delimiter);

/// Split into lines, dropping a trailing carriage return from each line.
std::vector<std::string> splitLines(const std::string& text);

/// Join with a separator.
std::string join(const std::vector<std::string>& parts, std::string_view separator);

/// Replace every occurrence of @p from with @p to.
std::string replaceAll(const std::string& text, std::string_view from, std::string_view to);

}  // namespace nfg2glm
