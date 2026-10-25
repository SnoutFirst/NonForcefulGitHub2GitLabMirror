#include "gitlabproject.h"

#include <boost/json.hpp>

#include <cctype>
#include <cstdio>
#include <sstream>
#include <string_view>

#include "textutil.h"

namespace nfg2glm {
namespace {

std::string jsonString(const boost::json::object& object, std::string_view key) {
  const boost::json::value* value = object.if_contains(key);
  if (value == nullptr || !value->is_string()) {
    return {};
  }
  return std::string(value->get_string());
}

std::string percentEncode(const std::string& text) {
  std::string encoded;
  for (const char character : text) {
    const unsigned char code = static_cast<unsigned char>(character);
    if (std::isalnum(code) != 0 || character == '-' || character == '.' || character == '_' ||
        character == '~') {
      encoded.push_back(character);
    } else {
      char buffer[4] = {'%', '\0', '\0', '\0'};
      std::snprintf(buffer + 1, 3, "%02X", static_cast<unsigned int>(code));
      encoded.append(buffer, 3);
    }
  }
  return encoded;
}

bool mentionsHttpStatus(const std::string& text, const std::string& status) {
  return text.find(status) != std::string::npos;
}

bool looksLikeMissingProject(const std::string& text) {
  return mentionsHttpStatus(text, "404");
}

bool looksLikeAuthenticationFailure(const std::string& text) {
  return mentionsHttpStatus(text, "401") || mentionsHttpStatus(text, "403") ||
         text.find("Unauthorized") != std::string::npos;
}

bool looksLikePathCollision(const std::string& text) {
  return text.find("has already been taken") != std::string::npos ||
         text.find("already exists") != std::string::npos ||
         mentionsHttpStatus(text, "400");
}

}  // namespace

GitLabProjects::GitLabProjects(const ProcessRunner& runner, std::string glabExecutable)
    : runner_(runner), glabExecutable_(std::move(glabExecutable)) {}

bool GitLabProjects::verifyAuthentication(std::string& namespacePath, std::string& error) const {
  const ProcessResult user = runner_.run(glabExecutable_, {"api", "user"});
  if (!user.succeeded()) {
    error = "GitLab CLI authentication check failed: " + user.diagnostic();
    return false;
  }

  boost::system::error_code parseError;
  const boost::json::value document = boost::json::parse(user.standardOutput, parseError);
  if (parseError || !document.is_object()) {
    error = "GitLab CLI returned an unreadable response for the authenticated user";
    return false;
  }
  namespacePath = jsonString(document.get_object(), "username");
  if (namespacePath.empty()) {
    error = "GitLab CLI is authenticated but did not report a username";
    return false;
  }
  return true;
}

std::string GitLabProjects::requiredVisibility(const GitHubRepository& repository) {
  // GitHub public mirrors are public; private, internal and anything unknown stay private.
  return repository.isPublic() ? "public" : "private";
}

std::string GitLabProjects::projectSlug(const std::string& name) {
  std::string slug;
  bool pendingSeparator = false;
  for (const char character : name) {
    const unsigned char code = static_cast<unsigned char>(character);
    const char lowered = static_cast<char>(std::tolower(code));
    if (std::isalnum(code) != 0 || lowered == '_' || lowered == '.' || lowered == '-') {
      if (pendingSeparator && !slug.empty()) {
        slug.push_back('-');
      }
      pendingSeparator = false;
      slug.push_back(lowered);
    } else if (!slug.empty()) {
      pendingSeparator = true;
    }
  }
  while (!slug.empty() && (slug.back() == '-' || slug.back() == '.')) {
    slug.pop_back();
  }
  if (slug.empty()) {
    slug = "repository";
  }
  if (std::isdigit(static_cast<unsigned char>(slug.front())) != 0 || slug.front() == '.' ||
      slug.front() == '-') {
    slug.insert(slug.begin(), 'r');
  }
  return slug;
}

GitLabProjects::Lookup GitLabProjects::lookUp(const std::string& pathWithNamespace) const {
  Lookup lookup;
  const ProcessResult response =
      runner_.run(glabExecutable_, {"api", "/projects/" + percentEncode(pathWithNamespace)});
  if (!response.succeeded()) {
    const std::string diagnostic = response.diagnostic();
    if (looksLikeMissingProject(diagnostic)) {
      lookup.found = false;
      return lookup;
    }
    lookup.error = "GitLab project lookup for " + pathWithNamespace + " failed: " + diagnostic;
    lookup.category = looksLikeAuthenticationFailure(diagnostic) ? ErrorCategory::authentication
                                                                : ErrorCategory::gitlabProject;
    return lookup;
  }

  boost::system::error_code parseError;
  const boost::json::value document = boost::json::parse(response.standardOutput, parseError);
  if (parseError || !document.is_object()) {
    lookup.error = "cannot parse the GitLab response for " + pathWithNamespace;
    lookup.category = ErrorCategory::gitlabProject;
    return lookup;
  }

  const boost::json::object& project = document.get_object();
  lookup.found = true;
  lookup.project.outcome = ProjectOutcome::existing;
  lookup.project.pathWithNamespace = jsonString(project, "path_with_namespace");
  lookup.project.webUrl = jsonString(project, "web_url");
  lookup.project.httpUrlToRepo = jsonString(project, "http_url_to_repo");
  lookup.project.visibility = jsonString(project, "visibility");

  if (lookup.project.pathWithNamespace.empty()) {
    lookup.found = false;
    lookup.error = "the GitLab response for " + pathWithNamespace + " has no path";
    lookup.category = ErrorCategory::gitlabProject;
  }
  return lookup;
}

bool GitLabProjects::disableCiCd(const std::string& pathWithNamespace, std::string& error) const {
  // Equivalent to: glab api 'projects/<namespace>%2F<path>' --method PUT
  //                        --raw-field builds_access_level=disabled
  // The project is addressed by its URL-encoded full path, so the value stays valid even when the
  // numeric project id is not cached.
  const ProcessResult response =
      runner_.run(glabExecutable_, {"api", "--method", "PUT",
                                    "/projects/" + percentEncode(pathWithNamespace), "--raw-field",
                                    "builds_access_level=disabled"});
  if (!response.succeeded()) {
    error = "CI/CD could not be disabled on the new project " + pathWithNamespace + ": " +
            response.diagnostic();
    return false;
  }

  boost::system::error_code parseError;
  const boost::json::value document = boost::json::parse(response.standardOutput, parseError);
  if (parseError || !document.is_object()) {
    error = "cannot parse the GitLab response after disabling CI/CD on " + pathWithNamespace;
    return false;
  }
  const std::string buildAccess = jsonString(document.get_object(), "builds_access_level");
  if (!buildAccess.empty() && buildAccess != "disabled") {
    error = "GitLab reports builds_access_level='" + buildAccess + "' for " + pathWithNamespace +
            " even though it was set to disabled";
    return false;
  }
  return true;
}

ProjectResult GitLabProjects::create(const GitHubRepository& repository, const std::string& slug,
                                     const std::string& visibility) const {
  ProjectResult result;
  const ProcessResult response =
      runner_.run(glabExecutable_, {"api", "--method", "POST", "/projects", "--raw-field",
                                    "name=" + repository.name, "--raw-field", "path=" + slug,
                                    "--raw-field", "visibility=" + visibility});
  if (!response.succeeded()) {
    result.error = "creating the GitLab project failed: " + response.diagnostic();
    result.category = looksLikeAuthenticationFailure(response.diagnostic())
                          ? ErrorCategory::authentication
                          : (looksLikePathCollision(response.diagnostic())
                                 ? ErrorCategory::collision
                                 : ErrorCategory::gitlabProject);
    return result;
  }

  boost::system::error_code parseError;
  const boost::json::value document = boost::json::parse(response.standardOutput, parseError);
  if (parseError || !document.is_object()) {
    result.error = "cannot parse the GitLab response for the created project";
    result.category = ErrorCategory::gitlabProject;
    return result;
  }

  const boost::json::object& project = document.get_object();
  result.outcome = ProjectOutcome::created;
  result.pathWithNamespace = jsonString(project, "path_with_namespace");
  result.webUrl = jsonString(project, "web_url");
  result.httpUrlToRepo = jsonString(project, "http_url_to_repo");
  result.visibility = jsonString(project, "visibility");
  if (result.pathWithNamespace.empty() || result.httpUrlToRepo.empty()) {
    result.outcome = ProjectOutcome::failed;
    result.error = "GitLab created the project but the response lacks the repository URL";
    result.category = ErrorCategory::gitlabProject;
    return result;
  }

  // The project was created by this run, so its CI/CD is switched off right away: the pushes that
  // follow must not trigger pipelines. Settings of pre-existing projects are never touched.
  std::string ciCdError;
  if (disableCiCd(result.pathWithNamespace, ciCdError)) {
    result.ciCdDisabled = true;
  } else {
    result.ciCdError = ciCdError;
  }
  return result;
}

ProjectResult GitLabProjects::ensureProject(const GitHubRepository& repository) const {
  ProjectResult result;
  if (namespace_.empty()) {
    result.error = "no GitLab namespace was resolved before project lookup";
    result.category = ErrorCategory::gitlabProject;
    return result;
  }

  const std::string slug = projectSlug(repository.name);
  const std::string candidate = namespace_ + "/" + slug;
  const std::string visibility = requiredVisibility(repository);

  const Lookup existing = lookUp(candidate);
  if (existing.found) {
    result = existing.project;
    result.visibilityMatchesRequirement = equalsIgnoreCase(result.visibility, visibility);
    if (!result.visibilityMatchesRequirement) {
      result.error = "the existing project is " + result.visibility + " but the GitHub source " +
                     repository.visibility + " requires " + visibility +
                     "; the visibility was left unchanged";
    }
    const std::string actual = result.pathWithNamespace;
    if (!equalsIgnoreCase(actual, candidate)) {
      result.outcome = ProjectOutcome::failed;
      result.error = "the resolved project " + actual + " is not the expected project " + candidate;
      result.category = ErrorCategory::gitlabProject;
    }
    return result;
  }
  if (!existing.error.empty()) {
    result = existing.project;
    result.error = existing.error;
    result.category = existing.category;
    return result;
  }

  result = create(repository, slug, visibility);

  // Creation can fail for reasons that do not mean the project is absent. Look again before
  // reporting a failure so that a partially completed run can be resumed.
  if (result.outcome == ProjectOutcome::failed) {
    const Lookup retry = lookUp(candidate);
    if (retry.found) {
      ProjectResult recovered = retry.project;
      recovered.visibilityMatchesRequirement = equalsIgnoreCase(recovered.visibility, visibility);
      if (!recovered.visibilityMatchesRequirement) {
        recovered.error = "the existing project is " + recovered.visibility +
                          " but the GitHub source requires " + visibility +
                          "; the visibility was left unchanged";
      }
      const std::string note =
          "note: the first creation attempt reported: " + result.error;
      recovered.error = recovered.error.empty() ? note : recovered.error + "; " + note;
      return recovered;
    }
  }
  return result;
}

}  // namespace nfg2glm
