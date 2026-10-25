#pragma once

#include <string>

#include "githubrepository.h"
#include "processrunner.h"
#include "syncresult.h"

namespace nfg2glm {

/// Looks up and creates GitLab projects in the authenticated user's personal namespace.
class GitLabProjects {
 public:
  GitLabProjects(const ProcessRunner& runner, std::string glabExecutable);

  /// Verify that `glab` is authenticated and report the account's namespace path.
  bool verifyAuthentication(std::string& namespacePath, std::string& error) const;

  /// Destination namespace used for every project (the authenticated user's namespace).
  void setNamespace(std::string namespacePath) { namespace_ = std::move(namespacePath); }
  const std::string& namespacePath() const { return namespace_; }

  /// Required GitLab visibility for a GitHub repository.
  static std::string requiredVisibility(const GitHubRepository& repository);

  /// Deterministic GitLab path (URL slug) for a repository name.
  static std::string projectSlug(const std::string& name);

  /// Resolve the project for @p repository, creating it when it does not exist yet.
  ProjectResult ensureProject(const GitHubRepository& repository) const;

 private:
  struct Lookup {
    bool found = false;
    ProjectResult project;
    std::string error;
    ErrorCategory category = ErrorCategory::none;
  };

  Lookup lookUp(const std::string& pathWithNamespace) const;
  ProjectResult create(const GitHubRepository& repository, const std::string& slug,
                       const std::string& visibility) const;

  /// Switch CI/CD off on a project created by this run (`PUT /projects/:id` with
  /// `builds_access_level=disabled`). Called immediately after creation and never for a project
  /// that already existed. Returns false and fills @p error when GitLab did not confirm the
  /// change; the mirror itself continues either way.
  bool disableCiCd(const std::string& pathWithNamespace, std::string& error) const;

  const ProcessRunner& runner_;
  std::string glabExecutable_;
  std::string namespace_;
};

}  // namespace nfg2glm
