#pragma once

#include <ostream>

#include "syncresult.h"

namespace nfg2glm {

/// Print the summary and the per-repository, per-ref detail of a completed run.
///
/// Unless @c verbose is set, a ref that is merely `already up to date` is not listed one line per
/// ref: the unchanged refs of each kind are counted in a single line instead, and only refs that
/// were pushed, rejected or left unknown appear individually.
///
/// The phase timings are printed separately by @c printRunTimings, because the cost of
/// rendering this report is only known once it has been rendered.
void printRunReport(std::ostream& stream, const RunResult& result, bool verbose);

/// Print the elapsed time of every synchronization phase of a completed run.
void printRunTimings(std::ostream& stream, const RunResult& result);

/// Print the one-line-per-ref detail of a single repository, honouring @c verbose as described
/// for @c printRunReport.
void printRepositoryReport(std::ostream& stream, const RepositoryResult& result, bool verbose);

}  // namespace nfg2glm
