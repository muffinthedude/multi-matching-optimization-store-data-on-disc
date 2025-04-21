#ifndef LIBMGM_SOLUTION_HPP
#define LIBMGM_SOLUTION_HPP

#include <unordered_map>
#include <vector>
#include <memory>
#include <utility>

#include "cliques.hpp"
#include "multigraph.hpp"

namespace mgm {

class GmSolution {
    public:
        GmSolution() = default;
        
        std::vector<int> labeling;
        std::vector<int> old_labeling;

        // For use with models saved in memory 
        GmSolution(std::shared_ptr<GmModel> model);
        double evaluate() const;
        std::shared_ptr<GmModel> model;

        // For use with models saved on disc
        GmSolution(std::shared_ptr<GmModel> model, GmModelIdx gmModelIdx);
        GmSolution(std::shared_ptr<MgmModelBase> model, GmModelIdx gmModelIdx);
        GmSolution(std::shared_ptr<MgmModelBase> model, GmModelIdx gmModelIdx, const double& energy);
        double evaluate(const std::shared_ptr<MgmModelBase> mgmModel) const;
        double evaluate_and_subtract_old_labelling(const std::shared_ptr<MgmModelBase> mgmModel) const;
        GmModelIdx gmModelIdx;

        void update_energy() {this->energy = this->evaluate();};
        void update_energy(const std::shared_ptr<MgmModelBase> model) {this->energy = this->evaluate(model);};

        double get_energy() const {return this->energy;};

    private:
        double energy = 0.0;
        double old_energy = 0.0;
        bool is_active(AssignmentIdx assignment) const;
        bool was_active(AssignmentIdx assignment) const;
        double evaluate_gm_model(std::shared_ptr<GmModel> gmModel) const;
        double evaluate_gm_model_and_subtract_old_labelling(std::shared_ptr<GmModel> gmModel) const;
};

class MgmSolution {
    public:
        MgmSolution(std::shared_ptr<MgmModelBase> model);
        MgmSolution(std::shared_ptr<MgmModelBase> model, MgmSolution last_solution);

        std::unordered_map<GmModelIdx, GmSolution, GmModelIdxHash> gmSolutions;
        std::shared_ptr<MgmModelBase> model;

        void build_from(const CliqueTable& cliques);
        void build_from(const CliqueTable& cliques, const CliqueTable old_cliques);
        void extend_solution(const CliqueTable& cliques, const int& new_graph_id, const std::vector<int>& current_graph_ids);
        void update_solution(const int& new_graph_id);
        void update_all_energies();
        CliqueTable export_cliquetable();

        double evaluate() const;
        double evaluate_starting_from_old_energy(double old_energy, const int& graph_id) const;
        bool is_cycle_consistent() const;
};

}
#endif