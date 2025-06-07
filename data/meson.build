edje_cc = find_program('edje_cc')

theme_edj = custom_target('theme.edj',
  input : 'theme.edc',
  output : 'theme.edj',
  command : [edje_cc, '@INPUT@', '-o', '@OUTPUT@'],
  install : true,
  install_dir : get_option('datadir') / 'clock-gadget'
)
