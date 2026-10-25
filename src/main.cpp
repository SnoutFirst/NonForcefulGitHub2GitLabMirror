#include <exception>
#include <iostream>

#include "nonforcefulgithub2gitlabmirror.h"

int main(int argc, char** argv) {
  (void)argv;
  if (argc > 1) {
    std::cerr << "NonForcefulGitHub2GitLabMirror takes no command line arguments.\n";
    return 2;
  }

  try {
    nfg2glm::NonForcefulGitHub2GitLabMirror application;
    return application.run();
  } catch (const std::exception& error) {
    std::cerr << "fatal: " << error.what() << '\n';
    return 1;
  }
}
