#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <regex>

#include <cstdio>

// Logging
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h> // print vector

// json
#include <nlohmann/json.hpp>
namespace fs = std::filesystem;
using json = nlohmann::json;

#include "io_utils.hpp"
#include "solution.hpp"
#include "multigraph.hpp"

namespace mgm::io {
    
const std::regex re_gm("^gm ([0-9]+) ([0-9]+)$");
const std::regex re_p("^p ([0-9]+) ([0-9]+) ([0-9]+) ([0-9]+)$");
const std::regex re_a("^a ([0-9]+) ([0-9]+) ([0-9]+) (.+)$");
const std::regex re_e("^e ([0-9]+) ([0-9]+) (.+)$");

std::shared_ptr<MgmModelBase> parse_dd_file(fs::path dd_file, disc_save_mode save_mode, 
    cache_mode cache_mode_setter, long long int memory_limit) {
    std::shared_ptr<MgmModelBase> model;
    switch (save_mode) {
        case disc_save_mode::no:
            model = std::make_shared<MgmModel>();
            break;
        case disc_save_mode::sql:
            model = std::make_shared<SqlMgmModel>(cache_mode_setter == cache_mode::preload);
            break;
        case disc_save_mode::rocksdb:
            model = std::make_shared<RocksdbMgmModel>();
            break;
        default:
            model = std::make_shared<MgmModel>();
            break;
    }

    long long max_gm_model = 0;
    bool memory_limited = memory_limit != 0;

    long long write_limit;
    ParallelDBTasks parallel_saving_to_db(model);
    std::queue<std::pair<GmModelIdx, std::shared_ptr<GmModel>>> save_to_db_queue;
    switch (cache_mode_setter) {
        case cache_mode::preload:
            model->paralel_loading_mode = true;
            if (memory_limited) {
                write_limit = memory_limit;
            }
            break;
        case cache_mode::bulk:
            model->bulk_load_mode = true;
            break;
    }

    std::ifstream infile(dd_file);
    std::string line; 
    std::stringstream lineStream;
    std::smatch re_match;
    int max_graph_id = 0;
    while (std::getline(infile, line)) {
        if (std::regex_match(line, re_match, re_gm)) {
            int g1_id = std::stoi(re_match[1]);
            int g2_id = std::stoi(re_match[2]);
            if (g2_id > max_graph_id) {
                max_graph_id = g2_id;
                model->graphs.resize(max_graph_id+1);
            }
            
            spdlog::info("Graph {} and Graph {}", g1_id, g2_id);

            // metadata of GM problem
            std::getline(infile, line);
            lineStream.clear();
            lineStream.str(line.substr(2));
            int no_left = 0;
            int no_right = 0;
            int no_a = 0;
            int no_e = 0;

            lineStream >> no_left >> no_right >> no_a >> no_e;
            
            //FIXME: graphs with same id initialized multiple times over.
            Graph g1(g1_id, no_left);
            Graph g2(g2_id, no_right);
            model->graphs[g1_id] = g1;
            model->graphs[g2_id] = g2;

            std::shared_ptr<GmModel> gmModel = std::make_shared<GmModel>(g1, g2, no_a, no_e);
            
            int ass_id = 0;
            int id1 = 0;
            int id2 = 0;
            double c = 0.0;

            // Assignments
            for (auto i = 0; i < no_a; i++) {
                std::getline(infile, line);
                lineStream.clear();
                lineStream.str(line.substr(2));
                lineStream >> ass_id >> id1 >> id2 >> c;

                gmModel->add_assignment(ass_id, id1, id2, c);
            }

            // Edges
            for (auto i = 0; i < no_e; i++) {
                std::getline(infile, line);
                lineStream.clear();
                lineStream.str(line.substr(2));
                lineStream >> id1 >> id2 >> c;
                
                gmModel->add_edge(id1, id2, c);
            }

            GmModelIdx idx(g1_id, g2_id);

            if (memory_limited) {
                long long memory_gm_model = gmModel->estimate_memory_consumption();
                max_gm_model = std::max(max_gm_model, memory_gm_model);
                if (save_mode == disc_save_mode::no) {
                    memory_limit -= memory_gm_model;
                    if (memory_limit - max_gm_model < 0) {
                        spdlog::error("Not enough memory to load all models. Use disc saving_mode.");
                        exit(1);
                    }
                    model->save_gm_model(gmModel, idx);
                    continue;
                }
                
                if (write_limit > 2* memory_gm_model) {
                    model->save_in_static_cache(gmModel, idx);
                    write_limit -= memory_gm_model;
                    continue;
                }
            }
            model->save_gm_model(gmModel, idx);
        }
    }

    model->graphs = std::vector<Graph>(180);
    for (int j = 0; j < 180; ++j) {
        model->graphs[j] = Graph(j, 558);
        int value = 558;
        for (int i = j + 1; i < 180; ++i) {
            model->model_keys.push_back(GmModelIdx(j,i));
            model->graph1_no_nodes[GmModelIdx(j,i)] = value;
        }
    }

    model->no_graphs = max_graph_id + 1;
    model->number_of_cached_models = max_graph_id + 1;
    if (memory_limited and save_mode != disc_save_mode::no) {
        spdlog::info("Building caches...\n");
        model->build_caches(memory_limit, max_gm_model);
    }
    spdlog::info("Finished parsing model.\n");
    return model;
}

std::shared_ptr<MgmModelBase> parse_dd_file_fscan(fs::path dd_file) {
    auto model = MgmModel();

    FILE* infile;
    infile = std::fopen(dd_file.c_str(), "r");
    fscanf(infile, " ");

    char BUF[1024];

    int max_graph_id = 0;
    char line_indicator[11];
    int g1_id = 0;
    int g2_id = 0;
    int ret = 0;
    while (std::fgets(BUF, sizeof(BUF), infile) != NULL) {
        ret = std::sscanf(BUF, "%10s %d %d\n", line_indicator, &g1_id, &g2_id);
        if (ret < 3) {
            continue;
        }
        if (strncmp(line_indicator, "gm", 2) != 0) {
            continue;
        }


        if (g2_id > max_graph_id) {
            max_graph_id = g2_id;
            model.graphs.resize(max_graph_id+1);
        }
        spdlog::info("Graph {} and Graph {}", g1_id, g2_id);

        int no_left = 0;
        int no_right = 0;
        int no_a = 0;
        int no_e = 0;

        ret = std::fscanf(infile, "%10s %d %d %d %d\n", line_indicator, &no_left, &no_right, &no_a, &no_e);

        assert(ret == 5);
        assert(strncmp(line_indicator, "p", 1) == 0);

        //FIXME: graphs with same id initialized multiple times over.
        Graph g1(g1_id, no_left);
        Graph g2(g2_id, no_right);
        model.graphs[g1_id] = g1;
        model.graphs[g2_id] = g2;

        std::shared_ptr<GmModel> gmModel;
        gmModel = std::make_shared<GmModel>(g1, g2, no_a, no_e);

        int ass_id = 0;
        int id1 = 0;
        int id2 = 0;
        double c = 0.0;

        // Assignments
        for (auto i = 0; i < no_a; i++) {
            ret = std::fscanf(infile, "%10s %d %d %d %lf\n", line_indicator, &ass_id, &id1, &id2, &c);
            assert(ret == 5);
            assert(strncmp(line_indicator, "a", 1) == 0);
            gmModel->add_assignment(ass_id, id1, id2, c);
        }

        // Edges
        for (auto i = 0; i < no_e; i++) {
            ret = std::fscanf(infile, "%10s %d %d %lf\n", line_indicator, &id1, &id2, &c);
            assert(ret == 4);
            assert(strncmp(line_indicator, "e", 1) == 0);
            gmModel->add_edge(id1, id2, c);
        }

        GmModelIdx idx(g1_id, g2_id);
        model.save_gm_model(gmModel, idx);
    }
    model.no_graphs = max_graph_id + 1;

    fclose(infile);
    std::shared_ptr<MgmModelBase> model_ptr = std::make_shared<MgmModel>(model);
    return model_ptr;
}

json null_valued_labeling(std::vector<int> l) {
    json new_l;
    for (const auto& value : l) {
        if (value == -1) {
            new_l.push_back(nullptr);
        } else {
            new_l.push_back(value);
        }
    }
    return new_l;
}

void safe_to_disk(const MgmSolution& solution, fs::path outPath, std::string filename) {
   json j;

    spdlog::info("evaluating energy befor saving");
    // energy
    j["energy"] = solution.evaluate();
    
    // Number of nodes per graph:
    std::vector<int> graph_sizes;
    for (const auto & g : solution.model->graphs) {
        graph_sizes.push_back(g.no_nodes);
    }
    j["graph orders"] = graph_sizes;

    spdlog::info("saving labeling");
    // labeling
    for (auto const& [key, s] : solution.gmSolutions) {
        json json_labeling = null_valued_labeling(s.labeling);

        // std::shared_ptr<GmModel> gmModel = solution.model->get_gm_model(key);
        std::string key_string = fmt::format("{}, {}", key.first, key.second);
        j["labeling"][key_string] = json_labeling; 
    }


    spdlog::debug("Saving solution to disk: {}", j.dump());
    std::ofstream o(outPath / (filename + ".json"));
    o << std::setw(4) << j << std::endl;
}

GmModelIdx from_json(const std::string input) {
    std::string g1, g2;
    std::istringstream ss(input);
    
    std::getline(ss, g1, ',');
    std::getline(ss, g2, ',');

    return GmModelIdx(std::stoi(g1), std::stoi(g2));
}

MgmSolution import_from_disk(std::shared_ptr<MgmModelBase> model, fs::path labeling_path) {
    MgmSolution s(model);

    spdlog::info("Parsing json");
    std::ifstream ifs(labeling_path);
    json j = json::parse(ifs);

    for (auto& [key, arr] : j.at("labeling").items()) {
        GmModelIdx idx = from_json(key);
        auto& gm_labeling = s.gmSolutions[idx].labeling;

        int i = 0;
        for (auto& val : arr) {
            if (!val.is_null())
                gm_labeling[i] = val.template get<int>();
            i++;
        }
    }
    if (!j.at("energy").is_null()){
        double j_energy = j.at("energy").template get<double>();
        spdlog::debug("Energy according to json: {}", j_energy);
        }
    else {
        spdlog::debug("Energy according to json: null");
    }

    spdlog::debug("Energy of parsed model: {}", s.evaluate());
    return s;
}
}