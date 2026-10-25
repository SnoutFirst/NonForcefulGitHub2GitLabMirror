#include "githubdiscovery.h"

#include <boost/json.hpp>

#include <map>
#include <optional>
#include <string_view>

#include "textutil.h"

namespace nfg2glm {
namespace {

constexpr std::size_t pageSize = 100;
constexpr std::size_t maximumPages = 1000;

std::string jsonString(const boost::json::object& object, std::string_view key) {
  const boost::json::value* value = object.if_contains(key);
  if (value == nullptr || !value->is_string()) {
    return {};
  }
  return std::string(value->get_string());
}

bool jsonBool(const boost::json::object& object, std::string_view key, bool fallback) {
  const boost::json::value* value = object.if_contains(key);
  if (value == nullptr || !value->is_bool()) {
    return fallback;
  }
  return value->get_bool();
}

std::optional<GitHubRepository> parseRepository(const boost::json::value& value) {
  if (!value.is_object()) {
    return std::nullopt;
  }
  const boost::json::object& object = value.get_object();

  GitHubRepository repository;
  repository.name = jsonString(object, "name");
  repository.fullName = jsonString(object, "full_name");
  repository.cloneUrl = jsonString(object, "clone_url");
  repository.defaultBranch = jsonString(object, "default_branch");
  repository.archived = jsonBool(object, "archived", false);
  repository.fork = jsonBool(object, "fork", false);
  repository.disabled = jsonBool(object, "disabled", false);

  const boost::json::value* ownerValue = object.if_contains("owner");
  if (ownerValue != nullptr && ownerValue->is_object()) {
    repository.owner = jsonString(ownerValue->get_object(), "login");
  }

  repository.visibility = jsonString(object, "visibility");
  if (repository.visibility.empty()) {
    repository.visibility = jsonBool(object, "private", true) ? "private" : "public";
  }

  if (repository.name.empty() || repository.fullName.empty() || repository.owner.empty() ||
      repository.cloneUrl.empty()) {
    return std::nullopt;
  }
  return repository;
}

bool looksLikeAuthenticationFailure(const std::string& text) {
  return text.find("401") != std::string::npos || text.find("403") != std::string::npos ||
         text.find("authentication") != std::string::npos ||
         text.find("gh auth login") != std::string::npos ||
         text.find("not logged") != std::string::npos;
}

}  // namespace

GitHubDiscovery::GitHubDiscovery(const ProcessRunner& runner, std::string ghExecutable)
    : runner_(runner), ghExecutable_(std::move(ghExecutable)) {}

bool GitHubDiscovery::verifyAuthentication(std::string& account, std::string& error) const {
  const ProcessResult user = runner_.run(ghExecutable_, {"api", "/user"});
  if (!user.succeeded()) {
    error = "GitHub CLI authentication check failed: " + user.diagnostic();
    return false;
  }

  boost::system::error_code parseError;
  const boost::json::value document = boost::json::parse(user.standardOutput, parseError);
  if (parseError || !document.is_object()) {
    error = "GitHub CLI returned an unreadable response for /user";
    return false;
  }
  account = jsonString(document.get_object(), "login");
  if (account.empty()) {
    error = "GitHub CLI is authenticated but did not report an account login";
    return false;
  }

  const ProcessResult probe = runner_.run(
      ghExecutable_, {"api",
                      "/user/repos?affiliation=owner,collaborator,organization_member&per_page=1"});
  if (!probe.succeeded()) {
    error = "the authenticated GitHub account cannot enumerate repositories: " + probe.diagnostic();
    return false;
  }
  return true;
}

DiscoveryResult GitHubDiscovery::discover() const {
  DiscoveryResult result;
  std::map<std::string, GitHubRepository> byIdentity;

  for (std::size_t page = 1; page <= maximumPages; ++page) {
    const std::string endpoint =
        "/user/repos?affiliation=owner,collaborator,organization_member&per_page=" +
        std::to_string(pageSize) + "&sort=full_name&page=" + std::to_string(page);

    const ProcessResult response = runner_.run(ghExecutable_, {"api", endpoint});
    if (!response.succeeded()) {
      result.ok = false;
      result.complete = false;
      result.error = "GitHub repository discovery failed on page " + std::to_string(page) + ": " +
                     response.diagnostic();
      result.category = looksLikeAuthenticationFailure(response.diagnostic())
                            ? ErrorCategory::authentication
                            : ErrorCategory::discovery;
      return result;
    }

    boost::system::error_code parseError;
    const boost::json::value document = boost::json::parse(response.standardOutput, parseError);
    if (parseError) {
      result.ok = false;
      result.complete = false;
      result.error = "cannot parse the GitHub response for page " + std::to_string(page) + ": " +
                     parseError.message();
      result.category = ErrorCategory::discovery;
      return result;
    }
    if (!document.is_array()) {
      result.ok = false;
      result.complete = false;
      result.error = "the GitHub response for page " + std::to_string(page) + " is not an array";
      result.category = ErrorCategory::discovery;
      return result;
    }

    const boost::json::array& pageItems = document.get_array();
    for (const boost::json::value& item : pageItems) {
      const std::optional<GitHubRepository> repository = parseRepository(item);
      if (!repository.has_value()) {
        ++result.malformedEntries;
        continue;
      }
      const std::string identity = toLowerAscii(repository->fullName);
      if (byIdentity.find(identity) != byIdentity.end()) {
        ++result.duplicateEntries;
        continue;
      }
      byIdentity.emplace(identity, *repository);
    }

    if (pageItems.size() < pageSize) {
      result.ok = true;
      result.complete = result.malformedEntries == 0;
      if (!result.complete) {
        result.error = std::to_string(result.malformedEntries) +
                       " repository entries could not be interpreted";
      }
      break;
    }

    if (page == maximumPages) {
      result.ok = true;
      result.complete = false;
      result.error = "pagination stopped after " + std::to_string(maximumPages) +
                     " pages; the repository list may be truncated";
    }
  }

  for (auto& entry : byIdentity) {
    result.repositories.push_back(std::move(entry.second));
  }
  return result;
}

}  // namespace nfg2glm
