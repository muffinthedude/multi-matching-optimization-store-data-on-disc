#ifndef LIBMGM_IO_UTILS_HPP
#define LIBMGM_IO_UTILS_HPP

#include <filesystem>
#include "multigraph.hpp"
#include "solution.hpp"

namespace mgm::io {

enum disc_save_mode {
    no,
    sql,
    rocksdb
};

enum cache_mode {
    recent,
    preload,
    bulk
};

std::shared_ptr<MgmModelBase> parse_dd_file(std::filesystem::path dd_file, disc_save_mode save_mode, cache_mode cache_mode_setter, long long int memory_limit = 0);
std::shared_ptr<MgmModelBase> parse_dd_file_fscan(std::filesystem::path dd_file, disc_save_mode save_mode);

void safe_to_disk(const MgmSolution& solution, std::filesystem::path outPath, std::string filename);
MgmSolution import_from_disk(std::shared_ptr<MgmModelBase> model, std::filesystem::path labeling_path);

}
#endif