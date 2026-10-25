#include "textutil.h"

#include <algorithm>
#include <cctype>

namespace nfg2glm {
namespace {

bool isSpace(char character) {
  return std::isspace(static_cast<unsigned char>(character)) != 0;
}

char lowerAscii(char character) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

}  // namespace

std::string trim(const std::string& text) {
  const auto first = std::find_if_not(text.begin(), text.end(), isSpace);
  const auto last = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
  if (first >= last) {
    return std::string();
  }
  return std::string(first, last);
}

std::string toLowerAscii(const std::string& text) {
  std::string lowered(text);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), lowerAscii);
  return lowered;
}

bool equalsIgnoreCase(const std::string& left, const std::string& right) {
  return toLowerAscii(left) == toLowerAscii(right);
}

bool startsWith(const std::string& text, std::string_view prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& text, char delimiter) {
  std::vector<std::string> fields;
  std::string current;
  for (const char character : text) {
    if (character == delimiter) {
      fields.push_back(current);
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  fields.push_back(current);
  return fields;
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::string current;
  for (const char character : text) {
    if (character == '\n') {
      if (!current.empty() && current.back() == '\r') {
        current.pop_back();
      }
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  if (!current.empty()) {
    if (current.back() == '\r') {
      current.pop_back();
    }
    lines.push_back(current);
  }
  return lines;
}

std::string join(const std::vector<std::string>& parts, std::string_view separator) {
  std::string joined;
  for (std::size_t index = 0; index < parts.size(); ++index) {
    if (index != 0) {
      joined.append(separator);
    }
    joined.append(parts[index]);
  }
  return joined;
}

std::string replaceAll(const std::string& text, std::string_view from, std::string_view to) {
  if (from.empty()) {
    return text;
  }
  std::string replaced;
  std::size_t position = 0;
  while (position < text.size()) {
    const std::size_t found = text.find(from, position);
    if (found == std::string::npos) {
      replaced.append(text, position, std::string::npos);
      break;
    }
    replaced.append(text, position, found - position);
    replaced.append(to);
    position = found + from.size();
  }
  return replaced;
}

}  // namespace nfg2glm
