#include "storage.h"

#include <platform_folders.h>

#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "textutil.h"

namespace nfg2glm {
namespace {

constexpr const char* applicationDirectoryName = "NonForcefulGitHub2GitLabMirror";
constexpr const char* repositoryDirectoryName = "repositories";

}  // namespace

std::filesystem::path Storage::defaultRoot() {
  const std::string dataHome = sago::getDataHome();
  if (dataHome.empty()) {
    throw std::runtime_error(
        "PlatformFolders could not determine the per-user application-data directory");
  }
  std::error_code error;
  const std::filesystem::path absolute = std::filesystem::absolute(dataHome, error);
  const std::filesystem::path base = error ? std::filesystem::path(dataHome) : absolute;
  return base / applicationDirectoryName;
}

Storage::Storage(std::filesystem::path root)
    : root_(std::move(root)), repositories_(root_ / repositoryDirectoryName) {}

void Storage::ensureExists() {
  std::error_code error;
  std::filesystem::create_directories(repositories_, error);
  if (error && !std::filesystem::is_directory(repositories_)) {
    throw std::runtime_error("cannot create " + repositories_.string() + ": " + error.message());
  }
}

std::filesystem::path Storage::repositoryPath(const GitHubRepository& repository) const {
  const std::string owner = sanitizeComponent(toLowerAscii(repository.owner));
  const std::string name = sanitizeComponent(toLowerAscii(repository.name));
  const std::string suffix = stableHashHex(repository.fullName).substr(0, 12);
  return repositories_ / (owner + "--" + name + "--" + suffix + ".git");
}

std::string Storage::sanitizeComponent(const std::string& text) {
  std::string sanitized;
  sanitized.reserve(text.size());
  for (const char character : text) {
    const unsigned char code = static_cast<unsigned char>(character);
    if (std::isalnum(code) != 0 || character == '.' || character == '_' || character == '-') {
      sanitized.push_back(character);
    } else {
      sanitized.push_back('-');
    }
  }
  return sanitized;
}

std::string Storage::stableHashHex(const std::string& text) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char character : text) {
    hash ^= character;
    hash *= 1099511628211ULL;
  }
  std::ostringstream rendered;
  rendered << std::hex << std::setw(16) << std::setfill('0') << hash;
  return rendered.str();
}

}  // namespace nfg2glm
