#include "fetchstatus.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "textutil.h"

namespace nfg2glm {
namespace {

/// The marker column of Git's status table. A plain fast-forward update prints no marker at all.
bool isMarker(char character) {
  switch (character) {
    case '*':
    case '+':
    case '-':
    case 'x':
    case '!':
      return true;
    default:
      return false;
  }
}

/// Lines that carry no ref information: the source header, notices and progress.
bool isNoiseLine(const std::string& line) {
  return startsWith(line, "From ") || startsWith(line, "remote:") ||
         startsWith(line, "Receiving ") || startsWith(line, "Resolving ") ||
         startsWith(line, "Enumerating ") || startsWith(line, "Counting ") ||
         startsWith(line, "Total ") || startsWith(line, "warning:") ||
         startsWith(line, "Auto packing ") || line == "Already up to date.";
}

/// Split a trailing "(reason)" annotation off the text that follows the destination ref.
std::string trailingReason(const std::string& text) {
  if (text.size() < 2 || text.front() != '(' || text.back() != ')') {
    return {};
  }
  return trim(text.substr(1, text.size() - 2));
}

}  // namespace

std::string FetchStatusParse::reasonFor(const std::string& destination) const {
  for (const FetchStatusEntry& entry : entries) {
    if (entry.destination == destination && !entry.reason.empty()) {
      return entry.reason;
    }
  }
  return {};
}

bool FetchStatusParse::rejected(const std::string& destination) const {
  for (const FetchStatusEntry& entry : entries) {
    if (entry.destination == destination && entry.flag == '!') {
      return true;
    }
  }
  return false;
}

FetchStatusParse parseFetchStatus(const std::string& standardError) {
  FetchStatusParse parse;

  for (const std::string& raw : splitLines(standardError)) {
    // Progress is written with carriage returns and may share a line with a status line.
    const std::size_t carriageReturn = raw.find_last_of('\r');
    const std::string line =
        trim(carriageReturn == std::string::npos ? raw : raw.substr(carriageReturn));
    if (line.empty()) {
      continue;
    }
    if (startsWith(line, "fatal:")) {
      parse.fatalLines.push_back(line);
      continue;
    }
    if (isNoiseLine(line)) {
      continue;
    }

    FetchStatusEntry entry;
    entry.line = line;
    std::string body = line;

    const std::size_t markerPosition = line.find_first_not_of(' ');
    if (markerPosition == std::string::npos) {
      continue;
    }
    if (isMarker(line[markerPosition])) {
      entry.flag = line[markerPosition];
      body[markerPosition] = ' ';
    } else if (line.find("..") != std::string::npos) {
      // An unmarked line is a fast-forward update, printed as "<old>..<new> <source> -> <dst>".
      entry.flag = ' ';
    } else {
      continue;
    }

    const std::size_t arrow = body.rfind(" -> ");
    if (arrow != std::string::npos) {
      const std::string right = trim(body.substr(arrow + 4));
      body.erase(arrow);
      const std::size_t separator = right.find(' ');
      entry.destination = right.substr(0, separator);
      if (entry.destination.empty()) {
        parse.malformedLines.push_back(line);
        continue;
      }
      entry.reason = trailingReason(separator == std::string::npos
                                        ? std::string()
                                        : trim(right.substr(separator)));
    }

    const std::size_t open = body.find('[');
    const std::size_t close = open == std::string::npos ? std::string::npos : body.find(']', open);
    if (open != std::string::npos && close != std::string::npos) {
      entry.label = body.substr(open, close - open + 1);
      body.erase(open, close - open + 1);
    }

    for (const std::string& token : split(body, ' ')) {
      if (token.empty() || token.find("..") != std::string::npos || token.front() == '(') {
        continue;
      }
      entry.source = token;
    }

    // Every marked line of the table carries a label ("[new branch]", "[rejected]", ...).
    if (entry.flag != ' ' && entry.label.empty()) {
      parse.malformedLines.push_back(line);
      continue;
    }
    parse.entries.push_back(std::move(entry));
  }

  return parse;
}

}  // namespace nfg2glm
