#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <regex>
#include <fmt/core.h>

#include <cstdio>

// Logging
#include <spdlog/spdlog.h>

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

MgmModel parse_dd_file(fs::path dd_file) {
    auto model = MgmModel();

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
                model.graphs.resize(max_graph_id+1);
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
            model.graphs[g1_id] = g1;
            model.graphs[g2_id] = g2;

            GmModel gmModel(g1, g2, no_a, no_e);

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

                gmModel.add_assignment(ass_id, id1, id2, c);
            }

            // Edges
            for (auto i = 0; i < no_e; i++) {
                std::getline(infile, line);
                lineStream.clear();
                lineStream.str(line.substr(2));
                lineStream >> id1 >> id2 >> c;
                
                gmModel.add_edge(id1, id2, c);
            }

            GmModelIdx idx(g1_id, g2_id);
            model.models[idx] = std::make_shared<GmModel>(std::move(gmModel));
        }
    }
    model.no_graphs = max_graph_id + 1;
    return model;
}

MgmModel parse_dd_file_fscan(fs::path dd_file) {
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

        GmModel gmModel(g1, g2, no_a, no_e);

        int ass_id = 0;
        int id1 = 0;
        int id2 = 0;
        double c = 0.0;

        // Assignments
        for (auto i = 0; i < no_a; i++) {
            ret = std::fscanf(infile, "%10s %d %d %d %lf\n", line_indicator, &ass_id, &id1, &id2, &c);
            assert(ret == 5);
            assert(strncmp(line_indicator, "a", 1) == 0);
            gmModel.add_assignment(ass_id, id1, id2, c);
        }

        // Edges
        for (auto i = 0; i < no_e; i++) {
            ret = std::fscanf(infile, "%10s %d %d %lf\n", line_indicator, &id1, &id2, &c);
            assert(ret == 4);
            assert(strncmp(line_indicator, "e", 1) == 0);
            gmModel.add_edge(id1, id2, c);
        }

        GmModelIdx idx(g1_id, g2_id);
        model.models[idx] = std::make_shared<GmModel>(std::move(gmModel));
    }
    model.no_graphs = max_graph_id + 1;

    fclose(infile);
    return model;
}

void safe_to_disk(const MgmSolution& solution, fs::path outPath, std::string filename) {
   json j;

    j["energy"] = solution.evaluate();

    for (auto const& [key, s] : solution.gmSolutions) {
        std::string key_string = fmt::format("{}, {}", s.model->graph1.id, s.model->graph2.id);
        j["labeling"][key_string] = s.labeling;
    }

    spdlog::debug("Saving solution to disk: {}", j.dump());
    std::ofstream o(outPath / (filename + ".json"));
    o << std::setw(4) << j << std::endl;
}
}