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
]

add_project_arguments(
  cc.get_supported_arguments(global_args_maybe),
  language : 'c'
)

dep_lua = dependency('lua', version: '>= 5.3', required: false)
if not dep_lua.found()
  dep_lua = dependency('lua-5.3', version: '>= 5.3', required: true)
endif

dep_nklua = declare_dependency(
  include_directories : include_directories('src'),
  dependencies : dep_lua,
)
