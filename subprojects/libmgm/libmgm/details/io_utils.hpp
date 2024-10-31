#ifndef LIBMGM_IO_UTILS_HPP
#define LIBMGM_IO_UTILS_HPP

#include <filesystem>
#include "multigraph.hpp"
#include "solution.hpp"

namespace mgm::io {

std::shared_ptr<MgmModelBase> parse_dd_file(std::filesystem::path dd_file);
std::shared_ptr<MgmModelBase> parse_dd_file_fscan(std::filesystem::path dd_file);

void safe_to_disk(const MgmSolution& solution, std::filesystem::path outPath, std::string filename);
MgmSolution import_from_disk(std::shared_ptr<MgmModelBase> model, std::filesystem::path labeling_path);

}
#endif