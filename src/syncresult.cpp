#include "syncresult.h"

namespace nfg2glm {

std::string describe(ErrorCategory category) {
  switch (category) {
    case ErrorCategory::none:
      return "none";
    case ErrorCategory::authentication:
      return "authentication or authorization failure";
    case ErrorCategory::discovery:
      return "GitHub repository discovery failure";
    case ErrorCategory::gitlabProject:
      return "GitLab project lookup or creation failure";
    case ErrorCategory::localRepository:
      return "local repository validation failure";
    case ErrorCategory::filesystem:
      return "local filesystem failure";
    case ErrorCategory::gitCommand:
      return "Git command failure";
    case ErrorCategory::collision:
      return "GitLab project name collision";
    case ErrorCategory::other:
      return "other failure";
  }
  return "other failure";
}

std::string describe(RefKind kind) {
  return kind == RefKind::branch ? "branch" : "tag";
}

std::string describe(RefPushState state) {
  switch (state) {
    case RefPushState::pushed:
      return "pushed";
    case RefPushState::alreadyUpToDate:
      return "already up to date";
    case RefPushState::rejectedNonFastForward:
      return "rejected (non-fast-forward)";
    case RefPushState::rejectedTagConflict:
      return "rejected (conflicting tag)";
    case RefPushState::failed:
      return "failed";
  }
  return "failed";
}

std::string describe(LocalRepositoryState state) {
  switch (state) {
    case LocalRepositoryState::created:
      return "created";
    case LocalRepositoryState::reused:
      return "reused";
    case LocalRepositoryState::invalid:
      return "invalid";
  }
  return "invalid";
}

std::string describe(ProjectOutcome outcome) {
  switch (outcome) {
    case ProjectOutcome::created:
      return "created";
    case ProjectOutcome::existing:
      return "existing";
    case ProjectOutcome::failed:
      return "failed";
  }
  return "failed";
}

std::string describe(RepositoryState state) {
  switch (state) {
    case RepositoryState::synchronized:
      return "synchronized";
    case RepositoryState::completedWithFailures:
      return "completed with failures";
    case RepositoryState::skipped:
      return "skipped";
    case RepositoryState::failed:
      return "failed";
  }
  return "failed";
}

bool RepositoryResult::hasRefFailures() const {
  return branchesFailed() != 0 || tagsFailed() != 0;
}

namespace {

std::size_t countState(const std::vector<RefPushResult>& refs, RefPushState state) {
  std::size_t count = 0;
  for (const RefPushResult& ref : refs) {
    if (ref.state == state) {
      ++count;
    }
  }
  return count;
}

std::size_t countSucceeded(const std::vector<RefPushResult>& refs) {
  std::size_t count = 0;
  for (const RefPushResult& ref : refs) {
    if (ref.succeeded()) {
      ++count;
    }
  }
  return count;
}

}  // namespace

std::size_t RepositoryResult::branchesSucceeded() const { return countSucceeded(branches); }

std::size_t RepositoryResult::branchesFailed() const { return branches.size() - countSucceeded(branches); }

std::size_t RepositoryResult::tagsSucceeded() const { return countSucceeded(tags); }

std::size_t RepositoryResult::tagsFailed() const { return tags.size() - countSucceeded(tags); }

std::size_t RunResult::synchronizedRepositories() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    if (repository.state == RepositoryState::synchronized) {
      ++count;
    }
  }
  return count;
}

std::size_t RunResult::repositoriesWithFailures() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    if (repository.state == RepositoryState::failed ||
        repository.state == RepositoryState::completedWithFailures) {
      ++count;
    }
  }
  return count;
}

std::size_t RunResult::skippedRepositories() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    if (repository.state == RepositoryState::skipped) {
      ++count;
    }
  }
  return count;
}

std::size_t RunResult::projectsCreated() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    if (repository.project.outcome == ProjectOutcome::created) {
      ++count;
    }
  }
  return count;
}

std::size_t RunResult::branchesSucceeded() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    count += repository.branchesSucceeded();
  }
  return count;
}

std::size_t RunResult::branchesFailed() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    count += repository.branchesFailed();
  }
  return count;
}

std::size_t RunResult::branchesUpToDate() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    count += countState(repository.branches, RefPushState::alreadyUpToDate);
  }
  return count;
}

std::size_t RunResult::tagsSucceeded() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    count += repository.tagsSucceeded();
  }
  return count;
}

std::size_t RunResult::tagsFailed() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    count += repository.tagsFailed();
  }
  return count;
}

std::size_t RunResult::tagsUpToDate() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    count += countState(repository.tags, RefPushState::alreadyUpToDate);
  }
  return count;
}

std::size_t RunResult::collisions() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    if (repository.category == ErrorCategory::collision) {
      ++count;
    }
  }
  return count;
}

bool RunResult::successful() const {
  return discoveryComplete && repositoriesWithFailures() == 0 && skippedRepositories() == 0 &&
         collisions() == 0;
}

}  // namespace nfg2glm
