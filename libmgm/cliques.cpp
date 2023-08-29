#include <cassert>
#include <vector>
#include <exception>

#include "cliques.hpp"
#include "multigraph.hpp"


//FIXME: Clique is not Hashmap, not vector.
// Consider changing no_graphs to "max_no_graphs" to emphasize a rehash avoidance.
CliqueTable::CliqueTable(int no_graphs) {
    this->no_graphs = no_graphs;
    this->empty_clique = Clique(this->no_graphs);
}

CliqueTable::CliqueTable(MgmSolution& solution) {
    this->no_graphs = solution.model->no_graphs;
    this->empty_clique = Clique(this->no_graphs);

    this->build_cliques(solution);
}

void CliqueTable::add_clique() {
    this->cliques.push_back(empty_clique);
    this->no_cliques++;
}

// FIXME: Maybe make this a move function
void CliqueTable::add_clique(Clique c) {
    this->cliques.push_back(c);
    this->no_cliques++;
}

void CliqueTable::reserve(int no_cliques) {
    this->cliques.reserve(this->cliques.size() + no_cliques);
}

int& CliqueTable::operator()(int clique_id, int graph_id) {
    return this->cliques.at(clique_id).at(graph_id);
}

const int& CliqueTable::operator()(int clique_id, int graph_id) const {
    return this->cliques.at(clique_id).at(graph_id);
}

std::unordered_map<int, int>& CliqueTable::operator[](int clique_id) {
    return this->cliques.at(clique_id);
}

const std::unordered_map<int, int>& CliqueTable::operator[](int clique_id) const {
    return this->cliques.at(clique_id);
}

MgmSolution CliqueTable::export_solution(std::shared_ptr<MgmModel> model) {
    assert(model->no_graphs == this->no_graphs);
    return MgmSolution(model);
}

void CliqueTable::build_cliques(MgmSolution& solution) {
    throw std::logic_error("Not Implementd");
}
