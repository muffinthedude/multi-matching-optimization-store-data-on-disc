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

stxxl_proj = subproject('stxxl')
stxxl_dep  = stxxl_proj.get_variable('stxxl_dep')

openmp = dependency('openmp')

include_dirs = include_directories('.')

libmgm = static_library(
                    'libmgm', 
                    sources,
                    include_directories: include_dirs,
                    dependencies: [libqpbo_dep, libmpopt_dep, spdlog_dep, json_dep, openmp, unordered_dense_dep, lsap_dep, stxxl_dep],
                    install: false)

libmgm_dep = declare_dependency(include_directories : include_dirs, link_with : libmgm)