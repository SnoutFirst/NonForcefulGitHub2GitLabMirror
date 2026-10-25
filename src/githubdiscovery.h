#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "githubrepository.h"
#include "processrunner.h"
#include "syncresult.h"

namespace nfg2glm {

struct DiscoveryResult {
  bool ok = false;
  bool complete = false;
  std::vector<GitHubRepository> repositories;
  std::size_t duplicateEntries = 0;
  std::size_t malformedEntries = 0;
  std::string error;
  ErrorCategory category = ErrorCategory::none;
};

/// Enumerates every GitHub repository the authenticated account can reach.
class GitHubDiscovery {
 public:
  GitHubDiscovery(const ProcessRunner& runner, std::string ghExecutable);

  /// Verify that `gh` is authenticated and can enumerate repositories.
  bool verifyAuthentication(std::string& account, std::string& error) const;

  /// Page through /user/repos with every affiliation so that owned, collaborator and
  /// organisation repositories are all included.
  DiscoveryResult discover() const;

 private:
  const ProcessRunner& runner_;
  std::string ghExecutable_;
};

}  // namespace nfg2glm
