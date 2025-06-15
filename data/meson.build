edje_cc = find_program('edje_cc')

default_edj = custom_target('default.edj',
  input : 'themes/default.edc',
  output : 'default.edj',
  command : [
    edje_cc,
    '-fd', meson.current_source_dir() + '/themes',
    '-id', meson.current_source_dir() + '/themes',
    '@INPUT@', '-o', '@OUTPUT@'
  ],
  install : true,
  install_dir : get_option('datadir') / 'clock-gadget/data/themes'
)
