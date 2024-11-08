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

class GmModel{
    public:
        GmModel() {};
        GmModel(Graph g1, Graph g2, int no_assignments, int no_edges);
        Graph graph1;
        Graph graph2;

        int no_assignments;
        int no_edges;

        void add_assignment(int assignment_id, int node1, int node2, double cost);

        // both valid alternatives.
        void add_edge(int assignment1, int assigment2, double cost);
        void add_edge(int assignment1_node1, int assignment1_node2, int assignment2_node1, int assignment2_node2, double cost);

        std::vector<AssignmentIdx> assignment_list;
        std::vector<std::vector<int>> assignments_left;
        std::vector<std::vector<int>> assignments_right;
        std::unique_ptr<CostMap> costs;

        template <class Archive>
        void serialize(Archive& archive) {
            archive(
                this->assignment_list, this->assignments_left, this->assignments_right, this->costs,
                this->graph1, this->graph2, this->no_assignments, this->no_edges
                );
        }
        void serialize_to_binary(std::string& result_string) const;
        void deserialize_from_binary(std::string& serialized_model);
        
};

void serialize_to_binary(std::string& result_string, std::shared_ptr<GmModel> gmModel);
void deserialize_serialized_model(std::string& serialized_model, std::shared_ptr<GmModel> model);

class MgmModelBase {
    public: 
        virtual ~MgmModelBase() = default;
        virtual void save_gm_model(GmModel& gm_model, const GmModelIdx& idx) = 0;
        virtual std::shared_ptr<GmModel> get_gm_model(const GmModelIdx& idx) = 0;

        int no_graphs;
        std::vector<Graph> graphs;
        
        std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash> models;
        std::vector<GmModelIdx> model_keys;
    
};

class MgmModel: public MgmModelBase{
    public:
        void save_gm_model(GmModel& gm_model, const GmModelIdx& idx);
        std::shared_ptr<GmModel> get_gm_model(const GmModelIdx& idx);
        
        MgmModel();
};

class SqlMgmModel: public MgmModelBase {
    public:
        SqlMgmModel();

        void save_gm_model(GmModel& gm_model, const GmModelIdx& idx);
        std::shared_ptr<GmModel> get_gm_model(const GmModelIdx& idx);
        
        void save_model_to_db(GmModel& gm_model, const GmModelIdx& idx);
        std::shared_ptr<GmModel> read_model_from_db(const GmModelIdx& idx);

        // Rule of five
        ~SqlMgmModel();
        SqlMgmModel(const SqlMgmModel& other) = default;             // Copy constructor maybe need multithreading flag for this
        SqlMgmModel& operator=(const SqlMgmModel& other) = default;  // Copy assignment operator
        SqlMgmModel(SqlMgmModel&& other);         // Move constructor
        SqlMgmModel& operator=(SqlMgmModel&& other);

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
        const int number_of_cached_models = 5;
};

class RocksdbMgmModel: public MgmModelBase {
    public:
        RocksdbMgmModel();
        void save_gm_model(GmModel& gm_model, const GmModelIdx& idx);
        std::shared_ptr<GmModel> get_gm_model(const GmModelIdx& idx);

        ~RocksdbMgmModel();
    
    private:
        void open_db();
        std::string convert_idx_into_string(const GmModelIdx& idx) const;

        rocksdb::DB* db;
        rocksdb::WriteOptions write_options;
        rocksdb::ReadOptions read_options;
};
}
#endif