#ifndef LIBMGM_SYNCHRONIZATION_HPP
#define LIBMGM_SYNCHRONIZATION_HPP

#include "solution.hpp"
#include "multigraph.hpp"
#include "io_utils.hpp"

namespace mgm {

std::shared_ptr<MgmModelBase> build_sync_problem(std::shared_ptr<MgmModelBase> model, MgmSolution& solution, bool feasible=true, io::disc_save_mode save_mode=io::disc_save_mode::no);

namespace details {

std::shared_ptr<GmModelBase>  create_feasible_sync_model(std::shared_ptr<GmModelBase> model, GmSolution& solution, io::disc_save_mode save_mode);
std::shared_ptr<GmModelBase>  create_infeasible_sync_model(std::shared_ptr<GmModelBase> model, GmSolution& solution, io::disc_save_mode save_mode);

}
}

#endif