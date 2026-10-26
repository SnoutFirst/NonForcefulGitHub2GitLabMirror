#include <exception>
#include <iostream>
#include <string>

#include "nonforcefulgithub2gitlabmirror.h"

namespace {

constexpr const char* kUsage =
    "Usage: NonForcefulGitHub2GitLabMirror [--verbose]\n"
    "\n"
    "  --verbose, -v  list every branch and tag in the final report, including the ones that were\n"
    "                 already up to date (by default those are only counted, one line per\n"
    "                 repository); refs that were pushed, rejected or left unknown are always\n"
    "                 listed\n"
    "  --help, -h     show this message\n";

}  // namespace

int main(int argc, char** argv) {
  bool verbose = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--verbose" || argument == "-v") {
      verbose = true;
    } else if (argument == "--help" || argument == "-h") {
      std::cout << kUsage;
      return 0;
    } else {
      std::cerr << "error: unknown command line argument '" << argument << "'\n\n" << kUsage;
      return 2;
    }
  }

  try {
    nfg2glm::NonForcefulGitHub2GitLabMirror application(verbose);
    return application.run();
  } catch (const std::exception& error) {
    std::cerr << "fatal: " << error.what() << '\n';
    return 1;
  }
}
