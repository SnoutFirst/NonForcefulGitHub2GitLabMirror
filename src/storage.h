#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "githubrepository.h"

namespace nfg2glm {

/// Locates and creates the per-user application-data directory tree.
class Storage {
 public:
  /// PlatformFolders-based data directory for this application:
  /// e.g. $XDG_DATA_HOME/NonForcefulGitHub2GitLabMirror on Linux,
  /// ~/Library/Application Support/... on macOS, %LOCALAPPDATA%\\... on Windows.
  static std::filesystem::path defaultRoot();

  explicit Storage(std::filesystem::path root);

  const std::filesystem::path& root() const { return root_; }
  const std::filesystem::path& repositoryDirectory() const { return repositories_; }

  /// Create the directory tree, throwing std::runtime_error with a diagnostic on failure.
  void ensureExists();

  /// Deterministic, collision-resistant path of the bare repository for @p repository.
  ///
  /// The name is derived from the canonical GitHub identity and carries a hash of the
  /// case-sensitive full name, so two repositories whose names differ only by case or by
  /// characters that are not portable in file names can never share a workspace.
  std::filesystem::path repositoryPath(const GitHubRepository& repository) const;

  /// Replace everything outside [A-Za-z0-9._-] with '-'.
  static std::string sanitizeComponent(const std::string& text);

  /// FNV-1a 64 bit hash, rendered as 16 lower-case hexadecimal characters.
  static std::string stableHashHex(const std::string& text);

 private:
  std::filesystem::path root_;
  std::filesystem::path repositories_;
};

}  // namespace nfg2glm
