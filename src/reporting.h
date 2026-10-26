#pragma once

#include <ostream>

#include "syncresult.h"

namespace nfg2glm {

/// Print the summary and the per-repository, per-ref detail of a completed run.
///
/// The phase timings are printed separately by @c printRunTimings, because the cost of
/// rendering this report is only known once it has been rendered.
void printRunReport(std::ostream& stream, const RunResult& result);

/// Print the elapsed time of every synchronization phase of a completed run.
void printRunTimings(std::ostream& stream, const RunResult& result);

/// Print the one-line-per-ref detail of a single repository.
void printRepositoryReport(std::ostream& stream, const RepositoryResult& result);

}  // namespace nfg2glm
