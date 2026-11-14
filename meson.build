project(
  'igw-monkey-patcher', 'c',
  version: '0.1.0',
  meson_version: '>=1.0',
  default_options: ['c_std=c17', 'warning_level=3', 'buildtype=release'],
)

# Load feature selection before the patcher consumes its sources and includes.
subdir('patches')
subdir('monkey-patcher')
