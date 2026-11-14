project(
  'igw-monkey-patcher', 'c',
  version: '0.1.0',
  meson_version: '>=1.0',
  default_options: ['c_std=c17', 'warning_level=3', 'buildtype=release'],
)

executable(
  'igw-monkey-patcher',
  'src/main.c',
  'src/pe.c',
  'src/payload.c',
  'src/rules.c',
  'patches/cvar.c',
  'patches/code.c',
  include_directories: include_directories('src'),
  install: true,
)
