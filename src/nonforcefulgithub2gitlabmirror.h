#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "githubdiscovery.h"
#include "gitlabproject.h"
#include "gitworkspace.h"
#include "processrunner.h"
#include "storage.h"
#include "syncresult.h"

namespace nfg2glm {

/// Application entry point object: verifies prerequisites, discovers GitHub repositories and
/// mirrors every branch and tag into the authenticated user's GitLab namespace.
class NonForcefulGitHub2GitLabMirror {
 public:
  /// @param verbose List every ref of every repository in the final report, including the ones
  ///                that were already up to date. Off by default, where those are only counted.
  explicit NonForcefulGitHub2GitLabMirror(bool verbose = false) : verbose_(verbose) {}

  /// Run the synchronization and return the process exit status.
  int run();

 private:
  bool prepareStorage();
  bool verifyPrerequisites();
  void synchronize(const std::vector<GitHubRepository>& repositories);
  RepositoryResult synchronizeRepository(const GitHubRepository& repository);

  ProcessRunner runner_;
  std::unique_ptr<Storage> storage_;
  std::unique_ptr<GitHubDiscovery> discovery_;
  std::unique_ptr<GitLabProjects> projects_;
  std::unique_ptr<GitWorkspace> workspace_;
  std::map<std::string, std::string> projectPathOwners_;
  bool verbose_ = false;
  RunResult result_;
};

}  // namespace nfg2glm
