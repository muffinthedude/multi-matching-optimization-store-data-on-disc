#include "solver_mgm.hpp"
#include "multigraph.hpp"

namespace mgm {

constexpr double INFINITY_COST = 1e99;

//FIXME: This needs a better name.
class LocalSearcher {
    public:
        struct StoppingCriteria {
            int max_steps = 10000;
            double abstol = 0.0;
            double reltol = -1.0;
        };
        
        LocalSearcher(CliqueManager state, std::shared_ptr<MgmModelBase> model);
        LocalSearcher(CliqueManager state, std::vector<int> search_order, std::shared_ptr<MgmModelBase> model);

        StoppingCriteria stopping_criteria;
        bool search();
        
        CliqueManager export_CliqueManager();
        CliqueTable export_cliquetable();
        MgmSolution export_solution();

    private:
        int current_step = 0;
        double previous_energy = INFINITY_COST;
        double current_energy = 0.0;

        void iterate();
        void iteration_step(const int& graph_id, const int& idx);
        CliqueManager state;
        std::vector<int> search_order;
        std::shared_ptr<MgmModelBase> model;

        int last_improved_graph = -1;
        bool should_stop();
};

//FIXME: This needs a better name.
class LocalSearcherParallel {
    public:
        struct StoppingCriteria {
            int max_steps = 10000;
            double abstol = 0.0;
            double reltol = -1.0;
        };
        
        LocalSearcherParallel(CliqueManager state, std::shared_ptr<MgmModelBase> model, bool merge_all=true);

        StoppingCriteria stopping_criteria;
        bool search();
        
        MgmSolution export_solution();
        CliqueTable export_cliquetable();
        CliqueManager export_CliqueManager();

    private:
        int current_step = 0;
        double previous_energy = INFINITY_COST;
        double current_energy = 0.0;

        void iterate();
        CliqueManager state;

        using GraphID = int;
        std::vector<std::tuple<GraphID, GmSolution, CliqueManager, double>> matchings;

        std::shared_ptr<MgmModelBase> model;
        bool merge_all;

        bool should_stop();
};

namespace details {
    // Splits off graph [graph_id] from manager
    // Does not remove any potential empty cliques, to ensure their order and index remain valid. (See Parallel Local Searcher)
    std::pair<CliqueManager, CliqueManager> split_unpruned(const CliqueManager& manager, int graph_id, const std::shared_ptr<MgmModelBase> model);
}
}