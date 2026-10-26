#pragma once

#include <string>
#include <vector>

namespace nfg2glm {

/// One per-ref status line of a human-readable `git fetch` run.
///
/// `git fetch` has no machine-readable per-ref mode that older Git versions understand, so its
/// ordinary status table is the only place that says *why* an update was refused. Git prints one
/// line per ref it has something to say about, in this layout:
///
///     <marker> <label> <source> -> <destination> (<reason>)
///
/// with
///
///   ' '  a fast-forward update, printed as "<old>..<new>"
///   '*'  "[new branch]" or "[new tag]"
///   '+'  a forced update, printed as "<old>...<new>"      (never expected: nothing is forced)
///   '-'  "[deleted]"                                      (never expected: nothing is pruned)
///   'x'  a pruned ref                                     (never expected: nothing is pruned)
///   '!'  "[rejected]", with the reason in parentheses
///
/// This parse is best-effort by nature, because those labels are human text. It is used only to
/// attach a reason to a ref whose outcome the object comparison has already established, and to
/// spot a fatal diagnostic. No ref outcome is ever decided from this text alone.
struct FetchStatusEntry {
  char flag = '\0';         ///< The marker column; ' ' for a plain fast-forward update.
  std::string label;        ///< The bracketed label ("[new branch]", "[rejected]", ...).
  std::string source;       ///< The remote ref as Git printed it (may be empty for a prune).
  std::string destination;  ///< The source-tracking ref it was written to, when Git printed one.
  std::string reason;       ///< The text of the trailing parentheses, when Git supplied one.
  std::string line;         ///< The whole line, trimmed.
};

/// Result of parsing the complete standard error of one `git fetch` run.
///
/// Nothing is invented: a line that looks like a ref status but does not parse is kept verbatim
/// in @c malformedLines, and every line Git prefixed with "fatal:" is kept in @c fatalLines.
struct FetchStatusParse {
  std::vector<FetchStatusEntry> entries;
  std::vector<std::string> fatalLines;
  std::vector<std::string> malformedLines;

  /// The reason Git printed for @p destination, or an empty string.
  std::string reasonFor(const std::string& destination) const;

  /// True when Git marked the update of @p destination as rejected.
  bool rejected(const std::string& destination) const;
};

/// Parse the standard error of `git fetch`.
FetchStatusParse parseFetchStatus(const std::string& standardError);

}  // namespace nfg2glm
