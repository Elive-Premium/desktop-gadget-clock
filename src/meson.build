sources = files('main.c')

executable('clock-gadget',
  sources,
  dependencies : efl_deps,
  install : true,
  c_args : ['-DDATA_DIR="' + join_paths(meson.current_source_dir(), '..', 'data') + '"']
)
