#include "mcmf_batch_strategy.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>

namespace {

struct FlowEdge {
  int to;
  int reverse_index;
  int capacity;
  int cost;
  int original_capacity;
};

class MinCostMaxFlow {
public:
  explicit MinCostMaxFlow(int node_count) : graph_(node_count) {}

  void add_edge(int from, int to, int capacity, int cost) {
    FlowEdge forward{to, static_cast<int>(graph_[to].size()), capacity, cost,
                     capacity};
    FlowEdge reverse{from, static_cast<int>(graph_[from].size()), 0, -cost, 0};
    graph_[from].push_back(forward);
    graph_[to].push_back(reverse);
  }

  void run(int source, int sink) {
    const int node_count = static_cast<int>(graph_.size());
    const int inf = std::numeric_limits<int>::max() / 4;

    std::vector<int> distance(node_count);
    std::vector<int> previous_node(node_count);
    std::vector<int> previous_edge(node_count);
    std::vector<bool> in_queue(node_count);

    while (true) {
      std::fill(distance.begin(), distance.end(), inf);
      std::fill(previous_node.begin(), previous_node.end(), -1);
      std::fill(previous_edge.begin(), previous_edge.end(), -1);
      std::fill(in_queue.begin(), in_queue.end(), false);

      std::queue<int> queue;
      distance[source] = 0;
      queue.push(source);
      in_queue[source] = true;

      while (!queue.empty()) {
        const int current = queue.front();
        queue.pop();
        in_queue[current] = false;

        for (int edge_index = 0;
             edge_index < static_cast<int>(graph_[current].size());
             ++edge_index) {
          const FlowEdge &edge = graph_[current][edge_index];
          if (edge.capacity <= 0) {
            continue;
          }

          const int next_distance = distance[current] + edge.cost;
          if (distance[current] != inf && next_distance < distance[edge.to]) {
            distance[edge.to] = next_distance;
            previous_node[edge.to] = current;
            previous_edge[edge.to] = edge_index;
            if (!in_queue[edge.to]) {
              queue.push(edge.to);
              in_queue[edge.to] = true;
            }
          }
        }
      }

      if (previous_node[sink] == -1) {
        break;
      }

      int augment = 1;
      for (int node = sink; node != source; node = previous_node[node]) {
        const FlowEdge &edge = graph_[previous_node[node]][previous_edge[node]];
        augment = std::min(augment, edge.capacity);
      }

      for (int node = sink; node != source; node = previous_node[node]) {
        FlowEdge &edge = graph_[previous_node[node]][previous_edge[node]];
        FlowEdge &reverse_edge = graph_[edge.to][edge.reverse_index];
        edge.capacity -= augment;
        reverse_edge.capacity += augment;
      }
    }
  }

  const std::vector<std::vector<FlowEdge>> &graph() const { return graph_; }

private:
  std::vector<std::vector<FlowEdge>> graph_;
};

std::vector<int> sorted_unique_taxis(
    const std::vector<CandidateEdge> &candidate_edges) {
  std::vector<int> taxis;
  for (const auto &edge : candidate_edges) {
    if (edge.taxi_id >= 0 && edge.request_id >= 0 && edge.pickup_cost >= 0) {
      taxis.push_back(edge.taxi_id);
    }
  }

  std::sort(taxis.begin(), taxis.end());
  taxis.erase(std::unique(taxis.begin(), taxis.end()), taxis.end());
  return taxis;
}

std::vector<int> sorted_unique_requests(
    const std::vector<CandidateEdge> &candidate_edges) {
  std::vector<int> requests;
  for (const auto &edge : candidate_edges) {
    if (edge.taxi_id >= 0 && edge.request_id >= 0 && edge.pickup_cost >= 0) {
      requests.push_back(edge.request_id);
    }
  }

  std::sort(requests.begin(), requests.end());
  requests.erase(std::unique(requests.begin(), requests.end()),
                 requests.end());
  return requests;
}

std::unordered_map<int, int> build_index(const std::vector<int> &ids) {
  std::unordered_map<int, int> index_by_id;
  for (int index = 0; index < static_cast<int>(ids.size()); ++index) {
    index_by_id.emplace(ids[index], index);
  }
  return index_by_id;
}

} // namespace

std::vector<Assignment> McmfBatchStrategy::assign(
    const std::vector<CandidateEdge> &candidate_edges) const {
  const std::vector<CandidateEdge> normalized_edges =
      normalize_candidate_edges(candidate_edges);
  const std::vector<int> taxi_ids = sorted_unique_taxis(normalized_edges);
  const std::vector<int> request_ids = sorted_unique_requests(normalized_edges);
  if (taxi_ids.empty() || request_ids.empty()) {
    return {};
  }

  const std::unordered_map<int, int> taxi_index_by_id = build_index(taxi_ids);
  const std::unordered_map<int, int> request_index_by_id =
      build_index(request_ids);

  const int source = 0;
  const int taxi_offset = 1;
  const int request_offset = taxi_offset + static_cast<int>(taxi_ids.size());
  const int sink = request_offset + static_cast<int>(request_ids.size());

  MinCostMaxFlow flow(sink + 1);

  for (int taxi_index = 0; taxi_index < static_cast<int>(taxi_ids.size());
       ++taxi_index) {
    flow.add_edge(source, taxi_offset + taxi_index, 1, 0);
  }

  for (const auto &edge : normalized_edges) {
    if (edge.taxi_id < 0 || edge.request_id < 0 || edge.pickup_cost < 0) {
      continue;
    }

    const auto taxi_it = taxi_index_by_id.find(edge.taxi_id);
    const auto request_it = request_index_by_id.find(edge.request_id);
    if (taxi_it == taxi_index_by_id.end() ||
        request_it == request_index_by_id.end()) {
      continue;
    }

    const int taxi_node = taxi_offset + taxi_it->second;
    const int request_node = request_offset + request_it->second;
    flow.add_edge(taxi_node, request_node, 1, edge.pickup_cost);
  }

  for (int request_index = 0;
       request_index < static_cast<int>(request_ids.size()); ++request_index) {
    flow.add_edge(request_offset + request_index, sink, 1, 0);
  }

  flow.run(source, sink);

  std::vector<Assignment> assignments;
  const auto &graph = flow.graph();
  for (int taxi_index = 0; taxi_index < static_cast<int>(taxi_ids.size());
       ++taxi_index) {
    const int taxi_node = taxi_offset + taxi_index;
    for (const auto &edge : graph[taxi_node]) {
      if (edge.original_capacity != 1 || edge.capacity != 0 ||
          edge.to < request_offset || edge.to >= sink) {
        continue;
      }

      const int request_index = edge.to - request_offset;
      assignments.emplace_back(taxi_ids[taxi_index], request_ids[request_index],
                               edge.cost);
    }
  }

  std::sort(assignments.begin(), assignments.end(),
            [](const Assignment &lhs, const Assignment &rhs) {
              if (lhs.request_id != rhs.request_id) {
                return lhs.request_id < rhs.request_id;
              }
              return lhs.taxi_id < rhs.taxi_id;
            });
  return assignments;
}

std::vector<Assignment>
McmfBatchStrategy::assign(const BatchDispatchInput &batch,
                          const CandidateEdgeOptions &candidate_options) const {
  return assign(generate_candidate_edges(batch, candidate_options));
}
