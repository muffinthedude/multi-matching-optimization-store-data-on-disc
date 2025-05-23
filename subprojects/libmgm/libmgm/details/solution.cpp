#include <map>
#include <vector>
#include <memory>
#include <utility>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cassert>
#include <numeric>

// Logging
#include <spdlog/spdlog.h>

#include "multigraph.hpp"
#include "solution.hpp"
#include "cliques.hpp"
#include "solver_mgm.hpp"

namespace mgm {

constexpr double INFINITY_COST = 1e99;

GmSolution::GmSolution(std::shared_ptr<GmModel> model) {
    this->model = model;
    this->labeling = std::vector<int>(model->graph1.no_nodes, -1);
    this->old_labeling = std::vector<int>(model->graph1.no_nodes, -1);
}

GmSolution::GmSolution(std::shared_ptr<GmModel> model, GmModelIdx gmModelIdx): gmModelIdx(gmModelIdx) {
    this->labeling = std::vector<int>(model->graph1.no_nodes, -1);
    this->old_labeling = std::vector<int>(model->graph1.no_nodes, -1);
}

GmSolution::GmSolution(std::shared_ptr<MgmModelBase> model, GmModelIdx gmModelIdx): gmModelIdx(gmModelIdx) {
    this->labeling = std::vector<int>(model->graph1_no_nodes[gmModelIdx], -1); 
    this->old_labeling = std::vector<int>(model->graph1_no_nodes[gmModelIdx], -1);
}

GmSolution::GmSolution(std::shared_ptr<MgmModelBase> model, GmModelIdx gmModelIdx, const double& energy): gmModelIdx(gmModelIdx), energy(energy){
    this->labeling = std::vector<int>(model->graph1_no_nodes[gmModelIdx], -1);
    this->old_labeling = std::vector<int>(model->graph1_no_nodes[gmModelIdx], -1);
}

bool GmSolution::is_active(AssignmentIdx assignment) const {
    return this->labeling[assignment.first] == assignment.second;
}

bool GmSolution::was_active(AssignmentIdx assignment) const {
    return this->old_labeling[assignment.first] == assignment.second;
}

double GmSolution::evaluate_gm_model(std::shared_ptr<GmModel> gmModel) const {
    double result = 0.0;

    // assignments
    int node = 0;
    for (const auto& label : this->labeling) {
        if (label >= 0) {
            if (gmModel->get_costs()->contains(node, label)) {
                result += gmModel->get_costs()->unary(node, label);
            }
            else {
                return INFINITY_COST;
            }
        }
        node++;
    }

    //edges
    for (const auto& [edge_idx, cost] : gmModel->get_costs()->all_edges()) {
        auto& a1 = edge_idx.first;
        auto& a2 = edge_idx.second;
        if (this->is_active(a1) && this->is_active(a2)) {
            result += cost;
        }
    }

    return result;
}

double GmSolution::evaluate_gm_model_and_subtract_old_labelling(std::shared_ptr<GmModel> gmModel) const {
    double result = 0.0;

    // assignments
    int node = 0;
    for (const auto& label : this->labeling) {
        if (label >= 0) {
            if (gmModel->get_costs()->contains(node, label)) {
                result += gmModel->get_costs()->unary(node, label);
            }
            else {
                return INFINITY_COST;
            }
        }
        node++;
    }
    node = 0;
    for (const auto& label : this->old_labeling) {
        if (label >= 0) {
            if (gmModel->get_costs()->contains(node, label)) {
                result -= gmModel->get_costs()->unary(node, label);
            }
            else {
                return INFINITY_COST;
            }
        }
        node++;
    }

    //edges
    for (const auto& [edge_idx, cost] : gmModel->get_costs()->all_edges()) {
        auto& a1 = edge_idx.first;
        auto& a2 = edge_idx.second;
        if (this->is_active(a1) && this->is_active(a2)) {
            result += cost;
        }
        if (this->was_active(a1) && this->was_active(a2)) {
            result -= cost;
        }
    }

    return result;
}

double GmSolution::evaluate() const {
    return this->evaluate_gm_model(this->model);
}

double GmSolution::evaluate(const std::shared_ptr<MgmModelBase> mgmModel) const {
    
    std::shared_ptr<GmModel> gmModel = mgmModel->get_gm_model(this->gmModelIdx); 
    return this->evaluate_gm_model(gmModel);
}

double GmSolution::evaluate_and_subtract_old_labelling(const std::shared_ptr<MgmModelBase> mgmModel) const {
    std::shared_ptr<GmModel> gmModel = mgmModel->get_gm_model(this->gmModelIdx); 
    return this->evaluate_gm_model_and_subtract_old_labelling(gmModel);
}

MgmSolution::MgmSolution(std::shared_ptr<MgmModelBase> model) {
    this->model = model;
    gmSolutions.reserve(model->model_keys.size());
    for (auto const& key: model->model_keys) {
        gmSolutions[key] = GmSolution(model, key);
    }
}

MgmSolution::MgmSolution(std::shared_ptr<MgmModelBase> model, MgmSolution last_solution) {
    this->model = model;
    gmSolutions.reserve(model->model_keys.size());

    for (auto const& key: model->model_keys) {
        gmSolutions[key] = GmSolution(model, key, last_solution.gmSolutions[key].get_energy());
    }
}

void MgmSolution::build_from(const CliqueTable& cliques)
{
    for (const auto& c : cliques) {
        for (const auto& [g1, n1] : c) {
            for (const auto& [g2, n2] : c) {
                if (g1 == g2) 
                    continue;
                if (g1 < g2) {
                    this->gmSolutions[GmModelIdx(g1,g2)].labeling[n1] = n2;
                }
                else {
                    this->gmSolutions[GmModelIdx(g2,g1)].labeling[n2] = n1;
                }
            }
        }
    }
}

void MgmSolution::build_from(const CliqueTable& cliques, const CliqueTable old_cliques) 
{
    for (const auto& c : cliques) {
        for (const auto& [g1, n1] : c) {
            for (const auto& [g2, n2] : c) {
                if (g1 == g2) 
                    continue;
                if (g1 < g2) {
                    this->gmSolutions[GmModelIdx(g1,g2)].labeling[n1] = n2;
                }
                else {
                    this->gmSolutions[GmModelIdx(g2,g1)].labeling[n2] = n1;
                }
            }
        }
    }
    for (const auto& c : old_cliques) {
        for (const auto& [g1, n1] : c) {
            for (const auto& [g2, n2] : c) {
                if (g1 == g2) 
                    continue;
                if (g1 < g2) {
                    this->gmSolutions[GmModelIdx(g1,g2)].old_labeling[n1] = n2;
                }
                else {
                    this->gmSolutions[GmModelIdx(g2,g1)].old_labeling[n2] = n1;
                }
            }
        }
    }
}

void MgmSolution::extend_solution(const CliqueTable& cliques, const int& new_graph_id, const std::vector<int>& current_graph_ids)
{
    for (const auto& c : cliques) {
        for (const auto& [g1, n1] : c) {
            for (const auto& [g2, n2] : c) {
                if (g1 == g2 or g2 != new_graph_id) 
                    continue;
                if (g1 < g2) {
                    this->gmSolutions[GmModelIdx(g1,g2)].labeling[n1] = n2;
                }
                else {
                    this->gmSolutions[GmModelIdx(g2,g1)].labeling[n2] = n1;
                }
            }
        }
    }
    for (const int& graph_id: current_graph_ids) {
        if (graph_id == new_graph_id) {
            continue;
        }
        if (graph_id < new_graph_id) {
            this->gmSolutions[GmModelIdx(graph_id, new_graph_id)].update_energy(this->model);
        } else {
            this->gmSolutions[GmModelIdx(new_graph_id, graph_id)].update_energy(this->model);
        }
    }
}

void MgmSolution::update_solution(const int& new_graph_id) {
    for (int graph_id = 0; graph_id < this->model->no_graphs; ++graph_id) {
        if (graph_id == new_graph_id) {
            continue;
        }
        if (graph_id < new_graph_id) {
            this->gmSolutions[GmModelIdx(graph_id, new_graph_id)].update_energy(this->model);
        } else {
            this->gmSolutions[GmModelIdx(new_graph_id, graph_id)].update_energy(this->model);
        }
    }
}

void MgmSolution::update_all_energies() {
    for (int left_graph_id = 0; left_graph_id < this->model->no_graphs; ++left_graph_id) {
        for (int right_graph_id = 0; right_graph_id < this->model->no_graphs; ++right_graph_id) {
            if (left_graph_id < right_graph_id) {
                this->gmSolutions[GmModelIdx(left_graph_id, right_graph_id)].update_energy(this->model);
            }
        }
    }
}

CliqueTable MgmSolution::export_cliquetable(){
    CliqueTable table(this->model->no_graphs);

    // 2d array to store clique_idx of every node
    // node_clique_idx[graph_id][node_id] = clique_idx of table
    std::vector<std::vector<int>> node_clique_idx;
    for (const auto& g : this->model->graphs) {
        node_clique_idx.emplace_back(g.no_nodes, -1);
    }
    
    for (int g1 = 0; g1 < this->model->no_graphs; g1++){
        for (int g2 = (g1 + 1); g2 < this->model->no_graphs; g2++) {
            GmModelIdx model_idx(g1,g2);

            if (this->gmSolutions.find(model_idx) == this->gmSolutions.end())
                continue;

            const auto& l = this->gmSolutions[model_idx].labeling;
            
            for (size_t node_id = 0; node_id < l.size(); node_id++) {
                const int & label = l[node_id];
                if (label < 0) 
                    continue;
                
                int& clique_idx_n1 = node_clique_idx[g1][node_id];
                int& clique_idx_n2 = node_clique_idx[g2][label];
                if (clique_idx_n1 < 0 && clique_idx_n2 < 0) {
                    CliqueTable::Clique c;
                    c[g1] = node_id;
                    c[g2] = label;
                    table.add_clique(c);
                    clique_idx_n1 = table.no_cliques-1;
                    clique_idx_n2 = table.no_cliques-1;
                }
                else if (clique_idx_n1 >= 0 && clique_idx_n2 < 0) {
                    table[clique_idx_n1][g2] = label;
                    clique_idx_n2 = clique_idx_n1;
                }
                else if (clique_idx_n1 < 0 && clique_idx_n2 >= 0) {
                    table[clique_idx_n2][g1] = node_id;
                    clique_idx_n1 = clique_idx_n2;
                }
                else if (clique_idx_n1 != clique_idx_n2) {
                    throw std::logic_error("Cycle inconsistent labeling given. Nodes matched two each other implied to reside in different cliques because of previously parsed matchings.");
                }
            }
        }
    }
    
    // Add all remaining, unmatched nodes in a clique of their own.
    for (size_t graph_id = 0; graph_id < node_clique_idx.size(); graph_id++) {
        for (size_t node_id = 0; node_id < node_clique_idx[graph_id].size(); node_id++) {
            if (node_clique_idx[graph_id][node_id] >= 0)
                continue;
            CliqueTable::Clique c;
            c[graph_id] = node_id;
            table.add_clique(c);
        }
    }

    #ifndef DEBUG
    // VERIFIES THAT CLIQUES WERE PARSED CORRECTLY
    std::vector<int> graph_ids(this->model->no_graphs);
    std::iota(graph_ids.begin(), graph_ids.end(), 0);
    
    CliqueManager cm(graph_ids, model);
    cm.reconstruct_from(table);

    for (const auto& [model_idx, gm_solution] : this->gmSolutions) {
        const auto& l = gm_solution.labeling;
        for (size_t node_id = 0; node_id < l.size(); node_id++) {
            const int & label = l[node_id];
            if (label < 0)
                continue;
            assert(cm.clique_idx(model_idx.first, node_id) == cm.clique_idx(model_idx.second, label));
        }
    }
    #endif

    return table;
}

double MgmSolution::evaluate() const {
    double result = 0.0;
    for (const auto& m : this->gmSolutions) {
        // result += m.second.evaluate(this->model);
        result += m.second.get_energy();
    }
    return result;
}

double MgmSolution::evaluate_starting_from_old_energy(double old_energy, const int& graph_id) const {
    for (int idx = 0; idx < this->model->no_graphs; ++idx) {
        if (graph_id < idx) {
            old_energy += this->gmSolutions.at(GmModelIdx(graph_id, idx)).evaluate_and_subtract_old_labelling(this->model);
        } else if (graph_id > idx) {
            old_energy += this->gmSolutions.at(GmModelIdx(idx, graph_id)).evaluate_and_subtract_old_labelling(this->model);
        }
    }
    return old_energy;
}

bool MgmSolution::is_cycle_consistent() const{
    return true;
}

}