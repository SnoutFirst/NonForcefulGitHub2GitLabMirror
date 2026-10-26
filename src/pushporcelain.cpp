#include "pushporcelain.h"

#include <cstddef>
#include <utility>

#include "textutil.h"

namespace nfg2glm {
namespace {

/// "Done" and "To <url>" are the frame around the per-ref lines; neither is a ref status.
bool isFrameLine(const std::string& line, PushPorcelainParse& parse) {
  if (line == "Done") {
    parse.sawTerminator = true;
    return true;
  }
  if (startsWith(line, "To ")) {
    parse.sawDestinationHeader = true;
    return true;
  }
  return false;
}

bool isStatusFlag(char flag) {
  switch (flag) {
    case ' ':
    case '+':
    case '-':
    case '*':
    case '!':
    case '=':
      return true;
    default:
      return false;
  }
}

/// Split "<summary> (<reason>)" and remove the brackets Git puts around the summary.
void splitSummary(const std::string& text, PushPorcelainEntry& entry) {
  entry.summary = text;
  if (text.size() >= 2 && text.back() == ')') {
    const std::size_t open = text.rfind(" (");
    if (open != std::string::npos) {
      entry.summary = text.substr(0, open);
      entry.reason = text.substr(open + 2, text.size() - open - 3);
    }
  }
  if (entry.summary.size() >= 2 && entry.summary.front() == '[' && entry.summary.back() == ']') {
    entry.summary = entry.summary.substr(1, entry.summary.size() - 2);
  }
}

}  // namespace

PushPorcelainParse parsePushPorcelain(const std::string& standardOutput) {
  PushPorcelainParse parse;

  // Lines are deliberately not trimmed: the flag may itself be a space.
  for (const std::string& line : splitLines(standardOutput)) {
    if (line.empty()) {
      continue;
    }
    if (isFrameLine(line, parse)) {
      continue;
    }
    if (line.size() < 2 || line[1] != '\t') {
      parse.malformedLines.push_back(line);
      continue;
    }

    const char flag = line[0];
    const std::vector<std::string> fields = split(line.substr(2), '\t');
    if (fields.empty() || fields[0].empty()) {
      parse.malformedLines.push_back(line);
      continue;
    }
    const std::size_t colon = fields[0].find(':');
    if (colon == std::string::npos) {
      parse.malformedLines.push_back(line);
      continue;
    }

    PushPorcelainEntry entry;
    entry.flag = flag;
    entry.from = fields[0].substr(0, colon);
    entry.to = fields[0].substr(colon + 1);
    if (entry.to.empty()) {
      parse.malformedLines.push_back(line);
      continue;
    }
    splitSummary(fields.size() > 1 ? fields[1] : std::string(), entry);

    if (!isStatusFlag(flag)) {
      parse.unexpectedStatuses.push_back(line);
    }
    parse.entries.push_back(std::move(entry));
  }

  return parse;
}

}  // namespace nfg2glm
