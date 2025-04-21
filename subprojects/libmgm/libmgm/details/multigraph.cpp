#include "multigraph.hpp"

#include <utility>
#include <cassert>
#include <fstream>
#include <cstdlib>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/compression_type.h"

namespace mgm {
    
Graph::Graph(int id, int no_nodes) : id(id), no_nodes(no_nodes) {};

GmModel::GmModel(Graph g1, Graph g2, int no_assignments, int no_edges): 
    graph1(g1), 
    graph2(g2),
    no_assignments(no_assignments),
    no_edges(no_edges)
    {
    this->costs = std::make_unique<CostMap>(no_assignments, no_edges);
    this->assignment_list.reserve(no_assignments);

    //FIXME: Number of elements for assignments_left and assignments_right is unclear.
    // Loading assignments without reserving space leads to (avoidable?) reallocations. 
    this->assignments_left  = std::vector<std::vector<int>>(g1.no_nodes);
    this->assignments_right = std::vector<std::vector<int>>(g2.no_nodes);
}

std::shared_ptr<CostMap> GmModel::get_costs() {
    return this->costs;
}

void GmModel::add_assignment([[maybe_unused]] int assignment_id, int node1, int node2, double cost) {
    assert ((size_t) assignment_id == this->assignment_list.size());

    (void) this->assignment_list.emplace_back(node1, node2);

    this->costs->set_unary(node1, node2, cost);
    this->assignments_left[node1].push_back(node2);
    this->assignments_right[node2].push_back(node1);
}

void GmModel::add_edge(int assignment1, int assignment2, double cost) {
    auto& a1 = this->assignment_list[assignment1];
    auto& a2 = this->assignment_list[assignment2];

    this->add_edge(a1.first, a1.second, a2.first, a2.second, cost);
}

void GmModel::add_edge(int assignment1_node1, int assignment1_node2, int assignment2_node1, int assignment2_node2, double cost) {
    this->costs->set_pairwise(assignment1_node1, assignment1_node2, assignment2_node1, assignment2_node2, cost);
    //this->costs->set_pairwise(a2.first, a2.second, a1.first, a1.second, cost); //FIXME: RAM overhead. Avoids sorting later though.
}

void GmModel::serialize_to_binary(std::string& result_string) const {
    spdlog::info("Serializing Model...");
    std::ostringstream output_stream;
        {
            
            cereal::BinaryOutputArchive OArchive(output_stream);
            OArchive(*this);
            
        }
    result_string = output_stream.str();
    spdlog::info("Serialized Model!");
}

int GmModel::estimate_memory_consumption() {
    int map_memory_consumption = 112 + this->assignment_list.size() * 8;
    for (auto& sub_list: this->assignments_left) {
        map_memory_consumption += sub_list.size() * 8 + 24;
    }
    for (auto& sub_list: this->assignments_right) {
        map_memory_consumption += sub_list.size() * 8 + 24;
    }

    map_memory_consumption += this->costs->all_assignments_size() * (16 + sizeof(void*));
    map_memory_consumption += 56 + this->costs->all_assignments_num_buckets() * 8;
    map_memory_consumption += this->costs->all_edges_size() * (16 + sizeof(void*)) + 56 + this->costs->all_edges_num_buckets() * 8; 

    return map_memory_consumption;
}

// Serialization utils

void serialize_to_binary(std::string& result_string, std::shared_ptr<GmModel> gmModel) {
    spdlog::info("Serializing Model...");
    std::ostringstream output_stream;
        {
            
            cereal::BinaryOutputArchive OArchive(output_stream);
            OArchive(gmModel);
            
        }
    result_string = output_stream.str();
    spdlog::info("Serialized Model!");
}

std::shared_ptr<GmModel> deserialize_serialized_model(std::string& serialized_model) {
    spdlog::info("Deserializing Model...");
    std::shared_ptr<GmModel> gmModel;
    {
        std::istringstream iss(serialized_model);
        cereal::BinaryInputArchive iarchive(iss);
        iarchive(gmModel); 
    }
    spdlog::info("Deserialized Model");
    return gmModel;
}

// MgmModelBase

void MgmModelBase::build_caches(long long memory_limit, long long max_memory_model) {
    memory_limit = memory_limit - 2 * max_memory_model;
    int max_number_of_models_in_memory = memory_limit / max_memory_model;
    if (this->paralel_loading_mode) {
        if (max_number_of_models_in_memory < 2 * (this->no_graphs - 1)) {
            this->paralel_loading_mode = false;
            spdlog::warn("Not enough RAM for efficicient parallel mode. Parallel Mode turned off!");  // not enough space to use parallel loading efficiently (Warning?)
        } else {
            max_number_of_models_in_memory -= 2 * (this->no_graphs - 1);
            this->distribute_models_in_static_cache_while_in_parallel_mode(max_number_of_models_in_memory);
        }
    }
    if (!this->paralel_loading_mode) {
        int number_of_models_in_static_cache = this->static_cache.size();
        if (max_number_of_models_in_memory < this->no_graphs - 1) {
            this->static_cache = std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash>();
            spdlog::warn("Not enough RAM for efficicient seqseq mode. Programm will slow down considerably!");
        } else if (max_number_of_models_in_memory >= this->model_keys.size() ){
            if (number_of_models_in_static_cache < this->model_keys.size()) {
                for (const GmModelIdx& idx: this->model_keys) {
                    if (this->static_cache.find(idx) != this->static_cache.end()) {
                        this->static_cache[idx] = this->get_gm_model(idx);
                    }
                }
            }  // load all models

        } else if (max_number_of_models_in_memory - number_of_models_in_static_cache <  this->no_graphs - 1){
            auto static_cache_it = this->static_cache.begin();
            while (max_number_of_models_in_memory - number_of_models_in_static_cache <  this->no_graphs - 1) {
                this->save_gm_model(static_cache_it->second, static_cache_it->first);
                static_cache_it = this->static_cache.erase(static_cache_it);
            }
        } else if (max_number_of_models_in_memory - number_of_models_in_static_cache >  this->no_graphs - 1) {
            auto static_cache_it = this->static_cache.begin();
            for (const GmModelIdx& idx: this->model_keys) {
                if (max_number_of_models_in_memory - number_of_models_in_static_cache == this->no_graphs - 1) {
                    break;
                }
                if (this->static_cache.find(idx) != this->static_cache.end()) {
                    this->static_cache[idx] = this->get_gm_model(idx);
                }
            }
        }
    }
 };

 void MgmModelBase::distribute_models_iteration(std::unordered_map<int, std::unordered_set<int>>& models_count, const int first, int iteration, const int& original_iteration) {
    if (models_count.find(iteration) == models_count.end()) {
        models_count[iteration] = std::unordered_set<int>();
    }
    for (const int graph_id: models_count[iteration]) {
        if (first == graph_id) {
            continue;
        }
        GmModelIdx model_id = (first < graph_id)? GmModelIdx(first, graph_id): GmModelIdx(graph_id, first);
        if (this->static_cache.find(model_id) == this->static_cache.end()) {
            this->static_cache[model_id] = this->get_gm_model(model_id);
            models_count[original_iteration].erase(first);
            if (models_count.find(original_iteration + 1) == models_count.end()) {
                models_count[original_iteration + 1] = std::unordered_set<int>();
            }
            models_count[original_iteration + 1].insert(first);
            models_count[iteration].erase(graph_id);
            if (models_count.find(iteration + 1) == models_count.end()) {
                models_count[iteration + 1] = std::unordered_set<int>();
            }
            models_count[iteration + 1].insert(graph_id);
            break;
        }
    }
    if (models_count[original_iteration].find(first) != models_count[original_iteration].end()) {
        this->distribute_models_iteration(models_count, first, iteration+1, original_iteration);
    }
}

void MgmModelBase::distribute_models_in_static_cache_while_in_parallel_mode(int number_of_models_in_static_cache) {
    int number_of_models_to_write_to_cache = (this->model_keys.size() > number_of_models_in_static_cache)? number_of_models_in_static_cache: this->model_keys.size();
    std::unordered_map<int, std::unordered_set<int>> model_ids_count;
    std::unordered_set<int> model_ids;
    for (const Graph& graph: this->graphs) {
        model_ids.insert(graph.id);
    }
    model_ids_count[0] = model_ids;
    int iteration = 0;
    while(number_of_models_to_write_to_cache > 0) {
        while (!model_ids_count[iteration].empty() and number_of_models_to_write_to_cache > 0) {
            auto model_ids_it = model_ids_count[iteration].begin();
            int first = *model_ids_it;
            this->distribute_models_iteration(model_ids_count, first, iteration, iteration);
            --number_of_models_to_write_to_cache;
        }
        ++iteration;
    }
}

// MgmModel

MgmModel::MgmModel(){ 
    //models.reserve(300);
}

void MgmModel::save_gm_model(std::shared_ptr<GmModel> gm_model, const GmModelIdx& idx) {
    this->models[idx] = gm_model;
    this->model_keys.push_back(idx);
    this->graph1_no_nodes[idx] = gm_model->graph1.no_nodes;
}

std::shared_ptr<GmModel> MgmModel::get_gm_model(const GmModelIdx& idx) {
    return this->models.at(idx);
}

// SqlMgmModel

SqlMgmModel::SqlMgmModel() {
    sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
    sqlite3_initialize();
    this->db = this->open_db();
    this->create_table();
    this->set_up_write_statement();
    this->set_up_read_statement();
    std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash> load;
    this->load_cache = std::make_shared<std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash>>(load);
    std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash> process;
    this->process_cache = std::make_shared<std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash>>(process);
}

sqlite3* SqlMgmModel::open_db() {
    sqlite3* db;
    int rc;
    rc = sqlite3_open("/mnt/d/models_sql.db", &db);

    if(rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << ", file inaccessible." << "\n";
        exit(3);
    }

    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt;
    const char* journal_mode = nullptr;

    if (sqlite3_prepare_v2(db, "PRAGMA journal_mode;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            journal_mode = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::cout << "Current journal_mode: " << journal_mode << std::endl;
        }
    }
    sqlite3_finalize(stmt);

    return db;
}

void SqlMgmModel::create_table() {
    int rc;
    sqlite3_stmt* create_statement;
    const char* create_sql = "CREATE TABLE IF NOT EXISTS models (g1_id INTEGER, g2_id INTEGER, gm_model BLOB, PRIMARY KEY (g1_id, g2_id));";
    rc = sqlite3_prepare_v2(this->db, create_sql, -1, &create_statement, NULL);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    rc = sqlite3_step(create_statement);
    sqlite3_finalize(create_statement);
}

void SqlMgmModel::set_up_write_statement(){
    const char* sql_insert = "INSERT OR REPLACE INTO models (g1_id, g2_id, gm_model) VALUES (?, ?, ?);";
    int rc = sqlite3_prepare_v2(this->db, sql_insert, -1, &(this->insert_stmt), NULL);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
}

void SqlMgmModel::set_up_read_statement(){
    const char* sql_read = "SELECT gm_model FROM models WHERE g1_id = ? AND g2_id = ?;";
    int rc = sqlite3_prepare_v2(this->db, sql_read, -1, &(this->read_stmt), NULL);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
}

SqlMgmModel::SqlMgmModel(bool parallel_mode_on): SqlMgmModel() {
    if (parallel_mode_on) {
        this->paralel_loading_mode = true;
        this->setup_db_connections_for_parallel_tasks(8);
    }
}

SqlMgmModel::~SqlMgmModel() {
    sqlite3_finalize(this->read_stmt);
    sqlite3_finalize(this->insert_stmt);
    while (!this->connections.empty()) {
        sqlite3_finalize(this->read_pool.front());
        sqlite3_finalize(this->write_pool.front());
        sqlite3_close(this->connections.front());
        this->connections.pop();
        this->read_pool.pop();
        this->write_pool.pop();
    }
    // this->delete_table();
    sqlite3_close(db);
    
}

void SqlMgmModel::delete_table() {
    int rc;
    sqlite3_stmt* delete_statement;
    const char* delete_sql = "DROP TABLE models;";
    rc = sqlite3_prepare_v2(this->db, delete_sql, -1, &delete_statement, NULL);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    rc = sqlite3_step(delete_statement);
    sqlite3_finalize(delete_statement);
}

void SqlMgmModel::save_gm_model(std::shared_ptr<GmModel> gm_model, const GmModelIdx& idx) {
    if (this->save_in_parallel) {
        sqlite3* db_connection;
        sqlite3_stmt* write_stmt_of_connection;
            {
                std::lock_guard<std::mutex> lock(this->connection_mutex);
                write_stmt_of_connection = this->write_pool.front();
                db_connection = this->connections.front();
                this->read_pool.pop();
                this->connections.pop();
            }
        
        this->save_model_to_db(gm_model, idx, write_stmt_of_connection);
        {
            std::lock_guard<std::mutex> lock(this->connection_mutex);
            this->write_pool.push(write_stmt_of_connection);
            this->connections.push(db_connection);
        }
    } else {
        this->save_model_to_db(gm_model, idx, this->insert_stmt);
    }
    this->model_keys.push_back(idx);
    this->graph1_no_nodes[idx] = gm_model->graph1.no_nodes;
}

void SqlMgmModel::save_model_to_db(std::shared_ptr<GmModel> gm_model, const GmModelIdx& idx, sqlite3_stmt* insert_to_db_stmt) {
    std::string serialized_model;
    serialize_to_binary(serialized_model, gm_model);
    spdlog::info("Saving to db...");
    // bind statement
    int rc = sqlite3_bind_int(insert_to_db_stmt, 1, idx.first);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to bind index 1: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    rc = sqlite3_bind_int(insert_to_db_stmt, 2, idx.second);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to bind index 2: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    rc = sqlite3_bind_blob(insert_to_db_stmt, 3, serialized_model.data(), serialized_model.size(), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to bind blob: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    // write into db and then reset the statement again
    rc = sqlite3_step(insert_to_db_stmt);
    rc = sqlite3_reset(insert_to_db_stmt);
    spdlog::info("Saved to db!");
}

std::shared_ptr<GmModel> SqlMgmModel::get_gm_model(const GmModelIdx& idx) {
    if (this->bulk_load_mode or this->paralel_loading_mode) {
        auto process_it = this->process_cache->find(idx);
        if (process_it != this->process_cache->end()) {
            return process_it->second;
        }
    } else {
        auto it = this->cache.find(idx);
        if (it != this->cache.end()) {
            return it->second;
        }
    }
    std::shared_ptr<GmModel> gmModel = this->read_model_from_db(idx);
    if (not this->paralel_loading_mode) {
        if (this->cache_queue.size() == this->no_graphs-1) {
            GmModelIdx idxOfModelToBeErased = this->cache_queue.front();
            this->cache.erase(idxOfModelToBeErased);
            this->cache_queue.pop();
        }
        this->cache[idx] = gmModel;
        this->cache_queue.push(idx);
    }
    return gmModel;
}

std::shared_ptr<GmModel> SqlMgmModel::read_model_from_db(const GmModelIdx& idx) {
    // bind to statement
    spdlog::info("Reading model with id ({}, {}) from db...", idx.first, idx.second);
    int rc = sqlite3_bind_int(this->read_stmt, 1, idx.first);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to bind index 1: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    rc = sqlite3_bind_int(this->read_stmt, 2, idx.second);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to bind index 2: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    rc = sqlite3_step(this->read_stmt);
    std::string read_serialized_model;
    if (rc == SQLITE_ROW) {
        const void* blob_data = sqlite3_column_blob(this->read_stmt, 0);  // Get the BLOB data
        int blob_size = sqlite3_column_bytes(this->read_stmt, 0);
        read_serialized_model = std::string(reinterpret_cast<const char*>(blob_data), blob_size);
    } else {
        std::cerr << "No data found!" << "\n";
    }
    rc = sqlite3_reset(this->read_stmt);
    spdlog::info("Read model from db!");
    std::shared_ptr<GmModel> gmModelPtr = deserialize_serialized_model(read_serialized_model);
    return gmModelPtr;
}

void SqlMgmModel::save_in_load_cache(const GmModelIdx& model_idx, std::mutex& save_to_cache_mutex) {
    sqlite3_stmt* read_stmt_of_connection;
    {
        std::lock_guard<std::mutex> lock(this->connection_mutex);
        read_stmt_of_connection = this->read_pool.front();
        this->read_pool.pop();
    }

    int rc = sqlite3_bind_int(read_stmt_of_connection, 1, model_idx.first);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to bind index 1: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    rc = sqlite3_bind_int(read_stmt_of_connection, 2, model_idx.second);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to bind index 2: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    rc = sqlite3_step(read_stmt_of_connection);
    std::string read_serialized_model;
    if (rc == SQLITE_ROW) {
        const void* blob_data = sqlite3_column_blob(read_stmt_of_connection, 0);  // Get the BLOB data
        int blob_size = sqlite3_column_bytes(read_stmt_of_connection, 0);
        read_serialized_model = std::string(reinterpret_cast<const char*>(blob_data), blob_size);
    } else {
        std::cerr << "No data found!" << "\n";
    }
    std::shared_ptr<GmModel> gmModel = deserialize_serialized_model(read_serialized_model);
    {
        std::lock_guard<std::mutex> lock(save_to_cache_mutex);
        (*this->load_cache)[model_idx] = gmModel;
    }
    rc = sqlite3_reset(read_stmt_of_connection);
    {
        std::lock_guard<std::mutex> lock(this->connection_mutex);
        this->read_pool.push(read_stmt_of_connection);
    }
};

void SqlMgmModel::setup_db_connections_for_parallel_tasks(const int& number_of_connections) {
    for (int i = 0; i < number_of_connections; ++i) {
        sqlite3* parallel_db = this->open_db();

        sqlite3_stmt* parallel_read_stmt;
        const char* sql_read = "SELECT gm_model FROM models WHERE g1_id = ? AND g2_id = ?;";
        int rc = sqlite3_prepare_v2(parallel_db, sql_read, -1, &parallel_read_stmt, NULL);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
            exit(1);
        }
        this->read_pool.push(parallel_read_stmt);

        sqlite3_stmt* parallel_insert_stmt;
        const char* sql_insert = "INSERT OR REPLACE INTO models (g1_id, g2_id, gm_model) VALUES (?, ?, ?);";
        rc = sqlite3_prepare_v2(this->db, sql_insert, -1, &parallel_insert_stmt, NULL);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
            exit(1);
        }
        this->write_pool.push(parallel_insert_stmt);

        this->connections.push(parallel_db);
    }
}

void SqlMgmModel::save_in_static_cache(std::shared_ptr<GmModel> gm_model, GmModelIdx idx) {
    this->model_keys.push_back(idx);
    this->graph1_no_nodes[idx] = gm_model->graph1.no_nodes;
    this->static_cache[idx] = gm_model;
}

void SqlMgmModel::bulk_read_to_load_cache(std::vector<int> current_graph_ids, std::vector<int> next_graph_ids) {  // currently unused
    std::vector<GmModelIdx> keys;
    keys.reserve(current_graph_ids.size() * next_graph_ids.size());
    for (const int& current_graph_id: current_graph_ids) {
        for (const int& next_graph_id: next_graph_ids) {
            keys.emplace_back(
                (current_graph_id < next_graph_id)? GmModelIdx(current_graph_id, next_graph_id): GmModelIdx(next_graph_id, current_graph_id)
            );
        }
    }
    // build statement
    std::string sql_query = "SELECT * FROM models WHERE";
    for (size_t i = 0; i < keys.size(); ++i) {
        sql_query += " (g1_id = ? AND g2_id = ?)";
        if (i < keys.size() - 1) {
            sql_query += " OR ";
        }
    }
    sqlite3_stmt* sql_stmt;

    // Prepare the SQL statement
    if (sqlite3_prepare_v2(this->db, sql_query.c_str(), -1, &sql_stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    // bind indexes to statement
    int binding_index = 1;
    for (const GmModelIdx& model_idx: keys) {
        if (sqlite3_bind_int(sql_stmt, binding_index, model_idx.first)    != SQLITE_OK ||
            sqlite3_bind_int(sql_stmt, binding_index+1, model_idx.second) != SQLITE_OK
        ) {
            std::cerr << "Failed to bind values: " << sqlite3_errmsg(db) << std::endl;
            sqlite3_finalize(sql_stmt);
            exit(1);
        }
        binding_index += 2;
    }
    
    std::string read_serialized_model;
    while (sqlite3_step(sql_stmt) == SQLITE_ROW) {
        int g1_id = sqlite3_column_int(sql_stmt, 0);
        int g2_id = sqlite3_column_int(sql_stmt, 1);
        const void* blob_data = sqlite3_column_blob(sql_stmt, 2);  // Get the BLOB data
        int blob_size = sqlite3_column_bytes(sql_stmt, 2);
        read_serialized_model = std::string(reinterpret_cast<const char*>(blob_data), blob_size);
        (*this->load_cache)[GmModelIdx(g1_id, g2_id)] = deserialize_serialized_model(read_serialized_model);
    }
    sqlite3_finalize(sql_stmt);
}

void SqlMgmModel::swap_caches() {
    std::swap(this->load_cache, this->process_cache);
    this->load_cache->clear();
}

void SqlMgmModel::bulk_read_to_load_cache(const int& model_id) {  // currently unused
    std::string sql_query = "SELECT * FROM models WHERE g1_id  = " + std::to_string(model_id) + " OR g2_id = " + std::to_string(model_id);
    sqlite3_stmt* sql_stmt;

    // Prepare the SQL statement
    if (sqlite3_prepare_v2(this->db, sql_query.c_str(), -1, &sql_stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    std::string read_serialized_model;
    while (sqlite3_step(sql_stmt) == SQLITE_ROW) {
        int g1_id = sqlite3_column_int(sql_stmt, 0);
        int g2_id = sqlite3_column_int(sql_stmt, 1);
        const void* blob_data = sqlite3_column_blob(sql_stmt, 2);  // Get the BLOB data
        int blob_size = sqlite3_column_bytes(sql_stmt, 2);
        read_serialized_model = std::string(reinterpret_cast<const char*>(blob_data), blob_size);
        (*this->load_cache)[GmModelIdx(g1_id, g2_id)] = deserialize_serialized_model(read_serialized_model);
    }
    sqlite3_finalize(sql_stmt);
}

SqlMgmModel::SqlMgmModel(SqlMgmModel&& other)
    : MgmModelBase(std::move(other)), db(std::move(other.db)) { }


SqlMgmModel& SqlMgmModel::operator=(SqlMgmModel&& other) {
    if (this != &other) {
        MgmModelBase::operator=(std::move(other));  // Move base class data
        db = std::move(other.db);               // Move the unique pointer
    }
    return *this;
}

// RocksdbMgmModel

RocksdbMgmModel::RocksdbMgmModel() {
    this->open_db();
    this->write_options = rocksdb::WriteOptions();
    this->write_options.sync = false;  // supposed to improve performance
    this->read_options = rocksdb::ReadOptions();
    this->read_options.fill_cache = false;
    std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash> load;
    this->load_cache = std::make_shared<std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash>>(load);
    std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash> process;
    this->process_cache = std::make_shared<std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash>>(process);
}

void RocksdbMgmModel::open_db() {
    rocksdb::Options options;
    options.max_open_files = 18000;
    options.create_if_missing = true;
    options.compression = rocksdb::kLZ4Compression;
    options.max_background_jobs = 6;
    options.bytes_per_sync = 1048576 * 100;
    options.compaction_pri = rocksdb::kMinOverlappingRatio;
    // Set up table options and enable a bloom filter.
    rocksdb::BlockBasedTableOptions table_options;
    table_options.enable_index_compression = false;
    table_options.cache_index_and_filter_blocks = true;
    // The parameter specifies bits per key, e.g., 10 bits per key.
    table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(20, false));

    // Assign the table options to the Options using a new block-based table factory.
    options.table_factory.reset(NewBlockBasedTableFactory(table_options));

    std::string db_path = "/mnt/d/modelsdb";
    rocksdb::Status status = rocksdb::DB::Open(options, db_path, &(this->db));
    if (!status.ok()) {
        std::cerr << "Failed to open RocksDB at path 'modelsdb': " << status.ToString() << std::endl;
        exit(1);
    }
}

RocksdbMgmModel::~RocksdbMgmModel() {
    delete this->db;
    // rocksdb::Status status = rocksdb::DestroyDB("modelsdb", rocksdb::Options());
    // if (!status.ok()) {
    //     std::cerr << "Error deleting database: " << status.ToString() << std::endl;
    // }
}

void RocksdbMgmModel::save_gm_model(std::shared_ptr<GmModel> gm_model, const GmModelIdx& idx) {
    std::string serialized_model;
    serialize_to_binary(serialized_model, gm_model);
    spdlog::info("Saving to db...");
    rocksdb::Status status = db->Put(this->write_options, this->convert_idx_into_string(idx), serialized_model);
    if (!status.ok()) {
        std::cerr << "Failed to write binary data to database: " << status.ToString() << std::endl;
        delete db;
        return;
    }
    spdlog::info("Saved to db!");
    this->model_keys.push_back(idx);
    this->graph1_no_nodes[idx] = gm_model->graph1.no_nodes;
}

std::string RocksdbMgmModel::convert_idx_into_string(const GmModelIdx& idx) const {
    return std::to_string(idx.first) + "," + std::to_string(idx.second);
}

std::shared_ptr<GmModel> RocksdbMgmModel::get_gm_model(const GmModelIdx& idx) {
    auto it = this->static_cache.find(idx);
    if (it != this->static_cache.end()) {
        return it->second;
    }
    if (this->bulk_load_mode or this->paralel_loading_mode) {
        auto it = this->process_cache->find(idx);
        if (it != this->process_cache->end()) {
            return it->second;
        }
    } else {
       auto it = this->cache.find(idx);
        if (it != this->cache.end()) {
            return it->second;
        } 
    }
    
    std::string retrieved_serialized_model;
    spdlog::info("Reading model with id ({}, {}) from db...", idx.first, idx.second);
    rocksdb::Status status = db->Get(this->read_options, this->convert_idx_into_string(idx), &retrieved_serialized_model);
    if (!status.ok()) {
        std::cerr << "Failed to read binary data from database: " << status.ToString() << std::endl;
    }
    spdlog::info("Read model from db!");
    std::shared_ptr<GmModel> gmModel = deserialize_serialized_model(retrieved_serialized_model);
    if (not this->paralel_loading_mode) {
        if (this->cache_queue.size() == this->no_graphs-1) {
            GmModelIdx idxOfModelToBeErased = this->cache_queue.front();
            this->cache.erase(idxOfModelToBeErased);
            this->cache_queue.pop();
        }
        this->cache[idx] = gmModel;
        this->cache_queue.push(idx);
    }
    
    return gmModel;
}

void RocksdbMgmModel::save_in_load_cache(const GmModelIdx& model_idx, std::mutex& save_to_cache_mutex) {
    // get model from db
    std::string retrieved_serialized_model;
    rocksdb::Status status = db->Get(this->read_options, this->convert_idx_into_string(model_idx), &retrieved_serialized_model);
    if (!status.ok()) {
        std::cerr << "Failed to read binary data from database: " << status.ToString() << std::endl;
    }
    std::shared_ptr<GmModel> gmModel = deserialize_serialized_model(retrieved_serialized_model);
    {
        std::lock_guard<std::mutex> lock(save_to_cache_mutex);
        (*this->load_cache)[model_idx] = gmModel;
    }
};

void RocksdbMgmModel::swap_caches() {
    std::swap(this->load_cache, this->process_cache);
    this->load_cache->clear();
}

void RocksdbMgmModel::save_in_static_cache(std::shared_ptr<GmModel> gm_model, GmModelIdx idx) {
    this->model_keys.push_back(idx);
    this->graph1_no_nodes[idx] = gm_model->graph1.no_nodes;
    this->static_cache[idx] = gm_model;
}

void RocksdbMgmModel::bulk_read_to_load_cache(std::vector<int> current_graph_ids, std::vector<int> next_graph_ids) {  // currently unused
    std::vector<GmModelIdx> keys;
    keys.reserve(current_graph_ids.size() * next_graph_ids.size());
    for (const int& current_graph_id: current_graph_ids) {
        for (const int& next_graph_id: next_graph_ids) {
            keys.emplace_back(
                (current_graph_id < next_graph_id)? GmModelIdx(current_graph_id, next_graph_id): GmModelIdx(next_graph_id, current_graph_id)
            );
        }
    }
    // convert keys to string
    std::vector<rocksdb::Slice> slice_keys(keys.size());
    std::vector<std::string> string_storage(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        string_storage[i] = this->convert_idx_into_string(keys[i]);
        slice_keys[i] = rocksdb::Slice(string_storage[i]);
    }
    std::vector<std::string> values(keys.size());
    this->db->MultiGet(this->read_options, slice_keys, &values);
    for (size_t i = 0; i < keys.size(); ++i) {
        try {
            (*this->load_cache)[keys[i]] = deserialize_serialized_model(values[i]);
        } catch (const cereal::Exception& ce) {
            std::cerr << "Cereal exception caught: " << ce.what() << std::endl;
            spdlog::info("Reloading model ({}, {})", keys[i].first, keys[i].second);
            std::string retrieved_serialized_model;
            rocksdb::Status status = db->Get(this->read_options, this->convert_idx_into_string(keys[i]), &retrieved_serialized_model);
            if (!status.ok()) {
                std::cerr << "Failed to read binary data from database: " << status.ToString() << std::endl;
            }
            spdlog::info("Read model from db!");
            (*this->load_cache)[keys[i]] = deserialize_serialized_model(retrieved_serialized_model);
        }
    }
}

void RocksdbMgmModel::bulk_read_to_load_cache(const int& model_id) {  // currently unused
    std::vector<rocksdb::Slice> slice_keys(this->no_graphs - 1);
    std::vector<std::string> string_storage(this->no_graphs - 1);
    int slice_idx = 0;
    for (int other_model_id = 0; other_model_id < this->no_graphs; ++other_model_id) {
        if (model_id == other_model_id) {
            continue;
        }
        string_storage[slice_idx] = (model_id < other_model_id) ? std::to_string(model_id) + "," + std::to_string(other_model_id): 
                                                                  std::to_string(other_model_id) + "," + std::to_string(model_id);
        slice_keys[slice_idx] = rocksdb::Slice(string_storage[slice_idx]);
        ++slice_idx;
    }
    std::vector<std::string> values(this->no_graphs - 1);
    this->db->MultiGet(this->read_options, slice_keys, &values);
    int serialized_model_index = 0;
    for (int other_model_id = 0; other_model_id < this->no_graphs; ++other_model_id) {
        if (model_id == other_model_id) {
            continue;
        }
        try {
            (*this->load_cache)[(model_id < other_model_id) ? GmModelIdx(model_id, other_model_id): GmModelIdx(other_model_id, model_id)] = deserialize_serialized_model(values[serialized_model_index]);
        } catch (const cereal::Exception& ce) {
            std::cerr << "Cereal exception caught: " << ce.what() << std::endl;
            GmModelIdx idx = (model_id < other_model_id) ? GmModelIdx(model_id, other_model_id): GmModelIdx(other_model_id, model_id);
            spdlog::info("Reloading model ({}, {})", idx.first, idx.second);
            std::string retrieved_serialized_model;
            rocksdb::Status status = db->Get(this->read_options, this->convert_idx_into_string(idx), &retrieved_serialized_model);
            if (!status.ok()) {
                std::cerr << "Failed to read binary data from database: " << status.ToString() << std::endl;
            }
            spdlog::info("Read model from db!");
            (*this->load_cache)[idx] = deserialize_serialized_model(retrieved_serialized_model);
        }
        ++serialized_model_index;
    }
};

// ParallelDBTasks

void ParallelDBTasks::work_on_tasks() {
    std::vector<std::thread> threads;
    for (int idx = 0; idx < numThreads; ++idx) {
        threads.emplace_back(&ParallelDBTasks::thread_work_on_tasks, this);
    }
    for (auto& t : threads) {
        t.join();
    }
}

void ParallelDBTasks::thread_work_on_tasks() {
    while (true) {
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lock(this->task_mutex);
            if (this->tasks.empty()) return;
            task = this->tasks.front();
            this->tasks.pop();
        }
        task();
    }
}

void ParallelDBTasks::work_on_tasks_for_solver(std::function<void()> solver_function, const int& preload_graph_id, const std::vector<int>& graph_ids) {
    this->add_task(solver_function);
    for (const int& graph_id: graph_ids) {
        GmModelIdx model_id = (graph_id < preload_graph_id)? GmModelIdx(graph_id, preload_graph_id): GmModelIdx(preload_graph_id, graph_id);
        // if (!this->in_static_cache_map[model_id]) {
            tasks.push(
                [this, model_id]() {
                    this->model->save_in_load_cache(model_id, this->cache_mutex);
                }
            );
        // }; 
    }
    this->work_on_tasks();
}

void ParallelDBTasks::work_on_tasks_for_search(std::function<void()> search_function, const int& next_graph_id) {
    spdlog::info("Starting preload for {}", next_graph_id);
    this->add_task(search_function);
    for (int id = 0; id < model->no_graphs; ++id) {
        if (id != next_graph_id) {
            GmModelIdx model_id = (id < next_graph_id)? GmModelIdx(id, next_graph_id): GmModelIdx(next_graph_id, id);
            // if (!this->in_static_cache_map[model_id]) {
                tasks.push(
                    [this, model_id]() {
                        this->model->save_in_load_cache(model_id, this->cache_mutex);
                    }
                );
            // }; 
        }
    }
    this->work_on_tasks();
}

void ParallelDBTasks::work_on_tasks_for_saving_in_db(std::queue<std::pair<GmModelIdx, std::shared_ptr<GmModel>>>& gm_models) {
    while (!gm_models.empty()) {
        std::shared_ptr<GmModel> gm_model = gm_models.front().second;
        GmModelIdx idx = gm_models.front().first;
        this->tasks.push(
            [gm_model, idx, this]() {
                this->model->save_gm_model(gm_model, idx);
            }
        );
        gm_models.pop();
    }
    this->work_on_tasks();
    std::queue<std::pair<GmModelIdx, std::shared_ptr<GmModel>>>().swap(gm_models);  // clear queue
}

void ParallelDBTasks::add_task(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(this->task_mutex);
    this->tasks.push(task);
}
    
}
