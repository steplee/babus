project('babus-cpp', ['c', 'cpp'],
    default_options : [
      'default_library=static',
      'cpp_std=c++17',
      'buildtype=debugoptimized',
      'optimization=2',
      'python.platlibdir=@0@/lib/python3/dist-packages/'.format(get_option('prefix')),
      'python.purelibdir=@0@/lib/python3/dist-packages/'.format(get_option('prefix')),
      ]
    )

'''
fmt_dep = dependency('fmt', required: true, fallback: 'fmt',
  default_options: ['default_library=static']
)
'''

# add_global_arguments('-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE', language: 'cpp')
add_global_arguments('-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG', language: 'cpp')

spdlog = subproject('spdlog',
  required: true,
  # default_options: ['external_fmt=enabled', 'compile_library=true', 'tests=false', 'default_library=static']
  default_options: ['compile_library=true', 'tests=false', 'default_library=static']
  # default_options: ['compile_library=false', 'tests=false', 'default_library=static']
)
spdlog_dep = spdlog.get_variable('spdlog_dep')

fs_dep = declare_dependency(dependencies: meson.get_compiler('cpp').find_library('stdc++fs'))





base_inc = include_directories('.', './babus', './subprojects/')
base_dep = declare_dependency(
  dependencies: [spdlog_dep, fs_dep],
  include_directories: base_inc
  )



babus = library(
  'babus',
  files(
    'babus/fs/mmap.cc',
    'babus/domain.cc',
    'babus/client.cc',
    'babus/waiter.cc',
    ),
  dependencies: [base_dep],
  install: true,
  )

babus_dep = declare_dependency(
  dependencies: [base_dep],
  link_with: [babus]
  )


# FFI Library for rust bindings
babus_ffi = library(
  'babus_ffi',
  files('babus/ffi.cc'),
  dependencies: [babus_dep],
  install: true)



# Main runner for current testing.
executable(
  'runBabus',
  files('babus/app/main.cc'),
  dependencies: [babus_dep])



if get_option('tests').enabled()
  gtest_main_dep = dependency('gtest_main', required: true)

  tests = executable('tests',
    files(
      'babus/test/futex.cc',
      'babus/test/waiter.cc',
      ),
    dependencies: [babus_dep, gtest_main_dep])
endif

if get_option('benchmarks').enabled()
  gbenchmark_dep = dependency('benchmark', required: true)
  # benchmark_dep = gbenchmark.dependency('benchmark')

  executable('runProfileBabus',
    files('babus/benchmark/profileBabus.cc'),
    dependencies: [babus_dep],
    install: false)
endif

if get_option('profileRedis').enabled()
  subdir('babus/benchmark/profileRedis')
endif
