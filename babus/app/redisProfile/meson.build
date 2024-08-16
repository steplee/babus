cmake = import('cmake')

hiredis_opts = cmake.subproject_options()
hiredis_opts.add_cmake_defines({'BUILD_SHARED_LIBS': 'OFF'})
hiredis = cmake.subproject('hiredis', options: hiredis_opts)
hiredis_dep = hiredis.dependency('hiredis')

redispp_opts = cmake.subproject_options()
redispp_opts.add_cmake_defines({'BUILD_SHARED_LIBS': 'OFF'})
redispp_opts.add_cmake_defines({'HIREDIS_HEADER': 'hiredis.h'})
redispp_opts.add_cmake_defines({'HIREDIS_HEADER': 'hiredis.h'})
redispp_opts.add_cmake_defines({'CMAKE_CXX_FLAGS': '-I' + meson.source_root() + '/subprojects/'})
# redispp_opts.add_cmake_defines({'HIREDIS_LIB': 'libhiredis.so'})
# redispp_opts.add_cmake_defines({'TEST_HIREDIS_LIB': 'libhiredis.so'})
redispp_opts.add_cmake_defines({'HIREDIS_LIB': 'libhiredis.a'})
redispp_opts.add_cmake_defines({'TEST_HIREDIS_LIB': 'libhiredis.a'})
redispp_opts.add_cmake_defines({'REDIS_PLUS_PLUS_BUILD_SHARED': 'OFF'})
redispp_opts.add_cmake_defines({'REDIS_PLUS_PLUS_BUILD_STATIC': 'ON'})
redispp_opts.append_link_args('-L' + meson.build_root() + '/subprojects/hiredis')
redispp = cmake.subproject('redis-plus-plus', options: redispp_opts)
redispp_dep = redispp.dependency('redis++_static')
redispp_dep = declare_dependency(
   dependencies: [redispp_dep, hiredis_dep],
   link_args: ['-L' + meson.build_root() + '/subprojects/hiredis'],
   include_directories: base_inc
  )




executable('profileRedis',
  files('main.cc'),
  dependencies: [base_dep, redispp_dep, hiredis_dep]
  )
