#include <iostream>

#include <spdlog/spdlog.h>

#include "solution.hpp"
#include "multigraph.hpp"
#include "synchronization.hpp"

namespace mgm {
std::shared_ptr<MgmModelBase> build_sync_problem(std::shared_ptr<MgmModelBase> model, MgmSolution &solution, bool feasible, io::disc_save_mode save_mode) {
    spdlog::info("Building synchronization problem from given model and solution.");

    std::shared_ptr<MgmModelBase> sync_model = std::make_shared<MgmModel>();

    switch (save_mode) {
        case io::disc_save_mode::no:
            sync_model = std::make_shared<MgmModel>();
        case io::disc_save_mode::sql:
            sync_model = std::make_shared<SqlMgmModel>();
        case io::disc_save_mode::rocksdb:
            sync_model = std::make_shared<RocksdbMgmModel>();
        case io::disc_save_mode::stxxl:
            sync_model = std::make_shared<StxxlMgmModel>();
        default:
            sync_model = std::make_shared<MgmModel>();
    }

    sync_model->no_graphs = model->no_graphs;
    sync_model->graphs = model->graphs;

    int i = 0;
    for (const auto& key : model->model_keys) {  // TODO: this is not working for SqlMgmModel
        std::shared_ptr<GmModelBase> gm_model = model->get_gm_model(key);

        // Progress, prints iterations on terminal.
        i++;
        std::cout << i << "/" << model->model_keys.size() << " \r";
        std::cout.flush();

        std::shared_ptr<GmModelBase> sync_gm_model;

        if (feasible) {
            // only allow assignments that are present
            sync_gm_model = details::create_feasible_sync_model(gm_model, solution.gmSolutions[key], save_mode);
        }
        else {
            // allow all assignments
            sync_gm_model = details::create_infeasible_sync_model(gm_model, solution.gmSolutions[key], save_mode);
        }
        sync_model->save_gm_model(sync_gm_model, key);
    }

    return sync_model;
}


namespace details {
std::shared_ptr<GmModelBase>  create_feasible_sync_model(std::shared_ptr<GmModelBase> model, GmSolution& solution, io::disc_save_mode save_mode) {

    std::shared_ptr<GmModelBase> sync_model;
    switch (save_mode) {
        case(io::disc_save_mode::stxxl):
            sync_model = std::make_shared<StxxlGmModel>(model->graph1, model->graph2, model->no_assignments, 0);
        default:
            sync_model = std::make_shared<GmModel>(model->graph1, model->graph2, model->no_assignments, 0);
    }
    
    // Copy assignments
    // TODO: This is unnecessarily inefficient. Cost datastructure might be too restricted.
    size_t i = 0;
    for (const auto& idx : model->assignment_list) {
        sync_model->add_assignment(i, idx.first, idx.second, 0);
        i++;
    }
    
    // set labeled assignments
    for (size_t i = 0; i < solution.labeling.size(); ++i) {
        if (solution.labeling[i] == -1)
            continue;
        
        sync_model->get_costs()->set_unary(i, solution.labeling[i], -1);
    }

    return sync_model;
}

std::shared_ptr<GmModelBase>  create_infeasible_sync_model(std::shared_ptr<GmModelBase> model, GmSolution& solution, io::disc_save_mode save_mode) {
    int no_assignments = model->graph1.no_nodes * model->graph2.no_nodes;
    std::shared_ptr<GmModelBase> sync_model;
    switch (save_mode) {
        case(io::disc_save_mode::stxxl):
            sync_model = std::make_shared<StxxlGmModel>(model->graph1, model->graph2, model->no_assignments, 0);
        default:
            sync_model = std::make_shared<GmModel>(model->graph1, model->graph2, model->no_assignments, 0);
    }

    // Initialize all costs to 0
    int idx = 0;
    for (auto i = 0; i  < model->graph1.no_nodes; i++) {
        for (auto j = 0; j  < model->graph2.no_nodes; j++) {
            sync_model->add_assignment(idx, i, j, 0);
            idx++;
        }
    }

    // set labeled assignments
    for (size_t i = 0; i < solution.labeling.size(); ++i) {
        if (solution.labeling[i] == -1)
            continue;
        
        sync_model->get_costs()->set_unary(i, solution.labeling[i], -1);
    }

    return sync_model;
};

}}