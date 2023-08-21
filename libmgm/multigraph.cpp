#include "multigraph.hpp"

#include <utility>
#include <cassert>

Graph::Graph(int id, int no_nodes) : id(id), no_nodes(no_nodes) {};

GmModel::GmModel(Graph g1, Graph g2, int no_assignments, int no_edges) : graph1(g1), graph2(g2) {
    this->costs = std::make_unique<CostMap>(g1.no_nodes, no_assignments, no_edges);
    this->assignment_list.reserve(no_assignments);

    //FIXME: Number of elements for assignments_left and assignments_right is unclear.
    // Loading assignments without reserving space leads to (avoidable?) reallocations. 
    this->assignments_left  = std::vector<std::vector<int>>(g1.no_nodes);
    this->assignments_right = std::vector<std::vector<int>>(g2.no_nodes);
}

void GmModel::add_assignment(int assignment_id, int node1, int node2, double cost) {
    assert (assignment_id == this->assignment_list.size());

    (void) this->assignment_list.emplace_back(node1, node2);

    this->costs->set_unary(node1, node2, cost);
    this->assignments_left[node1].push_back(node2);
    this->assignments_right[node2].push_back(node1);
}

void GmModel::add_edge(int assignment1, int assigment2, double cost) {
    auto& a1 = this->assignment_list[assignment1];
    auto& a2 = this->assignment_list[assignment1];

    this->costs->set_pairwise(a1.first, a1.second, a2.first, a2.second, cost);
    this->costs->set_pairwise(a2.first, a2.second, a1.first, a1.second, cost); //FIXME: RAM overhead. Avoids sorting later though.
}


MgmModel::MgmModel(){ 
    //models.reserve(300);
}