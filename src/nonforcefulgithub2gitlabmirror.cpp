#include "nonforcefulgithub2gitlabmirror.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <sstream>

#include "reporting.h"
#include "textutil.h"

namespace nfg2glm {
namespace {

double elapsedMilliseconds(const std::chrono::steady_clock::time_point& started) {
  const std::chrono::duration<double, std::milli> elapsed =
      std::chrono::steady_clock::now() - started;
  return elapsed.count();
}

}  // namespace

int NonForcefulGitHub2GitLabMirror::run() {
  const auto runStarted = std::chrono::steady_clock::now();

  // Rendering the report is measured separately, so the report can state its own cost.
  const auto emitReport = [this, &runStarted]() {
    result_.elapsedMs = elapsedMilliseconds(runStarted);
    const auto reportStarted = std::chrono::steady_clock::now();
    std::ostringstream body;
    printRunReport(body, result_);
    result_.reportGenerationMs = elapsedMilliseconds(reportStarted);
    std::cout << body.str();
    printRunTimings(std::cout, result_);
  };

  std::cout << "NonForcefulGitHub2GitLabMirror\n"
            << "One-way GitHub to GitLab mirror. Destination refs are never forced, "
               "overwritten or deleted.\n\n";

  if (!prepareStorage()) {
    return 1;
  }
  std::cout << "Local data directory: " << result_.dataDirectory.string() << "\n\n";

  if (!verifyPrerequisites()) {
    std::cout << "\nAborting before synchronization: the required tooling or authentication "
                 "is not usable.\n";
    return 1;
  }

  std::cout << "\nDiscovering GitHub repositories...\n";
  const auto discoveryStarted = std::chrono::steady_clock::now();
  const DiscoveryResult discovery = discovery_->discover();
  result_.discoveryMs = elapsedMilliseconds(discoveryStarted);
  if (!discovery.ok) {
    result_.discoveryComplete = false;
    result_.discoveryError = discovery.error;
    std::cout << "Repository discovery failed: " << discovery.error << "\n";
    std::cout << "No repository was synchronized.\n";
    emitReport();
    return 1;
  }
  if (!discovery.complete) {
    result_.discoveryComplete = false;
    result_.discoveryError = discovery.error;
    std::cout << "Warning: repository discovery is incomplete: " << discovery.error << "\n";
  }
  std::cout << "Discovered " << discovery.repositories.size() << " repositories"
            << " (" << discovery.duplicateEntries << " duplicate entries ignored, "
            << discovery.malformedEntries << " unusable entries skipped).\n\n";

  synchronize(discovery.repositories);
  emitReport();

  if (result_.sourceRefsRefused() != 0) {
    // Deliberately not "SUCCESS": the only reason this tool exists is to notice that GitHub
    // rewrote history, so a refusal must reach the operator through the exit status as well.
    std::cout << "\nResult: GITHUB HISTORY WAS REWRITTEN (" << result_.sourceRefsRefused()
              << " ref(s) in " << result_.divergedRepositories()
              << " repository(ies)) -- nothing was forced, see \"GitHub history was rewritten\" "
                 "above\n";
    if (result_.repositoriesWithFailures() != 0 || result_.skippedRepositories() != 0 ||
        result_.collisions() != 0 || !result_.discoveryComplete) {
      std::cout << "The run reported other problems as well; see the report above.\n";
    }
    return 1;
  }
  if (!result_.successful()) {
    std::cout << "\nResult: FAILED (see the report above)\n";
    return 1;
  }
  std::cout << "\nResult: SUCCESS\n";
  return 0;
}

bool NonForcefulGitHub2GitLabMirror::prepareStorage() {
  try {
    storage_ = std::make_unique<Storage>(Storage::defaultRoot());
    storage_->ensureExists();
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    storage_.reset();
    return false;
  }
  result_.dataDirectory = storage_->root();
  return true;
}

bool NonForcefulGitHub2GitLabMirror::verifyPrerequisites() {
  const std::filesystem::path git = runner_.findExecutable("git");
  const std::filesystem::path gh = runner_.findExecutable("gh");
  const std::filesystem::path glab = runner_.findExecutable("glab");

  const bool missing = git.empty() || gh.empty() || glab.empty();
  if (git.empty()) {
    std::cout << "error: the 'git' executable was not found on PATH\n";
  }
  if (gh.empty()) {
    std::cout << "error: the 'gh' (GitHub CLI) executable was not found on PATH\n";
  }
  if (glab.empty()) {
    std::cout << "error: the 'glab' (GitLab CLI) executable was not found on PATH\n";
  }
  if (missing) {
    return false;
  }

  std::cout << "git:  " << git.string() << "\n"
            << "gh:   " << gh.string() << "\n"
            << "glab: " << glab.string() << "\n";

  discovery_ = std::make_unique<GitHubDiscovery>(runner_, gh.string());
  std::string account;
  std::string error;
  if (!discovery_->verifyAuthentication(account, error)) {
    std::cout << "error: " << error << "\n";
    return false;
  }
  std::cout << "Authenticated GitHub account: " << account << "\n";

  projects_ = std::make_unique<GitLabProjects>(runner_, glab.string());
  std::string namespacePath;
  if (!projects_->verifyAuthentication(namespacePath, error)) {
    std::cout << "error: " << error << "\n";
    return false;
  }
  projects_->setNamespace(namespacePath);
  std::cout << "Authenticated GitLab namespace: " << namespacePath << "\n";

  workspace_ = std::make_unique<GitWorkspace>(runner_, git.string(), gh.string(),
                                                            glab.string());
  return true;
}

void NonForcefulGitHub2GitLabMirror::synchronize(
    const std::vector<GitHubRepository>& repositories) {
  std::size_t index = 0;
  for (const GitHubRepository& repository : repositories) {
    ++index;
    std::cout << '[' << index << '/' << repositories.size() << "] " << repository.fullName
              << " (" << repository.visibility << ")\n";

    RepositoryResult result = synchronizeRepository(repository);
    std::cout << "    -> " << describe(result.state) << ": " << result.branchesSucceeded() << '/'
              << result.branches.refs.size() << " branch(es), " << result.tagsSucceeded() << '/'
              << result.tags.refs.size() << " tag(s)";
    if (!result.error.empty()) {
      std::cout << " -- " << result.error;
    }
    std::cout << "\n";
    result_.repositories.push_back(std::move(result));
  }
}

RepositoryResult NonForcefulGitHub2GitLabMirror::synchronizeRepository(
    const GitHubRepository& repository) {
  RepositoryResult result;
  result.repository = repository;

  try {
    const std::string slug = GitLabProjects::projectSlug(repository.name);
    const std::string pathKey = toLowerAscii(slug);
    const auto claimed = projectPathOwners_.find(pathKey);
    if (claimed != projectPathOwners_.end() &&
        !equalsIgnoreCase(claimed->second, repository.fullName)) {
      result.state = RepositoryState::skipped;
      result.category = ErrorCategory::collision;
      result.error = "the GitLab project path '" + slug + "' already mirrors " + claimed->second +
                     "; this repository was skipped instead of merging the two";
      return result;
    }

    result.project = projects_->ensureProject(repository);
    if (!result.project.ok()) {
      result.state = RepositoryState::failed;
      result.category = result.project.category;
      result.error = result.project.error;
      return result;
    }
    projectPathOwners_[pathKey] = repository.fullName;
    if (!result.project.visibilityMatchesRequirement) {
      result.notes.push_back(result.project.error);
    }

    const std::filesystem::path repositoryPath = storage_->repositoryPath(repository);
    auto phaseStarted = std::chrono::steady_clock::now();
    result.localRepository =
        workspace_->prepare(repository, repositoryPath, result.project.httpUrlToRepo);
    result.timings.localRepositoryMs = elapsedMilliseconds(phaseStarted);
    if (!result.localRepository.ok()) {
      result.state = RepositoryState::skipped;
      result.category = result.localRepository.category;
      result.error = result.localRepository.error;
      return result;
    }

    phaseStarted = std::chrono::steady_clock::now();
    result.fetch = workspace_->fetchAll(repository, repositoryPath);
    result.timings.fetchMs = elapsedMilliseconds(phaseStarted);
    if (!result.fetch.ok()) {
      result.state = RepositoryState::failed;
      result.category = result.fetch.category;
      result.error = result.fetch.error;
      return result;
    }

    // One push process for all branches, one for all tags: the wildcard refspec maps the whole
    // source-tracking namespace, so the number of refs does not multiply the number of commands.
    result.branches = workspace_->pushAll(repositoryPath, RefKind::branch, result.fetch.branches);
    result.timings.branchPushMs = result.branches.durationMs;

    result.tags = workspace_->pushAll(repositoryPath, RefKind::tag, result.fetch.tags);
    result.timings.tagPushMs = result.tags.durationMs;

    phaseStarted = std::chrono::steady_clock::now();
    result.staleDestinationRefs = workspace_->staleDestinationRefs(repositoryPath, result.fetch);
    result.timings.destinationListingMs = elapsedMilliseconds(phaseStarted);

    const bool diverged = result.sourceDiverged();
    const bool failed = !result.project.visibilityMatchesRequirement ||
                        result.fetch.hasRefFailures() || result.hasRefFailures() ||
                        result.hasPushErrors();
    if (failed) {
      result.state = RepositoryState::completedWithFailures;
    } else if (diverged) {
      // Every ref that could be pushed is in place, but GitHub rewrote something. Say so, and let
      // the run fail: reporting a rewrite as if it were an ordinary success defeats the purpose of
      // the tool.
      result.state = RepositoryState::diverged;
    } else {
      result.state = RepositoryState::synchronized;
    }
    if (diverged) {
      const std::string count = std::to_string(result.fetch.refusedRefs());
      result.category = ErrorCategory::upstreamRewrite;
      result.error = "GitHub rewrote " + count +
                     " source ref(s); each one was left as it was and nothing was forced. The "
                     "GitLab ref and the local backup still hold the previous history -- resolve "
                     "the rewrite by hand (see the README)";
      if (failed) {
        result.notes.push_back("GitHub also rewrote " + count +
                               " source ref(s); those refs were kept as they were");
      }
    }
    if (failed && result.error.empty()) {
      if (result.fetch.hasRefFailures()) {
        result.category = ErrorCategory::gitCommand;
        result.error =
            "at least one GitHub ref could not be fetched; the refs that were available were "
            "still pushed, and the rejected ones were left as they were";
      } else if (result.hasRefFailures()) {
        result.category = ErrorCategory::gitCommand;
        result.error =
            "at least one destination ref was rejected or failed; it was left as GitLab had it";
      } else if (result.hasPushErrors()) {
        result.category = ErrorCategory::gitCommand;
        result.error = "a bulk git push command did not complete cleanly; only the refs listed "
                       "above are known to be in place";
      } else {
        result.category = ErrorCategory::gitlabProject;
        result.error = "the GitLab project exists with a visibility that differs from the "
                       "GitHub source; it was not changed";
      }
    }
    return result;
  } catch (const std::exception& error) {
    result.state = RepositoryState::failed;
    result.category = ErrorCategory::other;
    result.error = std::string("unexpected error while processing the repository: ") + error.what();
    return result;
  }
}

}  // namespace nfg2glm
