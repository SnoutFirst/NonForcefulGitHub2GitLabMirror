#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "githubrepository.h"
#include "processrunner.h"
#include "syncresult.h"

namespace nfg2glm {

/// Owns the persistent bare repositories used as synchronization workspaces.
///
/// Every ref update performed here is an ordinary, non-forced push. Destination refs are
/// never deleted, reset or overwritten; only the application's private source-tracking
/// namespace below refs/nonforceful-github/ is ever removed.
class GitWorkspace {
 public:
  GitWorkspace(const ProcessRunner& runner, std::string gitExecutable,
               std::string ghExecutable, std::string glabExecutable);

  /// Locate or create the bare repository for @p repository and verify its identity.
  LocalRepositoryResult prepare(const GitHubRepository& repository,
                                const std::filesystem::path& repositoryPath,
                                const std::string& destinationUrl) const;

  /// Fetch every branch and tag of the GitHub source into the source-tracking namespace.
  FetchResult fetchAll(const GitHubRepository& repository,
                       const std::filesystem::path& repositoryPath) const;

  /// Push one branch or tag without force, and classify the outcome.
  RefPushResult push(const std::filesystem::path& repositoryPath, RefKind kind,
                     const std::string& name) const;

  /// Destination refs that have no corresponding source ref. They are reported, never removed.
  std::vector<std::string> staleDestinationRefs(const std::filesystem::path& repositoryPath,
                                                const FetchResult& fetch) const;

 private:
  struct RemoteRef {
    std::string objectId;
    std::string name;
  };

  struct RemoteRefList {
    bool ok = false;
    std::vector<RemoteRef> refs;
    std::string error;
  };

  RemoteRefList listRemoteRefs(const std::filesystem::path& repositoryPath,
                               const std::string& remote, bool destination) const;

  /// Arguments that let the matching CLI act as Git's credential helper for one command.
  std::vector<std::string> credentialArguments(bool destination) const;
  std::string configGet(const std::filesystem::path& repositoryPath,
                        const std::string& key) const;
  bool configSet(const std::filesystem::path& repositoryPath, const std::string& key,
                 const std::string& value) const;
  bool ensureRemote(const std::filesystem::path& repositoryPath, const std::string& remote,
                    const std::string& url, std::vector<std::string>& repairs) const;

  const ProcessRunner& runner_;
  std::string gitExecutable_;
  std::string ghExecutable_;
  std::string glabExecutable_;
};

}  // namespace nfg2glm
