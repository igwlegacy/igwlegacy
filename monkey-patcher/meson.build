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
  '../patches/run_pitch.c',
  '../patches/run_roll.c',
  '../patches/bob_pitch.c',
  '../patches/bob_roll.c',
  '../patches/bob_up.c',
  '../patches/screen_shake.c',
  '../patches/damage_kick.c',
  '../patches/landing_dip.c',
  '../patches/main_menu_ad.c',
  '../patches/main_menu_github.c',
  include_directories: include_directories('src', '../patches'),
  install: true,
)
