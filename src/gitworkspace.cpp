#include "gitworkspace.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>

#include "fetchstatus.h"
#include "pushporcelain.h"
#include "textutil.h"

namespace nfg2glm {
namespace {

constexpr const char* sourceRemote = "github";
constexpr const char* destinationRemote = "gitlab";
constexpr const char* trackingRoot = "refs/nonforceful-github/";
constexpr const char* trackingBranchPrefix = "refs/nonforceful-github/heads/";
constexpr const char* trackingTagPrefix = "refs/nonforceful-github/tags/";
constexpr const char* destinationBranchPrefix = "refs/heads/";
constexpr const char* destinationTagPrefix = "refs/tags/";
// The fetch refspec for branches carries no leading '+': the persistent bare repository is a
// backup of GitHub, so a rewritten upstream branch must be refused by Git instead of being taken
// over. Ordinary fast-forward updates are still accepted.
constexpr const char* branchRefspec = "refs/heads/*:refs/nonforceful-github/heads/*";
// Tags are handled differently on purpose. Git applies a wildcard refspec to every tag that
// already exists, and a tag that moved *forward* (to a descendant commit) is accepted as an
// ordinary fast-forward update even without a '+'. A tag wildcard would therefore silently
// replace an existing tag. Instead, each tag is fetched with its own explicit refspec, and only
// while the local tag does not exist yet; an existing tag is never fetched again.
constexpr std::size_t tagFetchBatchSize = 200;
// Push refspecs carry no leading '+': these destinations must never be forced.
constexpr const char* branchPushRefspec = "refs/nonforceful-github/heads/*:refs/heads/*";
constexpr const char* tagPushRefspec = "refs/nonforceful-github/tags/*:refs/tags/*";
constexpr const char* sourceRepositoryConfigKey = "nfg2glm.sourcerepository";
constexpr const char* gitlabProjectConfigKey = "nfg2glm.gitlabproject";

double elapsedMilliseconds(const std::chrono::steady_clock::time_point& started) {
  const std::chrono::duration<double, std::milli> elapsed =
      std::chrono::steady_clock::now() - started;
  return elapsed.count();
}

std::string trackingRef(RefKind kind, const std::string& name) {
  return (kind == RefKind::branch ? trackingBranchPrefix : trackingTagPrefix) + name;
}

std::string destinationRef(RefKind kind, const std::string& name) {
  return (kind == RefKind::branch ? destinationBranchPrefix : destinationTagPrefix) + name;
}

/// Reduce a remote URL to its trailing "owner/name" so that equivalent spellings compare equal.
std::string remoteRepositorySuffix(const std::string& url) {
  std::string cleaned = trim(url);
  while (!cleaned.empty() && cleaned.back() == '/') {
    cleaned.pop_back();
  }
  if (endsWith(toLowerAscii(cleaned), ".git")) {
    cleaned.erase(cleaned.size() - 4);
  }
  std::replace(cleaned.begin(), cleaned.end(), ':', '/');
  const std::size_t lastSlash = cleaned.find_last_of('/');
  if (lastSlash == std::string::npos) {
    return cleaned;
  }
  const std::size_t previousSlash = cleaned.find_last_of('/', lastSlash - 1);
  const std::size_t start = previousSlash == std::string::npos ? 0 : previousSlash + 1;
  return cleaned.substr(start);
}

bool remoteMatchesRepository(const std::string& url, const GitHubRepository& repository) {
  return equalsIgnoreCase(remoteRepositorySuffix(url), repository.fullName);
}

/// A Git argument list must never contain a destructive or forced option, and never a refspec that
/// asks Git to force or prune a ref. This covers both `git fetch` and `git push`.
void ensureNonDestructiveArguments(const std::string& operation,
                                   const std::vector<std::string>& arguments) {
  for (const std::string& argument : arguments) {
    const bool forbidden = argument == "-f" || argument == "--force" ||
                           argument == "--force-with-lease" ||
                           startsWith(argument, "--force-with-lease=") ||
                           startsWith(argument, "--force") || argument == "--mirror" ||
                           argument == "--delete" || argument == "--prune" ||
                           argument == "--prune-tags" || argument == "--tags" ||
                           argument == "--all" || argument == "--update-head-ok";
    if (forbidden) {
      throw std::logic_error("refusing to run a destructive " + operation + ": " + argument);
    }
    // A leading '+' in a refspec is the short spelling of --force.
    if (!argument.empty() && argument.front() == '+') {
      throw std::logic_error("refusing to run a forced " + operation + ": " + argument);
    }
  }
}

/// The refspec that brings one named upstream tag into the source-tracking namespace. It is only
/// ever handed to Git for a tag that is not present locally yet.
std::string tagFetchRefspec(const std::string& name) {
  return std::string("refs/tags/") + name + ":" + trackingTagPrefix + name;
}

/// True when a fetch was refused because the upstream branch was rewritten.
bool isNonFastForwardReason(const std::string& detail) {
  const std::string lowered = toLowerAscii(detail);
  return lowered.find("non-fast-forward") != std::string::npos ||
         lowered.find("fetch first") != std::string::npos ||
         lowered.find("non-fast forward") != std::string::npos;
}

RefPushState classifyRejection(const std::string& detail, RefKind kind) {
  if (isNonFastForwardReason(detail)) {
    return RefPushState::rejectedNonFastForward;
  }
  if (toLowerAscii(detail).find("already exists") != std::string::npos && kind == RefKind::tag) {
    return RefPushState::rejectedTagConflict;
  }
  return RefPushState::failed;
}

/// The human-readable summary of one porcelain entry, for the per-ref report line.
std::string pushDetail(const PushPorcelainEntry& entry) {
  std::string detail = entry.summary;
  if (!entry.reason.empty()) {
    detail += " (" + entry.reason + ")";
  }
  if (detail.empty()) {
    detail = "git reported no summary";
  }
  return detail;
}

}  // namespace

GitWorkspace::GitWorkspace(const ProcessRunner& runner, std::string gitExecutable,
                           std::string ghExecutable, std::string glabExecutable)
    : runner_(runner),
      gitExecutable_(std::move(gitExecutable)),
      ghExecutable_(std::move(ghExecutable)),
      glabExecutable_(std::move(glabExecutable)) {}

std::vector<std::string> GitWorkspace::credentialArguments(bool destination) const {
  const std::string& helper = destination ? glabExecutable_ : ghExecutable_;
  if (helper.empty()) {
    return {};
  }
  // Git treats a "!" helper as a command line; quote it so paths with spaces survive, and use
  // forward slashes so the same spelling works for the shell shipped with Git for Windows.
  const std::string quotedHelper = "\"" + replaceAll(helper, "\\", "/") + "\"";
  return {"-c", "credential.helper=", "-c",
          "credential.helper=!" + quotedHelper + " auth git-credential"};
}

std::string GitWorkspace::configGet(const std::filesystem::path& repositoryPath,
                                    const std::string& key) const {
  const ProcessResult result = runner_.run(gitExecutable_, {"config", "--get", key},
                                           repositoryPath);
  if (!result.succeeded()) {
    return {};
  }
  return trim(result.standardOutput);
}

bool GitWorkspace::configSet(const std::filesystem::path& repositoryPath, const std::string& key,
                             const std::string& value) const {
  return runner_.run(gitExecutable_, {"config", key, value}, repositoryPath).succeeded();
}

bool GitWorkspace::ensureRemote(const std::filesystem::path& repositoryPath,
                                const std::string& remote, const std::string& url,
                                std::vector<std::string>& repairs) const {
  const std::string existing = configGet(repositoryPath, "remote." + remote + ".url");
  if (existing.empty()) {
    const ProcessResult added =
        runner_.run(gitExecutable_, {"remote", "add", remote, url}, repositoryPath);
    if (!added.succeeded()) {
      return false;
    }
    repairs.push_back("added remote '" + remote + "'");
    return true;
  }
  if (existing == url) {
    return true;
  }
  const ProcessResult updated =
      runner_.run(gitExecutable_, {"remote", "set-url", remote, url}, repositoryPath);
  if (!updated.succeeded()) {
    return false;
  }
  repairs.push_back("updated remote '" + remote + "' to " + url);
  return true;
}

LocalRepositoryResult GitWorkspace::prepare(const GitHubRepository& repository,
                                            const std::filesystem::path& repositoryPath,
                                            const std::string& destinationUrl) const {
  LocalRepositoryResult result;
  result.path = repositoryPath;

  std::error_code error;
  const bool exists = std::filesystem::exists(repositoryPath, error);
  if (error) {
    result.error = "cannot inspect " + repositoryPath.string() + ": " + error.message();
    result.category = ErrorCategory::filesystem;
    return result;
  }

  if (!exists) {
    const std::filesystem::path parent = repositoryPath.parent_path();
    std::filesystem::create_directories(parent, error);
    if (error && !std::filesystem::is_directory(parent)) {
      result.error = "cannot create " + parent.string() + ": " + error.message();
      result.category = ErrorCategory::filesystem;
      return result;
    }

    const ProcessResult initialised = runner_.run(
        gitExecutable_, {"init", "--bare", "--quiet", repositoryPath.string()});
    if (!initialised.succeeded()) {
      result.error = "cannot create the bare repository " + repositoryPath.string() + ": " +
                     initialised.diagnostic();
      result.category = ErrorCategory::gitCommand;
      return result;
    }

    if (!ensureRemote(repositoryPath, sourceRemote, repository.cloneUrl, result.repairs) ||
        !ensureRemote(repositoryPath, destinationRemote, destinationUrl, result.repairs) ||
        !clearConfiguredFetchRefspecs(repositoryPath, result.repairs)) {
      result.error = "cannot configure the remotes of " + repositoryPath.string();
      result.category = ErrorCategory::gitCommand;
      return result;
    }
    if (!configSet(repositoryPath, sourceRepositoryConfigKey, repository.fullName) ||
        !configSet(repositoryPath, gitlabProjectConfigKey, destinationUrl)) {
      result.error = "cannot record the repository identity in " + repositoryPath.string();
      result.category = ErrorCategory::gitCommand;
      return result;
    }

    result.state = LocalRepositoryState::created;
    return result;
  }

  const ProcessResult bareCheck =
      runner_.run(gitExecutable_, {"rev-parse", "--is-bare-repository"}, repositoryPath);
  if (!bareCheck.succeeded() || trim(bareCheck.standardOutput) != "true") {
    result.error = repositoryPath.string() +
                   " exists but is not a valid bare Git repository; it was left untouched";
    result.category = ErrorCategory::localRepository;
    return result;
  }

  const std::string recorded = configGet(repositoryPath, sourceRepositoryConfigKey);
  const std::string originUrl = configGet(repositoryPath, "remote.github.url");
  if (!recorded.empty()) {
    if (!equalsIgnoreCase(recorded, repository.fullName)) {
      result.error = repositoryPath.string() + " is the workspace of " + recorded +
                     ", not " + repository.fullName + "; it was left untouched";
      result.category = ErrorCategory::localRepository;
      return result;
    }
  } else if (!originUrl.empty() && !remoteMatchesRepository(originUrl, repository)) {
    result.error = repositoryPath.string() + " has a github remote pointing at " + originUrl +
                   ", not " + repository.fullName + "; it was left untouched";
    result.category = ErrorCategory::localRepository;
    return result;
  }

  if (!ensureRemote(repositoryPath, sourceRemote, repository.cloneUrl, result.repairs) ||
      !ensureRemote(repositoryPath, destinationRemote, destinationUrl, result.repairs)) {
    result.error = "cannot repair the remotes of " + repositoryPath.string();
    result.category = ErrorCategory::gitCommand;
    return result;
  }
  if (!clearConfiguredFetchRefspecs(repositoryPath, result.repairs)) {
    result.error = "cannot remove the forced fetch refspecs of " + repositoryPath.string();
    result.category = ErrorCategory::gitCommand;
    return result;
  }
  if (!configSet(repositoryPath, sourceRepositoryConfigKey, repository.fullName) ||
      !configSet(repositoryPath, gitlabProjectConfigKey, destinationUrl)) {
    result.error = "cannot record the repository identity in " + repositoryPath.string();
    result.category = ErrorCategory::gitCommand;
    return result;
  }

  result.state = LocalRepositoryState::reused;
  return result;
}

GitWorkspace::RemoteRefList GitWorkspace::listRemoteRefs(
    const std::filesystem::path& repositoryPath, const std::string& remote,
    bool destination) const {
  RemoteRefList listing;
  std::vector<std::string> arguments = credentialArguments(destination);
  arguments.insert(arguments.end(), {"ls-remote", "--heads", "--tags", remote});
  const ProcessResult result = runner_.run(gitExecutable_, arguments, repositoryPath);
  if (!result.succeeded()) {
    listing.error = result.diagnostic();
    return listing;
  }

  for (const std::string& line : splitLines(result.standardOutput)) {
    if (line.empty()) {
      continue;
    }
    const std::size_t tab = line.find('\t');
    if (tab == std::string::npos) {
      continue;
    }
    RemoteRef ref{line.substr(0, tab), line.substr(tab + 1)};
    if (ref.name.empty() || endsWith(ref.name, "^{}")) {
      continue;
    }
    listing.refs.push_back(std::move(ref));
  }
  listing.ok = true;
  return listing;
}

GitWorkspace::TrackingRefList GitWorkspace::listTrackingRefs(
    const std::filesystem::path& repositoryPath) const {
  TrackingRefList listing;
  const ProcessResult listed =
      runner_.run(gitExecutable_,
                  {"for-each-ref", "--format=%(objectname) %(refname)", trackingRoot},
                  repositoryPath);
  if (!listed.succeeded()) {
    listing.error = listed.diagnostic();
    return listing;
  }

  for (const std::string& line : splitLines(listed.standardOutput)) {
    const std::string trimmed = trim(line);
    const std::size_t space = trimmed.find(' ');
    if (trimmed.empty() || space == std::string::npos) {
      continue;
    }
    TrackingRef ref{trimmed.substr(0, space), trim(trimmed.substr(space + 1))};
    if (!startsWith(ref.name, trackingRoot)) {
      continue;
    }
    listing.refs.push_back(std::move(ref));
  }
  listing.ok = true;
  return listing;
}

bool GitWorkspace::clearConfiguredFetchRefspecs(const std::filesystem::path& repositoryPath,
                                                std::vector<std::string>& repairs) const {
  return clearConfiguredFetchRefspec(repositoryPath, sourceRemote, repairs) &&
         clearConfiguredFetchRefspec(repositoryPath, destinationRemote, repairs);
}

bool GitWorkspace::clearConfiguredFetchRefspec(const std::filesystem::path& repositoryPath,
                                               const std::string& remote,
                                               std::vector<std::string>& repairs) const {
  const std::string key = "remote." + remote + ".fetch";
  if (configGet(repositoryPath, key).empty()) {
    return true;
  }
  // `git remote add` writes "+refs/heads/*:refs/remotes/<remote>/*", a *forced* refspec into a
  // namespace this program never reads. It is cleared on both remotes: clearing the source one
  // keeps every fetch free of forced updates, and clearing the destination one stops Git from
  // quietly rewriting refs/remotes/gitlab/* when a push targets a local repository.
  const ProcessResult cleared =
      runner_.run(gitExecutable_, {"config", "--unset-all", key}, repositoryPath);
  if (!cleared.succeeded()) {
    return configGet(repositoryPath, key).empty();
  }
  repairs.push_back("removed the default forced fetch refspec of remote '" + remote + "'");
  return true;
}

FetchResult GitWorkspace::fetchAll(const GitHubRepository& repository,
                                   const std::filesystem::path& repositoryPath) const {
  FetchResult result;
  (void)repository;

  const RemoteRefList remote = listRemoteRefs(repositoryPath, sourceRemote, false);
  if (!remote.ok) {
    result.error = "listing the GitHub refs failed: " + remote.error;
    result.category = ErrorCategory::gitCommand;
    return result;
  }

  std::map<std::string, std::string> remoteBranches;
  std::map<std::string, std::string> remoteTags;
  for (const RemoteRef& ref : remote.refs) {
    if (startsWith(ref.name, destinationBranchPrefix)) {
      remoteBranches.emplace(ref.name.substr(std::string(destinationBranchPrefix).size()),
                             ref.objectId);
    } else if (startsWith(ref.name, destinationTagPrefix)) {
      remoteTags.emplace(ref.name.substr(std::string(destinationTagPrefix).size()), ref.objectId);
    }
  }

  const TrackingRefList before = listTrackingRefs(repositoryPath);
  if (!before.ok) {
    result.error = "listing the source-tracking refs failed: " + before.error;
    result.category = ErrorCategory::gitCommand;
    return result;
  }
  const auto toMap = [](const TrackingRefList& list) {
    std::map<std::string, std::string> mapped;
    for (const TrackingRef& ref : list.refs) {
      mapped.emplace(ref.name, ref.objectId);
    }
    return mapped;
  };
  const std::map<std::string, std::string> trackingBefore = toMap(before);

  // One non-forced wildcard fetch for every branch. Git accepts ordinary fast-forward updates and
  // refuses a rewritten branch, which leaves the existing source-tracking ref untouched.
  std::vector<std::string> branchArguments = credentialArguments(false);
  branchArguments.insert(branchArguments.end(), {"fetch", "--no-tags", "--no-prune",
                                                 "--no-prune-tags", sourceRemote, branchRefspec});
  ensureNonDestructiveArguments("git fetch", branchArguments);
  const ProcessResult fetchedBranches = runner_.run(gitExecutable_, branchArguments, repositoryPath);
  ++result.commands;
  const FetchStatusParse branchStatus = parseFetchStatus(fetchedBranches.standardError);

  const TrackingRefList afterBranches = listTrackingRefs(repositoryPath);
  if (!afterBranches.ok) {
    result.error = "listing the source-tracking refs failed: " + afterBranches.error;
    result.category = ErrorCategory::gitCommand;
    return result;
  }
  std::map<std::string, std::string> tracking = toMap(afterBranches);

  std::size_t refusedBranches = 0;
  std::size_t failedBranches = 0;
  for (const auto& entry : remoteBranches) {
    const std::string& name = entry.first;
    const std::string& remoteObject = entry.second;
    const std::string trackingName = trackingRef(RefKind::branch, name);

    FetchRefResult ref;
    ref.name = name;
    ref.kind = RefKind::branch;

    const auto previous = trackingBefore.find(trackingName);
    const auto current = tracking.find(trackingName);
    const bool hadPrevious = previous != trackingBefore.end();

    if (current == tracking.end()) {
      ref.state = FetchRefState::failed;
      ref.detail = "the source-tracking ref is missing after the fetch";
    } else if (current->second == remoteObject) {
      if (!hadPrevious) {
        ref.state = FetchRefState::created;
      } else {
        ref.state = previous->second == remoteObject ? FetchRefState::upToDate
                                                     : FetchRefState::updated;
      }
    } else if (hadPrevious && previous->second == current->second) {
      // The local ref still holds the value it had before the fetch, so Git refused the update.
      const std::string reason = branchStatus.reasonFor(trackingName);
      if (reason.empty() || isNonFastForwardReason(reason)) {
        ref.state = FetchRefState::refusedNonFastForward;
        ref.detail = "GitHub rewrote the branch; the local tracking ref and the GitLab branch "
                     "were kept";
      } else {
        ref.state = FetchRefState::failed;
        ref.detail = reason;
      }
    } else {
      ref.state = FetchRefState::failed;
      ref.detail = "the source-tracking ref did not reach the value GitHub reports";
    }

    if (ref.state == FetchRefState::failed) {
      ++failedBranches;
    } else if (ref.state == FetchRefState::refusedNonFastForward) {
      ++refusedBranches;
    }
    result.refs.push_back(std::move(ref));
  }

  // A fetch that could not run at all, or that reported a fatal diagnostic, leaves nothing that can
  // be trusted: the repository is reported as failed instead of being pushed.
  const bool branchFetchFatal =
      !fetchedBranches.launched || !branchStatus.fatalLines.empty();
  if (branchFetchFatal || (!fetchedBranches.succeeded() && refusedBranches == 0 &&
                          failedBranches == 0)) {
    result.error = "fetching the GitHub branches failed: " + fetchedBranches.diagnostic();
    result.category = ErrorCategory::gitCommand;
    return result;
  }

  // Tags: an existing tag is never fetched again. Git treats a tag that moved forward as an
  // ordinary fast-forward update, so fetching it -- with or without a forced refspec -- would
  // replace the local tag. A moved tag is reported instead, and the local tag keeps its value.
  std::vector<std::string> newTags;
  for (const auto& entry : remoteTags) {
    const std::string& name = entry.first;
    const std::string& remoteObject = entry.second;
    const std::string trackingName = trackingRef(RefKind::tag, name);

    const auto current = tracking.find(trackingName);
    if (current == tracking.end()) {
      newTags.push_back(name);
      continue;
    }

    FetchRefResult ref;
    ref.name = name;
    ref.kind = RefKind::tag;
    if (current->second == remoteObject) {
      ref.state = FetchRefState::upToDate;
    } else {
      ref.state = FetchRefState::refusedTagMoved;
      ref.detail = "GitHub moved the tag; the local tag and the GitLab tag were kept";
    }
    result.refs.push_back(std::move(ref));
  }

  for (std::size_t start = 0; start < newTags.size(); start += tagFetchBatchSize) {
    const std::size_t end = std::min(start + tagFetchBatchSize, newTags.size());

    std::vector<std::string> tagArguments = credentialArguments(false);
    tagArguments.insert(tagArguments.end(),
                        {"fetch", "--no-tags", "--no-prune", "--no-prune-tags", sourceRemote});
    for (std::size_t index = start; index < end; ++index) {
      tagArguments.push_back(tagFetchRefspec(newTags[index]));
    }
    ensureNonDestructiveArguments("git fetch", tagArguments);
    const ProcessResult fetchedTags = runner_.run(gitExecutable_, tagArguments, repositoryPath);
    ++result.commands;
    const FetchStatusParse tagStatus = parseFetchStatus(fetchedTags.standardError);

    const TrackingRefList listed = listTrackingRefs(repositoryPath);
    if (listed.ok) {
      tracking = toMap(listed);
    }

    for (std::size_t index = start; index < end; ++index) {
      const std::string& name = newTags[index];
      const std::string trackingName = trackingRef(RefKind::tag, name);
      const std::string& remoteObject = remoteTags.at(name);

      FetchRefResult ref;
      ref.name = name;
      ref.kind = RefKind::tag;
      const auto current = tracking.find(trackingName);
      if (current != tracking.end() && current->second == remoteObject) {
        ref.state = FetchRefState::created;
      } else {
        ref.state = FetchRefState::failed;
        const std::string reason = tagStatus.reasonFor(trackingName);
        ref.detail = reason.empty() ? fetchedTags.diagnostic() : reason;
      }
      result.refs.push_back(std::move(ref));
    }
  }

  // Refs that GitHub no longer has are kept: they are the backup of a history that once existed,
  // and the GitLab copy of them is left in place as well.
  for (const auto& entry : tracking) {
    const std::string& trackingName = entry.first;
    if (startsWith(trackingName, trackingBranchPrefix)) {
      const std::string name = trackingName.substr(std::string(trackingBranchPrefix).size());
      if (remoteBranches.count(name) != 0) {
        continue;
      }
      FetchRefResult ref;
      ref.name = name;
      ref.kind = RefKind::branch;
      ref.state = FetchRefState::retained;
      ref.detail = "GitHub no longer has this branch; the local and GitLab copies were kept";
      result.refs.push_back(std::move(ref));
    } else if (startsWith(trackingName, trackingTagPrefix)) {
      const std::string name = trackingName.substr(std::string(trackingTagPrefix).size());
      if (remoteTags.count(name) != 0) {
        continue;
      }
      FetchRefResult ref;
      ref.name = name;
      ref.kind = RefKind::tag;
      ref.state = FetchRefState::retained;
      ref.detail = "GitHub no longer has this tag; the local and GitLab copies were kept";
      result.refs.push_back(std::move(ref));
    }
  }

  // Only refs that exist locally after the fetch are pushed, so a tag whose fetch failed is
  // reported as such instead of being pushed from a missing source ref.
  for (const auto& entry : tracking) {
    const std::string& trackingName = entry.first;
    if (startsWith(trackingName, trackingBranchPrefix)) {
      const std::string name = trackingName.substr(std::string(trackingBranchPrefix).size());
      if (remoteBranches.count(name) != 0) {
        result.branches.push_back(name);
      }
    } else if (startsWith(trackingName, trackingTagPrefix)) {
      const std::string name = trackingName.substr(std::string(trackingTagPrefix).size());
      if (remoteTags.count(name) != 0) {
        result.tags.push_back(name);
      }
    }
  }

  result.state = result.hasRefFailures() ? FetchState::partial : FetchState::succeeded;
  return result;
}

BulkPushResult GitWorkspace::pushAll(const std::filesystem::path& repositoryPath, RefKind kind,
                                     const std::vector<std::string>& names) const {
  BulkPushResult result;
  std::map<std::string, std::size_t> indexByDestinationRef;
  for (const std::string& name : names) {
    RefPushResult ref;
    ref.name = name;
    ref.kind = kind;
    ref.state = RefPushState::unreported;
    ref.detail = "git reported no result for this ref";
    indexByDestinationRef.emplace(destinationRef(kind, name), result.refs.size());
    result.refs.push_back(std::move(ref));
  }
  if (result.refs.empty()) {
    // Nothing to push: no process is launched at all.
    return result;
  }

  const std::string refspec = kind == RefKind::branch ? branchPushRefspec : tagPushRefspec;
  std::vector<std::string> arguments = credentialArguments(true);
  arguments.insert(arguments.end(), {"push", "--porcelain", destinationRemote, refspec});
  ensureNonDestructiveArguments("git push", arguments);

  const auto started = std::chrono::steady_clock::now();
  const ProcessResult response = runner_.run(gitExecutable_, arguments, repositoryPath);
  result.durationMs = elapsedMilliseconds(started);
  result.exitCode = response.launched ? response.exitCode : -1;
  ++result.commands;

  std::vector<std::string> errors;
  if (!response.launched) {
    errors.push_back("the git push process could not be started: " + response.launchError);
    result.operationError = join(errors, "; ");
    return result;
  }

  const PushPorcelainParse parsed = parsePushPorcelain(response.standardOutput);
  for (const PushPorcelainEntry& entry : parsed.entries) {
    const auto found = indexByDestinationRef.find(entry.to);
    if (found == indexByDestinationRef.end()) {
      // Porcelain output for a ref that was not requested: never invent an outcome for it.
      continue;
    }
    RefPushResult& ref = result.refs[found->second];
    ref.detail = pushDetail(entry);
    if (!entry.from.empty() && entry.from != trackingRef(kind, ref.name)) {
      ref.detail += " (git reported the source ref '" + entry.from + "')";
    }
    switch (entry.flag) {
      case '=':
        ref.state = RefPushState::alreadyUpToDate;
        break;
      case ' ':
      case '*':
        ref.state = RefPushState::pushed;
        break;
      case '+':
        // A forced update is never requested by this program, so it must not go unnoticed.
        ref.state = RefPushState::pushed;
        ref.detail = "forced update (not requested by this program): " + ref.detail;
        errors.push_back("`git push` reported a forced update for " + entry.to);
        break;
      case '!':
        ref.state = classifyRejection(entry.summary + " " + entry.reason, kind);
        break;
      case '-':
        ref.state = RefPushState::failed;
        ref.detail = "git reported the destination ref as deleted: " + ref.detail;
        errors.push_back("`git push` deleted " + entry.to + ", which was never requested");
        break;
      default:
        ref.state = RefPushState::unreported;
        ref.detail = "unrecognised git push status '" + std::string(1, entry.flag) + "': " +
                     ref.detail;
        break;
    }
  }

  if (!parsed.malformedLines.empty()) {
    errors.push_back("`git push` printed " + std::to_string(parsed.malformedLines.size()) +
                     " line(s) that are not in the documented porcelain format (first: '" +
                     parsed.malformedLines.front() + "')");
  }
  if (!parsed.unexpectedStatuses.empty()) {
    errors.push_back("`git push` printed " +
                     std::to_string(parsed.unexpectedStatuses.size()) +
                     " ref line(s) with an undocumented status (first: '" +
                     parsed.unexpectedStatuses.front() + "')");
  }
  if (!parsed.complete() && (!parsed.entries.empty() || response.exitCode == 0)) {
    errors.push_back("the `git push` output ended without its final 'Done' line, so the "
                     "per-ref results may be incomplete");
  }

  std::size_t unreported = 0;
  for (const RefPushResult& ref : result.refs) {
    if (ref.state == RefPushState::unreported) {
      ++unreported;
    }
  }
  if (response.succeeded()) {
    if (unreported != 0) {
      errors.push_back("`git push` exited successfully but reported no result for " +
                       std::to_string(unreported) + " ref(s), whose outcome is therefore unknown");
    }
  } else if (parsed.entries.empty()) {
    errors.push_back("`git push` failed before reporting any ref: " + response.diagnostic());
  } else if (unreported != 0) {
    errors.push_back("`git push` exited with status " + std::to_string(response.exitCode) +
                     " and reported no result for " + std::to_string(unreported) +
                     " ref(s), whose outcome is therefore unknown");
  }

  result.operationError = join(errors, "; ");
  return result;
}

std::vector<std::string> GitWorkspace::staleDestinationRefs(
    const std::filesystem::path& repositoryPath, const FetchResult& fetch) const {
  std::vector<std::string> stale;
  const RemoteRefList destination = listRemoteRefs(repositoryPath, destinationRemote, true);
  if (!destination.ok) {
    return stale;
  }

  const std::set<std::string> branches(fetch.branches.begin(), fetch.branches.end());
  const std::set<std::string> tags(fetch.tags.begin(), fetch.tags.end());

  for (const RemoteRef& ref : destination.refs) {
    if (startsWith(ref.name, destinationBranchPrefix)) {
      const std::string name = ref.name.substr(std::string(destinationBranchPrefix).size());
      if (branches.find(name) == branches.end()) {
        stale.push_back("branch " + name);
      }
    } else if (startsWith(ref.name, destinationTagPrefix)) {
      const std::string name = ref.name.substr(std::string(destinationTagPrefix).size());
      if (tags.find(name) == tags.end()) {
        stale.push_back("tag " + name);
      }
    }
  }
  return stale;
}

}  // namespace nfg2glm
