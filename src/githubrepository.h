#pragma once

#include <string>

namespace nfg2glm {

/// Canonical identity and metadata of one GitHub repository.
struct GitHubRepository {
  std::string owner;          ///< Login of the owning account or organisation.
  std::string name;           ///< Repository name as spelled on GitHub.
  std::string fullName;       ///< "owner/name", the canonical identity used for de-duplication.
  std::string cloneUrl;       ///< HTTPS clone URL reported by the API.
  std::string visibility;     ///< "public", "private" or "internal".
  std::string defaultBranch;  ///< Default branch name, may be empty.
  bool archived = false;
  bool fork = false;
  bool disabled = false;

  bool isPublic() const { return visibility == "public"; }
};

}  // namespace nfg2glm
