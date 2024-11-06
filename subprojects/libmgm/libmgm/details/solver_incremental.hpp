#ifndef LIBMGM_SOLVER_INCREMENTAL_HPP
#define LIBMGM_SOLVER_INCREMENTAL_HPP

#include "solver_mgm.hpp"

namespace mgm {
    
class IncrementalGenerator : public SequentialGenerator {
    public:
        IncrementalGenerator(int subset_size, std::shared_ptr<MgmModelBase> model);
        
        void generate() override;

    private:
        int subset_size;

        void improve();
};

}

#endif