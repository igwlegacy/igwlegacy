# Each module's C blob runs inside the retail DLL, so pixi's zig builds it
# for i686 regardless of the patcher's own compiler. The patcher embeds
# <module>_blob.c; payload features include <module>_blob.h.
zig = find_program('zig')
python = find_program('python3')
blob_runtime = files('blob/runtime.c', 'blob/heap.c', 'blob/libc.c')
blob_headers = files(
  'blob/include/assert.h',
  'blob/include/blob.h',
  'blob/include/ctype.h',
  'blob/include/errno.h',
  'blob/include/libc.h',
  'blob/include/locale.h',
  'blob/include/math.h',
  'blob/include/stdio.h',
  'blob/include/stdlib.h',
  'blob/include/string.h',
  'blob/sdk/gamex86.h',
)
blob_targets = []
foreach module, sources : blob_sources
  command = [
    python, files('blob/blob.py'),
    '--module', module,
    '--zig', zig,
    '--include', meson.current_source_dir() / 'blob/include',
    '--include', meson.current_source_dir() / 'blob/sdk',
  ]
  foreach dir : blob_include_dirs.get(module, [])
    command += ['--include', dir]
  endforeach
  # These are among the inputs as well; blob.py builds them with SSE2.
  sse2_sources = blob_sse2_sources.get(module, [])
  foreach source : sse2_sources
    command += ['--sse2', source]
  endforeach
  command += [
    '--linker-script', files('blob/link.ld'),
    '--build-dir', '@PRIVATE_DIR@',
    '--source-output', '@OUTPUT0@',
    '--header-output', '@OUTPUT1@',
    '@INPUT@',
  ]
  blob_targets += custom_target(
    module + '_blob',
    input: blob_runtime + sources + sse2_sources,
    output: [module + '_blob.c', module + '_blob.h'],
    depend_files: [files('blob/blob.py', 'blob/link.ld'), blob_headers,
                   blob_depend_files.get(module, [])],
    command: command,
  )
endforeach

executable(
  'igw-monkey-patcher',
  'src/emit.c',
  'src/main.c',
  'src/pe.c',
  'src/payload.c',
  'src/rules.c',
  patch_sources,
  blob_targets,
  include_directories: [include_directories('src'), patch_include_dirs],
  install: true,
)
