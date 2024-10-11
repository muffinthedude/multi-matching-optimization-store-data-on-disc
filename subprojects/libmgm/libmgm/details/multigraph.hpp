#ifndef LIBMGM_MULTIGRAPH_HPP
#define LIBMGM_MULTIGRAPH_HPP

#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <utility>
#include <stxxl.h> 
#include <stxxl/bits/common/external_shared_ptr.h>
#include <variant>

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


struct CompareLess
{
 bool operator () (const GmModelIdx & a, const GmModelIdx & b) const
 { 
    return (a.first != b.first and a.first < b.first) or (a.first == b.first and a.second < b.second);
}
 static GmModelIdx min_value() { return GmModelIdx(std::numeric_limits<int>::min(), std::numeric_limits<int>::min()); }
 static GmModelIdx max_value() { return GmModelIdx(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()); }
};


class Graph {
    public:
        Graph() {};
        Graph(int id, int no_nodes);

        int id;
        int no_nodes;
};

//template<typename T> 
//class external_shared_ptr: public stxxl::external_shared_ptr<T> {
//public:
//    external_shared_ptr() : stxxl::external_shared_ptr<T>() {}
//    external_shared_ptr(T data) : stxxl::external_shared_ptr<T>(data) {}
//
//    std::shared_ptr<T> current_data;
//
//    T* operator->() {
//        this->current_data = std::make_shared<T>(this->get());
//        return current_data;
//    }
//    const std::shared_ptr<T> operator->() const {
//        return std::make_shared<T>(this->get());
//    }
//};

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
        stxxl::external_shared_ptr<CostMap> costs;
};

#define SUB_BLOCK_SIZE 8192
#define SUB_BLOCKS_PER_BLOCK 256
 // template parameter <KeyType, MappedType, HashType, CompareType, SubBlockSize, SubBlocksPerBlock>
 typedef stxxl::unordered_map<
GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash, CompareLess, SUB_BLOCK_SIZE, SUB_BLOCKS_PER_BLOCK
 > unordered_map_type;

class stxxl_unordered_map : public stxxl::unordered_map<GmModelIdx, 
                                              stxxl::external_shared_ptr<GmModel>, 
                                              GmModelIdxHash, 
                                              CompareLess, 
                                              SUB_BLOCK_SIZE, 
                                              SUB_BLOCKS_PER_BLOCK>                                               
{
public:
    stxxl::external_shared_ptr<GmModel>& at(const GmModelIdx);
    const stxxl::external_shared_ptr<GmModel>& at(const GmModelIdx) const;
};

class MgmModel {
    public:
        MgmModel();

        int no_graphs;
        std::vector<Graph> graphs;
        // std::unordered_map<GmModelIdx, std::shared_ptr<GmModel>, GmModelIdxHash> models;
        stxxl_unordered_map models;
};


class MgmModelStxxl{
    public:
        MgmModelStxxl();
};
}

#endif