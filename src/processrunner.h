#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace nfg2glm {

/// Result of running one child process.
struct ProcessResult {
  bool launched = false;            ///< True when the child process was actually started.
  int exitCode = -1;                ///< Exit status of the child (negative when signal-killed).
  bool terminatedBySignal = false;  ///< True when the child did not exit normally.
  std::string standardOutput;
  std::string standardError;
  std::string launchError;  ///< Diagnostic for the "could not start" case.

  bool succeeded() const { return launched && exitCode == 0; }

  /// Best available diagnostic text, preferring stderr, then stdout, then the launch error.
  std::string diagnostic() const;
};

/// Synchronous process launcher built on Boost.Process, with stdout/stderr capture.
///
/// Each argument is passed separately: no shell is ever involved, so repository names,
/// URLs and branch names can never be re-interpreted by a command interpreter.
class ProcessRunner {
 public:
  ProcessRunner() = default;

  /// Resolve @p name through PATH. Returns an empty path when the program is not present.
  std::filesystem::path findExecutable(const std::string& name) const;

  /// Run @p executable with @p arguments and capture both output streams.
  ///
  /// Diagnostics are drained concurrently with the child's execution, so a child that
  /// writes more than a pipe buffer cannot deadlock.
  ProcessResult run(const std::string& executable,
                    const std::vector<std::string>& arguments,
                    const std::filesystem::path& workingDirectory = {}) const;
};

}  // namespace nfg2glm
