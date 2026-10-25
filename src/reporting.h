#pragma once

#include <ostream>

#include "syncresult.h"

namespace nfg2glm {

/// Print the summary and the per-repository, per-ref detail of a completed run.
void printRunReport(std::ostream& stream, const RunResult& result);

/// Print the one-line-per-ref detail of a single repository.
void printRepositoryReport(std::ostream& stream, const RepositoryResult& result);

}  // namespace nfg2glm
