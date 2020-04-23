project('love-nuklear', 'c',
	version : '0.1',
	default_options: [
	  'warning_level=3',
	  'c_std=gnu99',
	  'werror=true'
	],
	license: 'MIT/Expat')

cc = meson.get_compiler('c')

global_args_maybe = [
	'-Wno-unused-parameter',
	'-Wno-missing-field-initializers',
	'-Wno-pedantic',
	'-Wno-type-limits',
	'-Wno-sign-compare',
]
global_args = []

foreach a : global_args_maybe
  if cc.has_argument(a)
    global_args += a
  endif
endforeach

if get_option('buildtype') == 'release' and cc.has_argument(a)
  global_args += '-fvisibility=hidden' #this options hides most
endif

dep_lua = dependency('lua', version: '>= 5.3', required: true)

lib_nklua = static_library(
  'nklua',
  'src/nuklear_love.c',
  c_args : global_args,
  include_directories: include_directories('src'),
  dependencies : dep_lua,
)

dep_nklua = declare_dependency(
  link_with : lib_nklua,
  include_directories : include_directories('src'),
  compile_args : global_args,
  dependencies : dep_lua,
)
