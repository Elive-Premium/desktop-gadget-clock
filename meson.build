project('clock-elive', 'c',
  version : '1.0.0',
  license        : 'BSD 2 clause',
  default_options: [ 'buildtype=release', 'c_std=gnu99', 'warning_level=2' ],
  meson_version  : '>= 0.47.0')

efl_deps = [
  dependency('elementary'),
  dependency('eina'),
  dependency('evas'),
  dependency('ecore'),
  dependency('ecore-file'),
  dependency('ecore-x'),
  dependency('edje')
]

subdir('data')
subdir('src')
