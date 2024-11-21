#include "multigraph.hpp"

#include <utility>
#include <cassert>
#include <fstream>
#include <cstdlib>
#include <cereal/archives/binary.hpp>

namespace mgm {
    
Graph::Graph(int id, int no_nodes) : id(id), no_nodes(no_nodes) {};

GmModelBase::GmModelBase(Graph g1, Graph g2, int no_assignments, int no_edges): 
    graph1(g1), 
    graph2(g2),
    no_assignments(no_assignments),
    no_edges(no_edges) {}

GmModel::GmModel(Graph g1, Graph g2, int no_assignments, int no_edges) 
    : 
    GmModelBase(g1, g2, no_assignments, no_edges)
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
    std::ostringstream output_stream;
        {
            
            cereal::BinaryOutputArchive OArchive(output_stream);
            OArchive(*this);
            
        }
    result_string = output_stream.str();
}

StxxlGmModel::StxxlGmModel(Graph g1, Graph g2, int no_assignments, int no_edges)
: 
    GmModelBase(g1, g2, no_assignments, no_edges) {
        costmap_ptr costs_ptr(std::make_shared<CostMap>(no_assignments, no_edges));
        external_costs ex_costs(costs_ptr);
        this->costs = ex_costs;
        this->assignment_list.reserve(no_assignments);

        //FIXME: Number of elements for assignments_left and assignments_right is unclear.
        // Loading assignments without reserving space leads to (avoidable?) reallocations. 
        this->assignments_left  = std::vector<std::vector<int>>(g1.no_nodes);
        this->assignments_right = std::vector<std::vector<int>>(g2.no_nodes);
    }

void StxxlGmModel::add_assignment([[maybe_unused]] int assignment_id, int node1, int node2, double cost) {
    assert ((size_t) assignment_id == this->assignment_list.size());

    (void) this->assignment_list.emplace_back(node1, node2);

    this->costs.get()->set_unary(node1, node2, cost);
    this->assignments_left[node1].push_back(node2);
    this->assignments_right[node2].push_back(node1);
}

void StxxlGmModel::add_edge(int assignment1, int assignment2, double cost) {
    auto& a1 = this->assignment_list[assignment1];
    auto& a2 = this->assignment_list[assignment2];

    this->add_edge(a1.first, a1.second, a2.first, a2.second, cost);
}

void StxxlGmModel::add_edge(int assignment1_node1, int assignment1_node2, int assignment2_node1, int assignment2_node2, double cost) {
    this->costs.get()->set_pairwise(assignment1_node1, assignment1_node2, assignment2_node1, assignment2_node2, cost);
    //this->costs->set_pairwise(a2.first, a2.second, a1.first, a1.second, cost); //FIXME: RAM overhead. Avoids sorting later though.
}

std::shared_ptr<CostMap> StxxlGmModel::get_costs() {
    return this->costs.get();
}


MgmModel::MgmModel(){ 
    //models.reserve(300);
}

void MgmModel::save_gm_model(std::shared_ptr<GmModelBase> gm_model, const GmModelIdx& idx) {
    this->models[idx] = gm_model;
    this->model_keys.push_back(idx);
}

std::shared_ptr<GmModelBase> MgmModel::get_gm_model(const GmModelIdx& idx) {
    return this->models.at(idx);
}

void SqlMgmModel::save_gm_model(std::shared_ptr<GmModelBase> gm_model, const GmModelIdx& idx) {
    this->save_model_to_db(gm_model, idx);
    this->model_keys.push_back(idx);
}
std::shared_ptr<GmModelBase> SqlMgmModel::get_gm_model(const GmModelIdx& idx) {
    auto it = this->models.find(idx);
    if (it != this->models.end()) {
        return it->second;
    }
    std::shared_ptr<GmModelBase> gmModel = this->read_model_from_db(idx);
    if (this->cache_queue.size() == this->number_of_cached_models) {
        GmModelIdx idxOfModelToBeErased = this->cache_queue.front();
        this->models.erase(idxOfModelToBeErased);
        this->cache_queue.pop();
    }
    this->models[idx] = gmModel;
    this->cache_queue.push(idx);

    return gmModel;
}

sqlite3* SqlMgmModel::open_db() {
    sqlite3* db;
    int rc;
    rc = sqlite3_open("./models_sql.db", &db);

    if(rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << ", file inaccessible." << "\n";
        exit(3);
    }
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
    const char* sql_insert = "INSERT OR REPLACE INTO models (g1_id, g2_id, gm_model) VALUES (?, ?, ?)";
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

void SqlMgmModel::save_model_to_db(std::shared_ptr<GmModelBase> gm_model, const GmModelIdx& idx) {
    // bind statement
    int rc = sqlite3_bind_int(this->insert_stmt, 1, idx.first);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to bind index 1: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    rc = sqlite3_bind_int(this->insert_stmt, 2, idx.second);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to bind index 2: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    std::string serialized_model;
    serialize_to_binary(serialized_model, gm_model);
    rc = sqlite3_bind_blob(this->insert_stmt, 3, serialized_model.data(), serialized_model.size(), SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to bind blob: " << sqlite3_errmsg(db) << std::endl;
        exit(1);
    }
    // write into db and then reset the statement again
    rc = sqlite3_step(this->insert_stmt);
    rc = sqlite3_reset(this->insert_stmt);
}

std::shared_ptr<GmModelBase> SqlMgmModel::read_model_from_db(const GmModelIdx& idx) {
    // bind to statement
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
    std::shared_ptr<GmModel> gmModelPtr;
    deserialize_serialized_model(read_serialized_model, gmModelPtr);
    return gmModelPtr;
}

void serialize_to_binary(std::string& result_string, std::shared_ptr<GmModelBase> gmModel) {
    std::ostringstream output_stream;
        {
            
            cereal::BinaryOutputArchive OArchive(output_stream);
            OArchive(gmModel);
            
        }
    result_string = output_stream.str();
}

void deserialize_serialized_model(std::string& serialized_model, std::shared_ptr<GmModelBase> gmModel) {
    std::istringstream iss(serialized_model);
    cereal::BinaryInputArchive iarchive(iss);
    iarchive(gmModel); 
}

SqlMgmModel::SqlMgmModel() {
    this->db = open_db();
    this->create_table();
    this->set_up_write_statement();
    this->set_up_read_statement();
}
SqlMgmModel::~SqlMgmModel() {
    sqlite3_finalize(this->read_stmt);
    sqlite3_finalize(this->insert_stmt);
    this->delete_table();
    sqlite3_close(db);
}

// Move constructor
SqlMgmModel::SqlMgmModel(SqlMgmModel&& other)
    : MgmModelBase(std::move(other)), db(std::move(other.db)) { }

// Move assignment operator
SqlMgmModel& SqlMgmModel::operator=(SqlMgmModel&& other) {
    if (this != &other) {
        MgmModelBase::operator=(std::move(other));  // Move base class data
        db = std::move(other.db);               // Move the unique pointer
    }
    return *this;
}

// RocksSqlMgmModel

RocksdbMgmModel::RocksdbMgmModel() {
    this->open_db();
    this->write_options = rocksdb::WriteOptions();
    this->read_options = rocksdb::ReadOptions();
}

void RocksdbMgmModel::save_gm_model(std::shared_ptr<GmModelBase> gm_model, const GmModelIdx& idx) {
    std::string serialized_model;
    serialize_to_binary(serialized_model, gm_model);
    rocksdb::Status status = db->Put(this->write_options, this->convert_idx_into_string(idx), serialized_model);
    if (!status.ok()) {
        std::cerr << "Failed to write binary data to database: " << status.ToString() << std::endl;
        delete db;
        return;
    }
    this->model_keys.push_back(idx);
}

std::shared_ptr<GmModelBase> RocksdbMgmModel::get_gm_model(const GmModelIdx& idx) {
    std::string retrieved_serialized_model;
    rocksdb::Status status = db->Get(this->read_options, this->convert_idx_into_string(idx), &retrieved_serialized_model);
    if (!status.ok()) {
        std::cerr << "Failed to read binary data from database: " << status.ToString() << std::endl;
    }
    std::shared_ptr<GmModelBase> gmModel;
    deserialize_serialized_model(retrieved_serialized_model, gmModel);
    return gmModel;
}

void RocksdbMgmModel::open_db() {
    rocksdb::Options options;
    options.create_if_missing = true;
    rocksdb::Status status = rocksdb::DB::Open(options, "modelsdb", &(this->db));
    if (!status.ok()) {
        std::cerr << "Failed to open RocksDB at path 'modelsdb': " << status.ToString() << std::endl;
        exit(1);
    }
}

std::string RocksdbMgmModel::convert_idx_into_string(const GmModelIdx& idx) const {
    return std::to_string(idx.first) + "," + std::to_string(idx.second);
}

RocksdbMgmModel::~RocksdbMgmModel() {
    delete this->db;
    rocksdb::Status status = rocksdb::DestroyDB("modelsdb", rocksdb::Options());
    if (!status.ok()) {
        std::cerr << "Error deleting database: " << status.ToString() << std::endl;
    }
}

// stxxl

external_gm_model& stxxl_unordered_map::at(const GmModelIdx idx)
{
    auto result = this->find(idx);
    if (result == this->end()) {
        throw std::out_of_range("Element not found in container");
    }

    return (*result).second;
}
const external_gm_model& stxxl_unordered_map::at(const GmModelIdx idx) const
{
    auto result = this->find(idx);
    if (result == this->end()) {
        throw std::out_of_range("Element not found in container");
    }

    return (*result).second;
}

void StxxlMgmModel::save_gm_model(std::shared_ptr<GmModelBase> gm_model, const GmModelIdx& idx) {
    external_gm_model ex_gm_model(gm_model);
    this->models.insert(std::make_pair(idx, ex_gm_model));
    this->model_keys.push_back(idx);
}
std::shared_ptr<GmModelBase> StxxlMgmModel::get_gm_model(const GmModelIdx& idx) {
    return this->models[idx].get();
}

}
