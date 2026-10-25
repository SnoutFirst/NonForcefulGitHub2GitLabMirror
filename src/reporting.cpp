#include "reporting.h"

#include <string>

namespace nfg2glm {
namespace {

void printRefs(std::ostream& stream, const std::vector<RefPushResult>& refs) {
  for (const RefPushResult& ref : refs) {
    stream << "    " << describe(ref.kind) << ' ' << ref.name << ": " << describe(ref.state);
    if (!ref.detail.empty()) {
      stream << " [" << ref.detail << ']';
    }
    stream << '\n';
  }
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

  if (result.fetch.state == FetchState::succeeded) {
    stream << "  fetch: " << result.fetch.branches.size() << " branch(es), "
           << result.fetch.tags.size() << " tag(s)";
    if (result.fetch.prunedTrackingRefs != 0) {
      stream << ", " << result.fetch.prunedTrackingRefs
             << " stale source-tracking ref(s) removed";
    }
    stream << '\n';
  } else if (!result.fetch.error.empty()) {
    stream << "  fetch: failed [" << result.fetch.error << "]\n";
  }

  printRefs(stream, result.branches);
  printRefs(stream, result.tags);

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

}  // namespace nfg2glm
