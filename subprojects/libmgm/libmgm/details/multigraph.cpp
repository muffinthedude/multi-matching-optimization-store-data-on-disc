#include "multigraph.hpp"

#include <utility>
#include <cassert>
#include<stdexcept>

namespace mgm {
    
Graph::Graph(int id, int no_nodes) : id(id), no_nodes(no_nodes) {};

GmModel::GmModel(Graph g1, Graph g2, int no_assignments, int no_edges) 
    : 
    graph1(g1), 
    graph2(g2),
    no_assignments(no_assignments),
    no_edges(no_edges) 
    {
    CostMap cost_map(no_assignments, no_edges);
    this->costs = stxxl::external_shared_ptr<CostMap>(cost_map);
    this->assignment_list.reserve(no_assignments);

    //FIXME: Number of elements for assignments_left and assignments_right is unclear.
    // Loading assignments without reserving space leads to (avoidable?) reallocations. 
    this->assignments_left  = std::vector<std::vector<int>>(g1.no_nodes);
    this->assignments_right = std::vector<std::vector<int>>(g2.no_nodes);
}

void GmModel::add_assignment([[maybe_unused]] int assignment_id, int node1, int node2, double cost) {
    assert ((size_t) assignment_id == this->assignment_list.size());

    (void) this->assignment_list.emplace_back(node1, node2);
    //CostMap cost_map(this->costs.get());
    //cost_map.set_unary(node1, node2, cost);
    //this->costs = stxxl::external_shared_ptr<CostMap>(cost_map);
    this->assignments_left[node1].push_back(node2);
    this->assignments_right[node2].push_back(node1);
}

void GmModel::add_edge(int assignment1, int assignment2, double cost) {
    auto& a1 = this->assignment_list[assignment1];
    auto& a2 = this->assignment_list[assignment2];

    this->add_edge(a1.first, a1.second, a2.first, a2.second, cost);
}

void GmModel::add_edge(int assignment1_node1, int assignment1_node2, int assignment2_node1, int assignment2_node2, double cost) {
    this->costs.get().set_pairwise(assignment1_node1, assignment1_node2, assignment2_node1, assignment2_node2, cost);
    //this->costs->set_pairwise(a2.first, a2.second, a1.first, a1.second, cost); //FIXME: RAM overhead. Avoids sorting later though.
}


MgmModel::MgmModel(){ 
    //models.reserve(300);
}
stxxl::external_shared_ptr<GmModel>& stxxl_unordered_map::at(const GmModelIdx idx)
{
    auto result = this->find(idx);
    if (result == this->end()) {
        throw std::out_of_range("Element not found in container");
    }

    return (*result).second;
}
const stxxl::external_shared_ptr<GmModel> &stxxl_unordered_map::at(const GmModelIdx idx) const
{
    auto result = this->find(idx);
    if (result == this->end()) {
        throw std::out_of_range("Element not found in container");
    }

    return (*result).second;
}
}