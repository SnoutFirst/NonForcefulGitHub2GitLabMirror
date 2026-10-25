#include "processrunner.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/process/v2/environment.hpp>
#include <boost/process/v2/impl/environment.ipp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/start_dir.hpp>
#include <boost/process/v2/stdio.hpp>

#include <algorithm>
#include <optional>
#include <utility>

#include "textutil.h"

namespace nfg2glm {
namespace {

namespace bp = boost::process::v2;

using EnvironmentChar = bp::environment::char_type;
using EnvironmentString = std::basic_string<EnvironmentChar>;

EnvironmentString toEnvironmentString(const std::string& text) {
  EnvironmentString converted;
  converted.reserve(text.size());
  for (const char character : text) {
    converted.push_back(static_cast<EnvironmentChar>(character));
  }
  return converted;
}

/// Copy the current environment and force non-interactive Git behaviour.
///
/// Credential helpers still run normally; only terminal prompts are disabled so that an
/// unattended run can never block waiting for a password.
std::vector<EnvironmentString> buildChildEnvironment() {
  std::vector<EnvironmentString> entries;
  for (auto entry : bp::environment::current()) {
    entries.push_back(std::get<0>(entry).native_string() + EnvironmentString(1, '=') +
                      std::get<1>(entry).native_string());
  }

  const EnvironmentString promptPrefix =
      toEnvironmentString("GIT_TERMINAL_PROMPT") + EnvironmentString(1, '=');
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&promptPrefix](const EnvironmentString& entry) {
                                 return entry.compare(0, promptPrefix.size(), promptPrefix) == 0;
                               }),
                entries.end());
  entries.push_back(promptPrefix + toEnvironmentString("0"));
  return entries;
}

}  // namespace

std::string ProcessResult::diagnostic() const {
  if (!launched) {
    return launchError.empty() ? std::string("the process could not be started") : launchError;
  }
  const std::string errorText = trim(standardError);
  if (!errorText.empty()) {
    return errorText;
  }
  const std::string outputText = trim(standardOutput);
  if (!outputText.empty()) {
    return outputText;
  }
  if (terminatedBySignal) {
    return "the process was terminated by signal " + std::to_string(-exitCode);
  }
  return "the process exited with status " + std::to_string(exitCode);
}

std::filesystem::path ProcessRunner::findExecutable(const std::string& name) const {
  if (name.empty()) {
    return {};
  }
  const boost::filesystem::path resolved = bp::environment::find_executable(name);
  if (resolved.empty()) {
    return {};
  }
  return std::filesystem::path(resolved.string());
}

ProcessResult ProcessRunner::run(const std::string& executable,
                                 const std::vector<std::string>& arguments,
                                 const std::filesystem::path& workingDirectory) const {
  ProcessResult result;

  boost::asio::io_context context;
  boost::asio::readable_pipe outputPipe{context};
  boost::asio::readable_pipe errorPipe{context};

  std::optional<bp::process> child;
  try {
    if (workingDirectory.empty()) {
      child.emplace(context, executable, arguments,
                    bp::process_stdio{nullptr, outputPipe, errorPipe},
                    bp::process_environment(buildChildEnvironment()));
    } else {
      child.emplace(context, executable, arguments,
                    bp::process_stdio{nullptr, outputPipe, errorPipe},
                    bp::process_start_dir(workingDirectory.string()),
                    bp::process_environment(buildChildEnvironment()));
    }
  } catch (const std::exception& error) {
    result.launched = false;
    result.launchError = error.what();
    return result;
  }

  // The read ends are assigned while the child is launched, so the asynchronous drains can
  // only be started afterwards. Both streams are read concurrently, which keeps a chatty
  // child from filling a pipe buffer and deadlocking.
  const auto drain = [](boost::asio::readable_pipe& pipe, std::string& sink) {
    boost::asio::async_read(
        pipe, boost::asio::dynamic_buffer(sink),
        [](const boost::system::error_code&, std::size_t) { /* end of stream */ });
  };
  drain(outputPipe, result.standardOutput);
  drain(errorPipe, result.standardError);

  try {
    context.run();
  } catch (const std::exception& error) {
    result.launched = false;
    result.launchError = std::string("failed while reading process output: ") + error.what();
    return result;
  }

  try {
    result.exitCode = child->wait();
  } catch (const std::exception& error) {
    result.launched = false;
    result.launchError = std::string("failed while waiting for the process: ") + error.what();
    return result;
  }

  result.launched = true;
  result.terminatedBySignal = result.exitCode < 0;
  return result;
}

}  // namespace nfg2glm
