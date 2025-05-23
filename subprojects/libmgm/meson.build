project('libmgm', ['cpp'], default_options: ['cpp_std=c++17'])

sources =  [
  'libmgm/details/io_utils.cpp',
  'libmgm/details/multigraph.cpp',
  'libmgm/details/costs.cpp',
  'libmgm/details/lap_interface.cpp',
  'libmgm/details/logger.cpp',
  'libmgm/details/solution.cpp',
  'libmgm/details/qap_interface.cpp',
  'libmgm/details/cliques.cpp',
  'libmgm/details/solver_mgm.cpp',
  'libmgm/details/solver_local_search.cpp',
  'libmgm/details/solver_ab_swap.cpp',
  'libmgm/details/solver_incremental.cpp',
  'libmgm/details/synchronization.cpp'
]

libqpbo_proj  = subproject('libqpbo')
libqpbo_dep   = libqpbo_proj.get_variable('libqpbo_dep')

libmpopt_proj  = subproject('libmpopt')
libmpopt_dep   = libmpopt_proj.get_variable('libmpopt_dep')

json_proj  = subproject('nlohmann_json')
json_dep   = json_proj.get_variable('nlohmann_json_dep')

unordered_dense  = subproject('unordered_dense')
unordered_dense_dep   = unordered_dense.get_variable('unordered_dense_dep')

spdlog_proj  = subproject('spdlog')
spdlog_dep   = spdlog_proj.get_variable('spdlog_dep', 'external_fmt=false')

lsap_proj  = subproject('scipy_lsap')
lsap_dep   = lsap_proj.get_variable('liblsap_dep')

cereal_proj = subproject('cereal')
cereal_dep = cereal_proj.get_variable('cereal_dep')

sqlite3_proj = subproject('sqlite3')
sqlite3_dep = sqlite3_proj.get_variable('sqlite3_dep')

openmp = dependency('openmp')

# Load stxxl as cmake dependency
cmake = import('cmake')
rocksdb = cmake.subproject('rocksdb', cmake_options: ['-DWITH_LZ4=ON'])
rocksdb_dep = rocksdb.dependency('rocksdb')

include_dirs = include_directories('.')

libmgm = static_library(
                    'libmgm', 
                    sources,
                    include_directories: include_dirs,
                    dependencies: [libqpbo_dep, libmpopt_dep, spdlog_dep, json_dep, openmp, unordered_dense_dep, lsap_dep, cereal_dep, sqlite3_dep, rocksdb_dep],
                    install: false)

libmgm_dep = declare_dependency(include_directories : include_dirs, link_with : libmgm)