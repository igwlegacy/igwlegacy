executable(
  'igw-monkey-patcher',
  'src/emit.c',
  'src/main.c',
  'src/pe.c',
  'src/payload.c',
  'src/rules.c',
  patch_sources,
  include_directories: [include_directories('src'), patch_include_dirs],
  install: true,
)
