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
/// Every ref update performed here is ordinary and non-forced, in both directions: the fetch into
/// refs/nonforceful-github/ refuses a rewritten upstream branch or a moved upstream tag, and the
/// push to GitLab is rejected by Git when the destination diverged. No ref is ever deleted here,
/// with or without a corresponding upstream ref.
class GitWorkspace {
 public:
  GitWorkspace(const ProcessRunner& runner, std::string gitExecutable,
               std::string ghExecutable, std::string glabExecutable);

  /// Locate or create the bare repository for @p repository and verify its identity.
  LocalRepositoryResult prepare(const GitHubRepository& repository,
                                const std::filesystem::path& repositoryPath,
                                const std::string& destinationUrl) const;

  /// Fetch the GitHub branches and the not-yet-known GitHub tags into the source-tracking
  /// namespace without ever forcing a local ref.
  ///
  /// Branches are fetched with one non-forced wildcard refspec, so a rewritten branch is refused
  /// and keeps its previous local value. Tags are fetched one explicit refspec at a time and only
  /// while the local tag does not exist, because Git accepts a fast-forward tag move even without
  /// a forced refspec. A source ref that disappeared on GitHub is reported and kept.
  FetchResult fetchAll(const GitHubRepository& repository,
                       const std::filesystem::path& repositoryPath) const;

  /// Push every branch or every tag of the source-tracking namespace in a single Git process.
  ///
  /// One wildcard refspec maps the whole namespace, so the cost does not grow with the number
  /// of refs. The refspec is passed as one literal argument; Git expands the wildcard itself.
  /// Branches and tags are pushed separately so their results can be classified independently.
  BulkPushResult pushAll(const std::filesystem::path& repositoryPath, RefKind kind,
                         const std::vector<std::string>& names) const;

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

  struct TrackingRef {
    std::string objectId;
    std::string name;
  };

  struct TrackingRefList {
    bool ok = false;
    std::vector<TrackingRef> refs;
    std::string error;
  };

  RemoteRefList listRemoteRefs(const std::filesystem::path& repositoryPath,
                               const std::string& remote, bool destination) const;

  /// Every ref below refs/nonforceful-github/, read before and after each fetch.
  TrackingRefList listTrackingRefs(const std::filesystem::path& repositoryPath) const;

  /// Remove the forced refspecs that `git remote add` writes for both remotes.
  bool clearConfiguredFetchRefspecs(const std::filesystem::path& repositoryPath,
                                    std::vector<std::string>& repairs) const;
  bool clearConfiguredFetchRefspec(const std::filesystem::path& repositoryPath,
                                   const std::string& remote,
                                   std::vector<std::string>& repairs) const;

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
