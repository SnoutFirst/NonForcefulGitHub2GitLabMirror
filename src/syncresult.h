#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "githubrepository.h"

namespace nfg2glm {

/// Broad classification of a failure, used by the final report.
enum class ErrorCategory {
  none,
  authentication,
  discovery,
  gitlabProject,
  localRepository,
  filesystem,
  gitCommand,
  collision,
  other
};

std::string describe(ErrorCategory category);

enum class RefKind { branch, tag };

std::string describe(RefKind kind);

/// Outcome of a single non-forced ref push.
enum class RefPushState {
  pushed,                  ///< The destination ref was created or fast-forwarded.
  alreadyUpToDate,         ///< The destination already pointed at the same object.
  rejectedNonFastForward,  ///< Git refused a divergent branch update.
  rejectedTagConflict,     ///< Git refused to move an existing tag to a different object.
  failed                   ///< Any other failure (permissions, transport, server error).
};

std::string describe(RefPushState state);

struct RefPushResult {
  std::string name;
  RefKind kind = RefKind::branch;
  RefPushState state = RefPushState::failed;
  std::string detail;

  bool succeeded() const {
    return state == RefPushState::pushed || state == RefPushState::alreadyUpToDate;
  }
};

enum class LocalRepositoryState { created, reused, invalid };

std::string describe(LocalRepositoryState state);

struct LocalRepositoryResult {
  LocalRepositoryState state = LocalRepositoryState::invalid;
  std::filesystem::path path;
  std::vector<std::string> repairs;  ///< Remote/configuration repairs applied in place.
  std::string error;
  ErrorCategory category = ErrorCategory::none;

  bool ok() const { return state != LocalRepositoryState::invalid; }
};

enum class FetchState { succeeded, failed };

struct FetchResult {
  FetchState state = FetchState::failed;
  std::vector<std::string> branches;
  std::vector<std::string> tags;
  std::size_t prunedTrackingRefs = 0;
  std::string error;
  ErrorCategory category = ErrorCategory::none;

  bool ok() const { return state == FetchState::succeeded; }
};

enum class ProjectOutcome { created, existing, failed };

std::string describe(ProjectOutcome outcome);

struct ProjectResult {
  ProjectOutcome outcome = ProjectOutcome::failed;
  std::string pathWithNamespace;
  std::string webUrl;
  std::string httpUrlToRepo;
  std::string visibility;
  bool visibilityMatchesRequirement = true;
  /// True when CI/CD was switched off on a project created by this run
  /// (`builds_access_level=disabled`). Existing projects are never modified.
  bool ciCdDisabled = false;
  /// Diagnostic when CI/CD could not be disabled on a newly created project.
  std::string ciCdError;
  std::string error;
  ErrorCategory category = ErrorCategory::none;

  bool ok() const { return outcome != ProjectOutcome::failed; }
};

enum class RepositoryState {
  synchronized,           ///< Every requested ref is in place.
  completedWithFailures,  ///< Processing finished but at least one ref or step failed.
  skipped,                ///< Deliberately not touched (collision, foreign local repository).
  failed                  ///< The repository could not be processed at all.
};

std::string describe(RepositoryState state);

struct RepositoryResult {
  GitHubRepository repository;
  RepositoryState state = RepositoryState::failed;
  ProjectResult project;
  LocalRepositoryResult localRepository;
  FetchResult fetch;
  std::vector<RefPushResult> branches;
  std::vector<RefPushResult> tags;
  std::vector<std::string> staleDestinationRefs;
  std::vector<std::string> notes;
  std::string error;
  ErrorCategory category = ErrorCategory::none;

  bool hasRefFailures() const;
  std::size_t branchesSucceeded() const;
  std::size_t branchesFailed() const;
  std::size_t tagsSucceeded() const;
  std::size_t tagsFailed() const;
};

struct RunResult {
  std::filesystem::path dataDirectory;
  bool discoveryComplete = true;
  std::string discoveryError;
  std::vector<RepositoryResult> repositories;

  std::size_t discoveredRepositories() const { return repositories.size(); }
  std::size_t synchronizedRepositories() const;
  std::size_t repositoriesWithFailures() const;
  std::size_t skippedRepositories() const;
  std::size_t projectsCreated() const;
  std::size_t branchesSucceeded() const;
  std::size_t branchesFailed() const;
  std::size_t branchesUpToDate() const;
  std::size_t tagsSucceeded() const;
  std::size_t tagsFailed() const;
  std::size_t tagsUpToDate() const;
  std::size_t collisions() const;
  /// Newly created projects whose CI/CD was switched off (`builds_access_level=disabled`).
  std::size_t createdProjectsWithCiCdDisabled() const;

  bool successful() const;
};

}  // namespace nfg2glm
