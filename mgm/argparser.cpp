#include <iostream>
#include <filesystem>
#include <CLI/CLI.hpp>
#include <omp.h>
#include <map>
#include <algorithm>

#include <libmgm/mgm.hpp>

#include "argparser.hpp"

ArgParser::Arguments ArgParser::parse(int argc, char **argv) {

    try {
        this->app.parse((argc), (argv));

        if (args.max_memory != "unlimited") {
           args.max_memory_in_bytes = this->parse_memory_string_to_int(args.max_memory); 
        }

        this->args.input_file   = fs::absolute(this->args.input_file);
        this->args.output_path  = fs::absolute(this->args.output_path);
        if (*this->labeling_path_option) {
            this->args.labeling_path  = fs::absolute(this->args.labeling_path);

            if (this->args.mode != this->optimization_mode::improve_swap &&
                this->args.mode != this->optimization_mode::improve_qap &&
                this->args.mode != this->optimization_mode::improve_qap_par &&
                this->args.mode != this->optimization_mode::improveopt &&
                this->args.mode != this->optimization_mode::improveopt_par &&
                !this->args.synchronize &&
                !this->args.synchronize_infeasible)
                throw CLI::ValidationError("'labeling path' option only available in improve modes and for synchronization.");
        }

        // For incremental generation, assert agreement between mode and set size option
        if (*this->incremental_set_size_option) {
            if( this->args.mode != this->optimization_mode::inc && 
                this->args.mode != this->optimization_mode::incseq && 
                this->args.mode != this->optimization_mode::incpar)
                throw CLI::ValidationError("'incremental_set_size' option only available in 'incremental' mode.");
        }
        if (this->args.mode == this->optimization_mode::inc || 
            this->args.mode == this->optimization_mode::incseq || 
            this->args.mode == this->optimization_mode::incpar) {
            if(!(*this->incremental_set_size_option))
                throw CLI::RequiredError("'incremental' mode: Option 'set_size'");
        }
    
        mgm::QAPSolver::libmpopt_seed = this->args.libmpopt_seed;

        omp_set_num_threads(this->args.nr_threads);

        std::cout << "### Arguments passed ###" << std::endl;
        std::cout << "Input file: "             << this->args.input_file        << std::endl;
        std::cout << "Output folder: "          << this->args.output_path       << std::endl;
        std::cout << "Optimization mode: "      << this->args.mode              << std::endl;
        std::cout << "Save mode: "              << this-args.save_mode          << std::endl;
        if (*this->labeling_path_option)
            std::cout << "Labeling path: "      << this->args.labeling_path     << std::endl;
        if (*this->output_filename_option)
            std::cout << "Output filename: "    << this->args.output_filename   << std::endl;
        if (*this->synchronize_option)
            std::cout << "Running as synchronization algorithm" << std::endl;
        else if (*this->synchronize_infeasible_option)
            std::cout << "Running as synchronization algorithm. Ignoring forbidden assignments." << std::endl;
        std::cout << "########################" << std::endl;



    } catch(const CLI::ParseError &e) {
        std::exit((this->app).exit(e));
    }
    return this->args;
}

long long int ArgParser::parse_memory_string_to_int(std::string& input) {
    static const std::unordered_map<std::string, double> unit_map = {
        {"B", 1}, {"KB", 1000}, {"MB", 1000000}, {"GB", 1000000000}, {"TB", 1000000000000}
    };
    input.erase(std::remove_if(input.begin(), input.end(), ::isspace), input.end());

    size_t index = 0;
    while (index < input.size() && isdigit(input[index])) {
        index++;
    }
    long long value = std::stoll(input.substr(0, index));
    std::string unit = input.substr(index);
    for (char &c : unit) {
        c = std::toupper(c);
    }
    auto it = unit_map.find(unit);
    value *= it->second;
    return value;
}