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
    case ErrorCategory::upstreamRewrite:
      return "GitHub history rewritten";
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
    case RefPushState::unreported:
      return "unknown (git reported no result)";
  }
  return "failed";
}

std::string describe(FetchRefState state) {
  switch (state) {
    case FetchRefState::created:
      return "created";
    case FetchRefState::updated:
      return "updated";
    case FetchRefState::upToDate:
      return "already up to date";
    case FetchRefState::refusedNonFastForward:
      return "update refused (non-fast-forward)";
    case FetchRefState::refusedTagMoved:
      return "update refused (upstream tag moved)";
    case FetchRefState::retained:
      return "kept (GitHub no longer has it)";
    case FetchRefState::failed:
      return "failed";
  }
  return "failed";
}

std::size_t FetchResult::countRefs(FetchRefState wanted) const {
  std::size_t count = 0;
  for (const FetchRefResult& ref : refs) {
    if (ref.state == wanted) {
      ++count;
    }
  }
  return count;
}

std::size_t FetchResult::refusedRefs() const {
  return countRefs(FetchRefState::refusedNonFastForward) + countRefs(FetchRefState::refusedTagMoved);
}

std::size_t FetchResult::retainedRefs() const { return countRefs(FetchRefState::retained); }

bool FetchResult::hasRefFailures() const { return countRefs(FetchRefState::failed) != 0; }

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
    case RepositoryState::diverged:
      return "UPSTREAM DIVERGED (nothing forced)";
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

bool RepositoryResult::hasPushErrors() const {
  return !branches.operationError.empty() || !tags.operationError.empty();
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

std::size_t RepositoryResult::branchesSucceeded() const { return countSucceeded(branches.refs); }

std::size_t RepositoryResult::branchesFailed() const {
  return branches.refs.size() - countSucceeded(branches.refs);
}

std::size_t RepositoryResult::tagsSucceeded() const { return countSucceeded(tags.refs); }

std::size_t RepositoryResult::tagsFailed() const { return tags.refs.size() - countSucceeded(tags.refs); }

std::size_t RunResult::synchronizedRepositories() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    if (repository.state == RepositoryState::synchronized) {
      ++count;
    }
  }
  return count;
}

std::size_t RunResult::divergedRepositories() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    if (repository.state == RepositoryState::diverged) {
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

std::size_t RunResult::createdProjectsWithCiCdDisabled() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    if (repository.project.outcome == ProjectOutcome::created &&
        repository.project.ciCdDisabled) {
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
    count += countState(repository.branches.refs, RefPushState::alreadyUpToDate);
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
    count += countState(repository.tags.refs, RefPushState::alreadyUpToDate);
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

std::size_t RunResult::sourceRefsRefused() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    count += repository.fetch.refusedRefs();
  }
  return count;
}

std::size_t RunResult::sourceRefsRetained() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    count += repository.fetch.retainedRefs();
  }
  return count;
}

std::size_t RunResult::fetchCommands() const {
  std::size_t count = 0;
  for (const RepositoryResult& repository : repositories) {
    count += repository.fetch.commands;
  }
  return count;
}

bool RunResult::successful() const {
  return discoveryComplete && repositoriesWithFailures() == 0 && skippedRepositories() == 0 &&
         divergedRepositories() == 0 && collisions() == 0;
}

}  // namespace nfg2glm
