#pragma once

#include "dispatch_batch.h"

#include <vector>

class McmfBatchStrategy {
public:
  std::vector<Assignment>
  assign(const std::vector<CandidateEdge> &candidate_edges) const;

  std::vector<Assignment>
  assign(const BatchDispatchInput &batch,
         const CandidateEdgeOptions &candidate_options) const;
};
