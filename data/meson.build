edje_cc = find_program('edje_cc')

default_edj = custom_target('default.edj',
  input : ['themes/default.edc', 'themes/lcd.ttf'],
  output : 'default.edj',
  command : [edje_cc, '-fd', meson.current_source_dir() + '/themes', '@INPUT0@', '-o', '@OUTPUT@'],
  depend_files : ['themes/lcd.ttf'],
  install : true,
  install_dir : get_option('datadir') / 'clock-gadget/data/themes'
)
