#ifndef LIBMGM_MULTIGRAPH_HPP
#define LIBMGM_MULTIGRAPH_HPP

// #include <unordered_map>
#include <vector>
#include<queue>
#include <string>
#include <memory>
#include <utility>
#include <sqlite3.h>
#include <rocksdb/db.h>
#include <cereal/types/memory.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/types/base_class.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h> 

#include "costs.hpp"

namespace mgm {

typedef std::pair<int,int> GmModelIdx;

struct GmModelIdxHash {
    std::size_t operator()(GmModelIdx const& input) const noexcept {
        size_t seed = 0;
        boost_hash_combine(seed, input.first);
        boost_hash_combine(seed, input.second);
        return seed;
    }
};

class Graph {
    public:
        Graph() {};
        Graph(int id, int no_nodes);

        int id;
        int no_nodes;
        
        template <class Archive>
        void serialize(Archive& archive) {
            archive(this->id, this->no_nodes);
        }
};

class GmModel {
    public:
        GmModel() {};
        GmModel(Graph g1, Graph g2, int no_assignments, int no_edges);

        Graph graph1;
        Graph graph2;

        int no_assignments;
        int no_edges;

        std::shared_ptr<CostMap> get_costs();

        void add_assignment(int assignment_id, int node1, int node2, double cost);

        // both valid alternatives.
        void add_edge(int assignment1, int assigment2, double cost);
        void add_edge(int assignment1_node1, int assignment1_node2, int assignment2_node1, int assignment2_node2, double cost);

        std::shared_ptr<CostMap> costs;

        template <class Archive>
        void serialize(Archive& archive) {
            archive(
                this->assignment_list, this->assignments_left, this->assignments_right, 
                this->graph1, this->graph2, this->no_assignments, this->no_edges, this->costs
                );
        }

        std::vector<AssignmentIdx> assignment_list;
        std::vector<std::vector<int>> assignments_left;
        std::vector<std::vector<int>> assignments_right;

        void serialize_to_binary(std::string& result_string) const;
        void deserialize_from_binary(std::string& serialized_model);

        int estimate_memory_consumption();
        
};

void serialize_to_binary(std::string& result_string, std::shared_ptr<GmModel> gmModel);
std::shared_ptr<GmModel> deserialize_serialized_model(std::string& serialized_model);

class MgmModelBase {
    public: 
        virtual ~MgmModelBase() = default;
        virtual void save_gm_model(std::shared_ptr<GmModel> gm_model, const GmModelIdx& idx) = 0;
        virtual std::shared_ptr<GmModel> get_gm_model(const GmModelIdx& idx) = 0;
        virtual void bulk_read_to_load_cache(std::vector<GmModelIdx> keys) = 0;
        virtual void bulk_read_to_load_cache(const int& model_id) = 0;
        virtual void swap_caches() = 0;
        virtual void build_caches(long long memory_limit, long long max_memory_model);

        int no_graphs;
        std::vector<Graph> graphs;
        std::unordered_map<GmModelIdx, int, GmModelIdxHash> graph1_no_nodes; 
        
        std::vector<GmModelIdx> model_keys;  // maybe use other data structure here to make sure same key is not saved multiple times (set?)

        std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash> static_cache;

        bool bulk_load_mode = false;
        int number_of_cached_models;
        bool paralel_loading_mode = false;
    protected:
        void distribute_models_in_static_cache_while_in_parallel_mode(int number_of_models_in_static_cache);
        void distribute_models_iteration(std::unordered_map<int, std::unordered_set<int>>& models_count, const int first, int iteration, const int& original_iteration);
    
};

class MgmModel: public MgmModelBase{
    public:
        void save_gm_model(std::shared_ptr<GmModel> gm_model, const GmModelIdx& idx);
        std::shared_ptr<GmModel> get_gm_model(const GmModelIdx& idx);
        void bulk_read_to_load_cache(std::vector<GmModelIdx> keys) {};
        void bulk_read_to_load_cache(const int& model_id) {};
        virtual void swap_caches() {};
        void build_caches(long long memory_limit, long long max_memory_model) override {};
        
        MgmModel();

        std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash> models;
};

class SqlMgmModel: public MgmModelBase {
    public:
        SqlMgmModel();

        void save_gm_model(std::shared_ptr<GmModel> gm_model, const GmModelIdx& idx) override;
        std::shared_ptr<GmModel> get_gm_model(const GmModelIdx& idx);
        
        void save_model_to_db(std::shared_ptr<GmModel> gm_model, const GmModelIdx& idx);
        std::shared_ptr<GmModel> read_model_from_db(const GmModelIdx& idx);
        void bulk_read_to_load_cache(std::vector<GmModelIdx> keys);
        void bulk_read_to_load_cache(const int& model_id);

        std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash> cache;  // cache used when bulk loads and parallel caching is not used

        // for parallel caching
        std::shared_ptr<std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash>> load_cache;
        std::shared_ptr<std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash>> process_cache;
        void swap_caches() {};

        // Rule of five
        ~SqlMgmModel();
        SqlMgmModel(const SqlMgmModel& other) = default;             // Copy constructor maybe need multithreading flag for this
        SqlMgmModel& operator=(const SqlMgmModel& other) = default;  // Copy assignment operator
        SqlMgmModel(SqlMgmModel&& other);         // Move constructor
        SqlMgmModel& operator=(SqlMgmModel&& other);
        int number_of_cached_models = 120;
    private:
        sqlite3* open_db();
        void create_table();
        void set_up_write_statement();
        void set_up_read_statement();
        void delete_table();
        // void deserialize_serialized_model(std::string& serialized_model, GmModel& model);

        sqlite3* db;
        sqlite3_stmt* insert_stmt;
        sqlite3_stmt* read_stmt;
        std::queue<GmModelIdx> cache_queue;
        
};

class RocksdbMgmModel: public MgmModelBase {
    public:
        RocksdbMgmModel();
        void save_gm_model(std::shared_ptr<GmModel> gm_model, const GmModelIdx& idx);
        std::shared_ptr<GmModel> get_gm_model(const GmModelIdx& idx);
        void bulk_read_to_load_cache(std::vector<GmModelIdx> keys);
        void bulk_read_to_load_cache(const int& model_id);
        void swap_caches();

        ~RocksdbMgmModel();

        std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash> cache;
        std::shared_ptr<std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash>> load_cache;
        std::shared_ptr<std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash>> process_cache;
        
        int number_of_cached_models = 120;
    private:
        void open_db();
        std::string convert_idx_into_string(const GmModelIdx& idx) const;

        rocksdb::DB* db;
        rocksdb::WriteOptions write_options;
        rocksdb::ReadOptions read_options;

        std::queue<GmModelIdx> cache_queue;
        
};

}

CEREAL_REGISTER_TYPE(mgm::SqlMgmModel)
CEREAL_REGISTER_POLYMORPHIC_RELATION(mgm::MgmModelBase, mgm::SqlMgmModel)

CEREAL_REGISTER_TYPE(mgm::RocksdbMgmModel)
CEREAL_REGISTER_POLYMORPHIC_RELATION(mgm::MgmModelBase, mgm::RocksdbMgmModel)
#endif