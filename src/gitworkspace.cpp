#include "gitworkspace.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>

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
constexpr const char* branchRefspec = "+refs/heads/*:refs/nonforceful-github/heads/*";
constexpr const char* tagRefspec = "+refs/tags/*:refs/nonforceful-github/tags/*";
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

/// A push argument list must never contain a destructive or forced option, and never a
/// refspec that asks Git to force the destination.
void ensureNonForcedArguments(const std::vector<std::string>& arguments) {
  for (const std::string& argument : arguments) {
    const bool forbidden = argument == "-f" || argument == "--force" ||
                           argument == "--force-with-lease" ||
                           startsWith(argument, "--force-with-lease=") ||
                           startsWith(argument, "--force") || argument == "--mirror" ||
                           argument == "--delete" || argument == "--prune" ||
                           argument == "--tags" || argument == "--all";
    if (forbidden) {
      throw std::logic_error("refusing to run a destructive git push: " + argument);
    }
    // A leading '+' in a refspec is the short spelling of --force.
    if (!argument.empty() && argument.front() == '+') {
      throw std::logic_error("refusing to run a forced git push: " + argument);
    }
  }
}

RefPushState classifyRejection(const std::string& detail, RefKind kind) {
  const std::string lowered = toLowerAscii(detail);
  if (lowered.find("non-fast-forward") != std::string::npos ||
      lowered.find("fetch first") != std::string::npos ||
      lowered.find("non-fast forward") != std::string::npos) {
    return RefPushState::rejectedNonFastForward;
  }
  if (lowered.find("already exists") != std::string::npos && kind == RefKind::tag) {
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
        !ensureRemote(repositoryPath, destinationRemote, destinationUrl, result.repairs)) {
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

  std::vector<std::string> fetchArguments = credentialArguments(false);
  fetchArguments.insert(fetchArguments.end(),
                        {"fetch", "--quiet", "--no-tags", sourceRemote, branchRefspec, tagRefspec});
  const ProcessResult fetched = runner_.run(gitExecutable_, fetchArguments, repositoryPath);
  if (!fetched.succeeded()) {
    result.error = "fetching from GitHub failed: " + fetched.diagnostic();
    result.category = ErrorCategory::gitCommand;
    return result;
  }

  std::set<std::string> expected;
  for (const RemoteRef& ref : remote.refs) {
    if (startsWith(ref.name, destinationBranchPrefix) || startsWith(ref.name, destinationTagPrefix)) {
      expected.insert(ref.name);
    }
  }

  // Drop source-tracking refs whose GitHub ref has disappeared. Only the application's own
  // namespace is touched: no object, branch or tag is ever deleted.
  const ProcessResult tracked =
      runner_.run(gitExecutable_, {"for-each-ref", "--format=%(refname)", trackingRoot},
                  repositoryPath);
  if (tracked.succeeded()) {
    for (const std::string& localRef : splitLines(tracked.standardOutput)) {
      const std::string name = trim(localRef);
      if (name.empty() || !startsWith(name, trackingRoot)) {
        continue;
      }
      std::string remoteRef;
      if (startsWith(name, trackingBranchPrefix)) {
        remoteRef = destinationBranchPrefix + name.substr(std::string(trackingBranchPrefix).size());
      } else if (startsWith(name, trackingTagPrefix)) {
        remoteRef = destinationTagPrefix + name.substr(std::string(trackingTagPrefix).size());
      } else {
        continue;
      }
      if (expected.find(remoteRef) != expected.end()) {
        continue;
      }
      if (runner_.run(gitExecutable_, {"update-ref", "-d", name}, repositoryPath).succeeded()) {
        ++result.prunedTrackingRefs;
      }
    }
  }

  for (const RemoteRef& ref : remote.refs) {
    if (startsWith(ref.name, destinationBranchPrefix)) {
      result.branches.push_back(ref.name.substr(std::string(destinationBranchPrefix).size()));
    } else if (startsWith(ref.name, destinationTagPrefix)) {
      result.tags.push_back(ref.name.substr(std::string(destinationTagPrefix).size()));
    }
  }

  result.state = FetchState::succeeded;
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
  ensureNonForcedArguments(arguments);

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
