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
  failed,                  ///< Any other failure (permissions, transport, server error).
  unreported               ///< Git printed no result for this ref, so its outcome is unknown.
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

/// Outcome of one bulk push command that carried every branch (or every tag) at once.
///
/// The operation-level fields are deliberately separate from the per-ref states: a push
/// process that exits non-zero because Git rejected one ref still reports the refs it did
/// update, and those keep their successful state.
struct BulkPushResult {
  /// One entry per requested ref, in the order the refs were requested.
  std::vector<RefPushResult> refs;
  /// Number of Git push processes this operation needed (0 or 1).
  std::size_t commands = 0;
  /// Exit status of the push process, captured separately from its output (-1 when the
  /// process could not be started). A non-zero status does not mean every ref failed.
  int exitCode = -1;
  /// Operation-level diagnostic when the command itself failed or its output was not
  /// trustworthy. Empty when the command ran and every ref line was parsed.
  std::string operationError;
  /// Wall-clock cost of the push command, in milliseconds.
  double durationMs = 0.0;
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

/// Outcome of one ref update attempted by a fetch.
enum class FetchRefState {
  created,                ///< The source-tracking ref did not exist and was created.
  updated,                ///< The source-tracking ref followed an ordinary fast-forward.
  upToDate,               ///< The source-tracking ref already pointed at GitHub's object.
  refusedNonFastForward,  ///< GitHub rewrote the branch; the source-tracking ref was kept.
  refusedTagMoved,        ///< GitHub moved the tag; the local tag was never re-fetched.
  retained,               ///< GitHub no longer has the ref; the source-tracking ref was kept.
  failed                  ///< The ref could not be fetched (transport, permissions, ...).
};

std::string describe(FetchRefState state);

struct FetchRefResult {
  std::string name;
  RefKind kind = RefKind::branch;
  FetchRefState state = FetchRefState::failed;
  std::string detail;

  /// True when the ref was deliberately left alone instead of being updated.
  bool refused() const {
    return state == FetchRefState::refusedNonFastForward || state == FetchRefState::refusedTagMoved;
  }
};

/// Result of fetching one repository.
///
/// A refused non-fast-forward update or a moved tag is a *success* of this phase, not a failure:
/// the point of the non-forced fetch is that the existing source-tracking ref, which is also the
/// backup of the previous GitHub history, survives. It is reported per ref so that the operator
/// can see which upstream refs were rewritten.
enum class FetchState {
  succeeded,  ///< Every ref was fetched, or deliberately left as it was.
  partial,    ///< A fetch command completed but at least one ref could not be fetched.
  failed      ///< The fetch as a whole failed; the destination was not touched.
};

struct FetchResult {
  FetchState state = FetchState::failed;
  /// Source refs that exist locally after the fetch and are therefore pushed to GitLab.
  std::vector<std::string> branches;
  std::vector<std::string> tags;
  /// Per-ref outcomes, in listing order: one entry per GitHub branch and tag.
  std::vector<FetchRefResult> refs;
  /// Number of `git fetch` processes this repository needed (one for branches, one per batch of
  /// new tags).
  std::size_t commands = 0;
  std::string error;
  ErrorCategory category = ErrorCategory::none;

  bool ok() const { return state != FetchState::failed; }

  std::size_t countRefs(FetchRefState wanted) const;
  /// Refs whose update was refused because GitHub rewrote or moved them.
  std::size_t refusedRefs() const;
  /// Refs that GitHub no longer has and that were kept as backup data.
  std::size_t retainedRefs() const;

  /// True when at least one ref could not be fetched at all.
  bool hasRefFailures() const;
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
  synchronized,           ///< Every requested ref is in place, or is deliberately left as it was
                          ///< because GitHub rewrote it (a refusal, not a failure).
  completedWithFailures,  ///< Processing finished but at least one ref or step failed.
  skipped,                ///< Deliberately not touched (collision, foreign local repository).
  failed                  ///< The repository could not be processed at all.
};

std::string describe(RepositoryState state);

/// Wall-clock cost of one repository's synchronization phases, in milliseconds.
struct PhaseTimings {
  double localRepositoryMs = 0.0;
  double fetchMs = 0.0;
  double destinationListingMs = 0.0;
  double branchPushMs = 0.0;
  double tagPushMs = 0.0;
};

struct RepositoryResult {
  GitHubRepository repository;
  RepositoryState state = RepositoryState::failed;
  ProjectResult project;
  LocalRepositoryResult localRepository;
  FetchResult fetch;
  BulkPushResult branches;
  BulkPushResult tags;
  std::vector<std::string> staleDestinationRefs;
  std::vector<std::string> notes;
  PhaseTimings timings;
  std::string error;
  ErrorCategory category = ErrorCategory::none;

  bool hasRefFailures() const;
  bool hasPushErrors() const;
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
  double discoveryMs = 0.0;
  double reportGenerationMs = 0.0;
  double elapsedMs = 0.0;

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
  /// Upstream refs whose rewrite or tag move was refused, so the existing ref was kept.
  std::size_t sourceRefsRefused() const;
  /// Upstream refs that disappeared but whose local and GitLab copies were kept.
  std::size_t sourceRefsRetained() const;
  /// `git fetch` processes started over the whole run.
  std::size_t fetchCommands() const;
  /// Newly created projects whose CI/CD was switched off (`builds_access_level=disabled`).
  std::size_t createdProjectsWithCiCdDisabled() const;

  bool successful() const;
};

}  // namespace nfg2glm
