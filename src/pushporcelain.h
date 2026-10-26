#pragma once

#include <string>
#include <vector>

namespace nfg2glm {

/// One per-ref status line of `git push --porcelain`.
///
/// Git documents the machine-readable format of this mode as, for every ref it handled,
///
///     <flag> \t <from>:<to> \t <summary> (<reason>)
///
/// where <flag> is
///
///   ' '  a fast-forward update               '+'  a forced update
///   '-'  a deletion                          '*'  a newly created ref
///   '!'  a rejected update or a failure      '='  the ref was already up to date
///
/// and <from> may be empty for a deletion. Everything that is not a ref status line is
/// either the leading "To <url>" header or the trailing "Done" marker, both of which the
/// parser recognises and skips.
struct PushPorcelainEntry {
  char flag = '\0';     ///< The documented status flag; never one of the frame markers.
  std::string from;     ///< The source ref as Git printed it (empty for a deletion).
  std::string to;       ///< The destination ref as Git printed it.
  std::string summary;  ///< Status text with the surrounding brackets removed ("new branch").
  std::string reason;   ///< The text of the trailing parentheses, when Git supplied one.
};

/// Result of parsing the complete standard output of one `git push --porcelain` run.
///
/// Nothing is ever invented: a line that does not match the documented format is kept
/// verbatim in @c malformedLines, and a status flag outside the documented set is recorded
/// in @c unexpectedStatuses as well as in the entry itself with its own flag preserved.
struct PushPorcelainParse {
  std::vector<PushPorcelainEntry> entries;
  std::vector<std::string> malformedLines;
  std::vector<std::string> unexpectedStatuses;
  bool sawDestinationHeader = false;  ///< The "To <url>" line was seen.
  bool sawTerminator = false;         ///< The final "Done" line was seen.

  /// True when Git printed the closing marker, i.e. the output is not truncated.
  bool complete() const { return sawTerminator; }
};

/// Parse the standard output of `git push --porcelain`.
///
/// Only documented, machine-readable output is interpreted; human-readable progress or
/// localised error text is never parsed for ref outcomes.
PushPorcelainParse parsePushPorcelain(const std::string& standardOutput);

}  // namespace nfg2glm
