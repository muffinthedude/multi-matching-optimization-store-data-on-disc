#include <algorithm>
#include <cassert>
#include <stdexcept>

#include <spdlog/spdlog.h>
#include <fmt/ranges.h> // print vector

#include "solver_mgm.hpp"
#include "random_singleton.hpp"
#include "solution.hpp"

#include "solver_local_search.hpp"
namespace mgm
{

    LocalSearcher::LocalSearcher(CliqueManager state, std::vector<int> search_order, std::shared_ptr<MgmModel> model)
        : state(state), search_order(search_order), model(model) {
        auto sol = MgmSolution(model);
        sol.build_from(state.cliques);

        this->current_energy = sol.evaluate();
    };

    void LocalSearcher::search() {
        assert(this->search_order.size() > 0); // Search order was not set
        spdlog::info("Running local search.");

        while (!this->should_stop()) {
            this->current_step++;
            this->previous_energy = this->current_energy;

            spdlog::info("Iteration {}. Current energy: {}", this->current_step, this->current_energy);

            this->iterate();

            spdlog::info("Finished iteration {}\n", this->current_step);
        }
    }

    MgmSolution LocalSearcher::export_solution() {
        spdlog::info("Exporting solution...");
        MgmSolution sol(this->model);
        sol.build_from(this->state.cliques);
        
        return sol;
    }

    void LocalSearcher::iterate() {
        int idx = 1;

        for (const auto& graph_id : this->search_order) {
            spdlog::info("Resolving for graph {} (step {}/{})", graph_id, idx, this->search_order.size());

            auto managers = details::split(this->state, graph_id, (*this->model));

            GmSolution sol              = details::match(managers.first, managers.second, (*this->model));
            CliqueManager new_manager   = details::merge(managers.first, managers.second, sol, (*this->model));

            // check if improved
            auto mgm_sol = MgmSolution(model);
            mgm_sol.build_from(new_manager.cliques);
            double energy = mgm_sol.evaluate();

            if (energy < this->current_energy) { 
                this->state = new_manager;
                this->current_energy = energy;
                spdlog::info("Better solution found. Previous energy: {} ---> Current energy: {}", this->previous_energy, this->current_energy);
            }
            else {
                spdlog::info("Worse solution(Energy: {}) after rematch. Reversing.\n", energy);
            }

            idx++;
        }
    }

    bool LocalSearcher::should_stop() {
        // check stopping criteria
        if (this->stopping_criteria.abstol >= 0)
        {
            if ((previous_energy - current_energy) <= this->stopping_criteria.abstol)
            {
                spdlog::info("Stopping - Absolute increase smaller than defined tolerance.\n");
                return true;
            }
        }
        if (this->stopping_criteria.reltol >= 0)
        {
            throw std::logic_error("Not Implementd");
        }
        if (this->stopping_criteria.max_steps >= 0)
        {
            if (this->current_step > this->stopping_criteria.max_steps) {
                spdlog::info("Stopping - Maximum number of iterations reached.\n");
                return true;
            }
        }

        return false;
    }

    LocalSearcherParallel::LocalSearcherParallel(CliqueManager state, std::shared_ptr<MgmModel> model)
    : state(state), model(model) {
            auto sol = MgmSolution(model);
            sol.build_from(state.cliques);

            this->current_energy = sol.evaluate();
            this->matchings.resize(state.graph_ids.size());
        }

    //FIXME: Is (nearly) same as in LocalSearcher
    void LocalSearcherParallel::search(){
        spdlog::info("Running parallel local search.");

        while (!this->should_stop()) {
            this->current_step++;
            this->previous_energy = this->current_energy;

            spdlog::info("Iteration {}. Current energy: {}", this->current_step, this->current_energy);

            this->iterate();

            spdlog::info("Finished iteration {}\n", this->current_step);
        }
    }

    //FIXME: Is same as in LocalSearcher
    MgmSolution LocalSearcherParallel::export_solution()
    {
        spdlog::info("Exporting solution...");
        MgmSolution sol(this->model);
        sol.build_from(this->state.cliques);
        
        return sol;
    }

    //FIXME: Is same as in LocalSearcher
    bool LocalSearcherParallel::should_stop() {
        // check stopping criteria
        if (this->stopping_criteria.abstol >= 0)
        {
            if ((previous_energy - current_energy) <= this->stopping_criteria.abstol)
            {
                spdlog::info("Stopping - Absolute increase smaller than defined tolerance.\n");
                return true;
            }
        }
        if (this->stopping_criteria.reltol >= 0)
        {
            throw std::logic_error("Not Implementd");
        }
        if (this->stopping_criteria.max_steps >= 0)
        {
            if (this->current_step > this->stopping_criteria.max_steps) {
                spdlog::info("Stopping - Maximum number of iterations reached.\n");
                return true;
            }
        }

        return false;
    }

    void LocalSearcherParallel::iterate()
    {

        // Solve local search for each graph separately.
        // FIXME: Do this in fact in parallel
        for (const auto& graph_id : this->state.graph_ids) {
            auto managers = details::split(this->state, graph_id, (*this->model));

            GmSolution sol              = details::match(managers.first, managers.second, (*this->model));
            CliqueManager new_manager   = details::merge(managers.first, managers.second, sol, (*this->model));

            auto mgm_sol = MgmSolution(model);
            mgm_sol.build_from(new_manager.cliques);
            double energy = mgm_sol.evaluate();

            this->matchings[graph_id] = std::make_tuple(graph_id, sol, new_manager, energy);
        }
        
        // sort and check for best solution
        static auto lambda_sort_high_energy = [](auto& a, auto& b) { return std::get<3>(a) < std::get<3>(b); };
        std::sort(this->matchings.begin(), this->matchings.end(), lambda_sort_high_energy);
        
        double best_energy      = std::get<3>(this->matchings[0]);
        if (best_energy >= this->current_energy) {
            spdlog::info("No new solution found");
            return;
        }

        // better solution
        this->state             = std::get<2>(this->matchings[0]);

        // readd each graph
        int no_better_solutions = 1;
        int no_graphs_merged = 1;

        for (auto it=this->matchings.begin() + 1 ; it != this->matchings.end(); it++) {
            double& e = std::get<3>(*it);

            // only readd, if energy improved.
            if (e >= this->current_energy) {
                continue;
            }
            no_better_solutions++;

            // Merge into current state.
            auto& graph_id = std::get<0>(*it);
            auto& sol = std::get<1>(*it);

            auto managers               = details::split(this->state, graph_id, (*this->model));
            CliqueManager new_manager   = details::merge(managers.first, managers.second, sol, (*this->model));

            // Overwrite solution, if improved.
            auto mgm_sol = MgmSolution(model);
            mgm_sol.build_from(new_manager.cliques);
            double energy = mgm_sol.evaluate();

            if (energy < best_energy) { 
                this->state = new_manager;
                best_energy = energy;

                no_graphs_merged++;
            }
        }
        this->current_energy    = best_energy;
        spdlog::info("Better solution found. Previous energy: {} ---> Current energy: {}", this->previous_energy, this->current_energy);
        spdlog::info("Number of better solutions {}. Of which were merged: {}\n", no_better_solutions, no_graphs_merged);
    }
}