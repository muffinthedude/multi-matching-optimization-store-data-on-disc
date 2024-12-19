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
        GmSolution(std::shared_ptr<GmModelBase> model);
        double evaluate() const;
        std::shared_ptr<GmModelBase> model;

        // For use with models saved on disc
        GmSolution(std::shared_ptr<GmModelBase> model, GmModelIdx gmModelIdx);
        GmSolution(std::shared_ptr<MgmModelBase> model, GmModelIdx gmModelIdx);
        double evaluate(const std::shared_ptr<MgmModelBase> mgmModel) const;
        double evaluate_and_subtract_old_labelling(const std::shared_ptr<MgmModelBase> mgmModel) const;
        GmModelIdx gmModelIdx;

    private:
        bool is_active(AssignmentIdx assignment) const;
        bool was_active(AssignmentIdx assignment) const;
        double evaluate_gm_model(std::shared_ptr<GmModelBase> gmModel) const;
        double evaluate_gm_model_and_subtract_old_labelling(std::shared_ptr<GmModelBase> gmModel) const;
};

class MgmSolution {
    public:
        MgmSolution(std::shared_ptr<MgmModelBase> model);

        std::unordered_map<GmModelIdx, GmSolution, GmModelIdxHash> gmSolutions;
        std::shared_ptr<MgmModelBase> model;

        void build_from(const CliqueTable& cliques);
        void build_from(const CliqueTable& cliques, const CliqueTable old_cliques);
        CliqueTable export_cliquetable();

        double evaluate() const;
        double evaluate_starting_from_old_energy(double old_energy, const int& graph_id) const;
        bool is_cycle_consistent() const;
};

}
#endif