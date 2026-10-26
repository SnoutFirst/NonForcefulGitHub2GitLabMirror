#include "reporting.h"

#include <iomanip>
#include <sstream>
#include <string>

namespace nfg2glm {
namespace {

void printRefs(std::ostream& stream, const std::vector<FetchRefResult>& refs) {
  for (const FetchRefResult& ref : refs) {
    // Only the refs an operator has to know about: refused, kept, or failed.
    const bool interesting = ref.refused() || ref.state == FetchRefState::retained ||
                             ref.state == FetchRefState::failed;
    if (!interesting) {
      continue;
    }
    stream << "    source " << describe(ref.kind) << ' ' << ref.name << ": "
           << describe(ref.state);
    if (!ref.detail.empty()) {
      stream << " [" << ref.detail << ']';
    }
    stream << '\n';
  }
}

void printRefs(std::ostream& stream, const std::vector<RefPushResult>& refs) {
  for (const RefPushResult& ref : refs) {
    stream << "    " << describe(ref.kind) << ' ' << ref.name << ": " << describe(ref.state);
    if (!ref.detail.empty()) {
      stream << " [" << ref.detail << ']';
    }
    stream << '\n';
  }
}

/// A bulk push command that failed as a whole is reported next to the per-ref lines, never
/// instead of them: the refs Git did update keep their successful state.
void printPushError(std::ostream& stream, RefKind kind, const std::string& error) {
  if (error.empty()) {
    return;
  }
  stream << "  " << describe(kind) << " push failed [" << error << "]\n";
}

std::string milliseconds(double value) {
  std::ostringstream formatted;
  formatted << std::fixed << std::setprecision(1) << value;
  return formatted.str();
}

double totalMilliseconds(const RunResult& result, double PhaseTimings::*member) {
  double total = 0.0;
  for (const RepositoryResult& repository : result.repositories) {
    total += repository.timings.*member;
  }
  return total;
}

std::size_t countFetchAttempts(const RunResult& result) {
  std::size_t count = 0;
  for (const RepositoryResult& repository : result.repositories) {
    if (repository.fetch.ok() || !repository.fetch.error.empty()) {
      ++count;
    }
  }
  return count;
}

std::size_t countDestinationListings(const RunResult& result) {
  std::size_t count = 0;
  for (const RepositoryResult& repository : result.repositories) {
    if (repository.fetch.ok()) {
      ++count;
    }
  }
  return count;
}

std::size_t countLocalRepositorySetups(const RunResult& result) {
  std::size_t count = 0;
  for (const RepositoryResult& repository : result.repositories) {
    if (!repository.localRepository.path.empty()) {
      ++count;
    }
  }
  return count;
}

std::size_t countPushCommands(const RunResult& result, RefKind kind) {
  std::size_t count = 0;
  for (const RepositoryResult& repository : result.repositories) {
    count += kind == RefKind::branch ? repository.branches.commands : repository.tags.commands;
  }
  return count;
}

std::size_t countPushCommands(const RunResult& result) {
  return countPushCommands(result, RefKind::branch) + countPushCommands(result, RefKind::tag);
}

std::size_t countPushedRefs(const RunResult& result, RefKind kind) {
  std::size_t count = 0;
  for (const RepositoryResult& repository : result.repositories) {
    count += (kind == RefKind::branch ? repository.branches.refs.size()
                                      : repository.tags.refs.size());
  }
  return count;
}

}  // namespace

void printRepositoryReport(std::ostream& stream, const RepositoryResult& result) {
  stream << result.repository.fullName << " [" << describe(result.state) << "]\n";

  if (result.project.outcome != ProjectOutcome::failed) {
    stream << "  project: " << describe(result.project.outcome) << ' '
           << result.project.pathWithNamespace;
    if (!result.project.visibility.empty()) {
      stream << " (" << result.project.visibility << ')';
    }
    if (!result.project.webUrl.empty()) {
      stream << " " << result.project.webUrl;
    }
    stream << '\n';

    if (result.project.outcome == ProjectOutcome::created) {
      stream << "  CI/CD: " << (result.project.ciCdDisabled ? "disabled" : "not disabled");
      if (!result.project.ciCdDisabled && !result.project.ciCdError.empty()) {
        stream << " [" << result.project.ciCdError << ']';
      }
      stream << '\n';
    }
  }

  if (result.localRepository.state != LocalRepositoryState::invalid) {
    stream << "  local repository: " << describe(result.localRepository.state) << ' '
           << result.localRepository.path.string() << '\n';
    for (const std::string& repair : result.localRepository.repairs) {
      stream << "    repaired: " << repair << '\n';
    }
  }

  if (result.fetch.state != FetchState::failed) {
    stream << "  fetch: " << result.fetch.branches.size() << " branch(es), "
           << result.fetch.tags.size() << " tag(s)";
    if (!result.fetch.refs.empty()) {
      stream << " (" << result.fetch.countRefs(FetchRefState::created) << " created, "
             << result.fetch.countRefs(FetchRefState::updated) << " updated, "
             << result.fetch.countRefs(FetchRefState::upToDate) << " unchanged";
      if (result.fetch.refusedRefs() != 0) {
        stream << ", " << result.fetch.refusedRefs() << " refused";
      }
      if (result.fetch.retainedRefs() != 0) {
        stream << ", " << result.fetch.retainedRefs() << " kept without a GitHub ref";
      }
      if (result.fetch.hasRefFailures()) {
        stream << ", " << result.fetch.countRefs(FetchRefState::failed) << " failed";
      }
      stream << ')';
    }
    stream << '\n';
    printRefs(stream, result.fetch.refs);
  } else if (!result.fetch.error.empty()) {
    stream << "  fetch: failed [" << result.fetch.error << "]\n";
  }

  printPushError(stream, RefKind::branch, result.branches.operationError);
  printRefs(stream, result.branches.refs);
  printPushError(stream, RefKind::tag, result.tags.operationError);
  printRefs(stream, result.tags.refs);

  if (!result.staleDestinationRefs.empty()) {
    stream << "  stale destination refs (left untouched): ";
    for (std::size_t index = 0; index < result.staleDestinationRefs.size(); ++index) {
      if (index != 0) {
        stream << ", ";
      }
      stream << result.staleDestinationRefs[index];
    }
    stream << '\n';
  }

  for (const std::string& note : result.notes) {
    stream << "  note: " << note << '\n';
  }

  if (!result.error.empty()) {
    stream << "  error: " << result.error;
    if (result.category != ErrorCategory::none) {
      stream << " (" << describe(result.category) << ')';
    }
    stream << '\n';
  }
}

void printRunReport(std::ostream& stream, const RunResult& result) {
  stream << "\nSynchronization report\n=====================\n";
  stream << "Local data directory: " << result.dataDirectory.string() << '\n';
  if (!result.discoveryComplete) {
    stream << "Repository discovery: INCOMPLETE";
    if (!result.discoveryError.empty()) {
      stream << " (" << result.discoveryError << ')';
    }
    stream << '\n';
  }
  stream << "Repositories discovered: " << result.discoveredRepositories() << '\n';
  stream << "Repositories synchronized: " << result.synchronizedRepositories() << '\n';
  stream << "Repositories with failures: " << result.repositoriesWithFailures() << '\n';
  stream << "Repositories skipped: " << result.skippedRepositories() << '\n';
  stream << "GitLab projects created: " << result.projectsCreated() << '\n';
  stream << "New projects with CI/CD disabled: " << result.createdProjectsWithCiCdDisabled()
         << '\n';
  stream << "Branches pushed successfully: " << result.branchesSucceeded() << " (already up to date: "
         << result.branchesUpToDate() << ")\n";
  stream << "Branch conflicts/failures: " << result.branchesFailed() << '\n';
  stream << "Tags pushed successfully: " << result.tagsSucceeded() << " (already up to date: "
         << result.tagsUpToDate() << ")\n";
  stream << "Tag conflicts/failures: " << result.tagsFailed() << '\n';
  stream << "Git push commands: " << countPushCommands(result) << " (branches: "
         << countPushCommands(result, RefKind::branch) << ", tags: "
         << countPushCommands(result, RefKind::tag) << ")\n";
  stream << "Source updates refused (existing refs kept): " << result.sourceRefsRefused() << '\n';
  stream << "Source refs GitHub no longer has (kept as backup): " << result.sourceRefsRetained()
         << '\n';
  stream << "Name collisions: " << result.collisions() << '\n';

  stream << "\nRepository details\n------------------\n";
  if (result.repositories.empty()) {
    stream << "(no repositories were discovered)\n";
    return;
  }
  for (const RepositoryResult& repository : result.repositories) {
    printRepositoryReport(stream, repository);
    stream << '\n';
  }
}

void printRunTimings(std::ostream& stream, const RunResult& result) {
  stream << "\nPhase timings\n-------------\n";
  stream << "Repository discovery:            " << milliseconds(result.discoveryMs) << " ms\n";
  stream << "Local repository setup:          "
         << milliseconds(totalMilliseconds(result, &PhaseTimings::localRepositoryMs)) << " ms ("
         << countLocalRepositorySetups(result) << " workspace(s))\n";
  stream << "Source fetch:                    "
         << milliseconds(totalMilliseconds(result, &PhaseTimings::fetchMs)) << " ms ("
         << countFetchAttempts(result) << " repository fetch(es), "
         << result.fetchCommands() << " git fetch command(s))\n";
  stream << "Destination ref listing:         "
         << milliseconds(totalMilliseconds(result, &PhaseTimings::destinationListingMs))
         << " ms (" << countDestinationListings(result) << " listing(s))\n";
  stream << "Bulk branch push:                "
         << milliseconds(totalMilliseconds(result, &PhaseTimings::branchPushMs)) << " ms ("
         << countPushCommands(result, RefKind::branch) << " command(s) for "
         << countPushedRefs(result, RefKind::branch) << " branch(es))\n";
  stream << "Bulk tag push:                   "
         << milliseconds(totalMilliseconds(result, &PhaseTimings::tagPushMs)) << " ms ("
         << countPushCommands(result, RefKind::tag) << " command(s) for "
         << countPushedRefs(result, RefKind::tag) << " tag(s))\n";
  stream << "Report generation:               " << milliseconds(result.reportGenerationMs)
         << " ms\n";
  stream << "Total before report generation:  " << milliseconds(result.elapsedMs) << " ms\n";
}

}  // namespace nfg2glm
