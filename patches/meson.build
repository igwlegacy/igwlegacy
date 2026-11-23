# Refresh at build time so a new commit updates incremental builds as well.
git_version = vcs_tag(
	input: 'version.h.in',
	output: 'version.h',
	command: ['git', 'describe', '--tags', '--always'],
	fallback: 'unknown',
)

# The TOML wave blob runs inside gamex86.dll, so pixi's zig builds it for
# i686 regardless of the patcher's own compiler.
zig = find_program('zig')
python = find_program('python3')
toml_waves_blob = custom_target(
	'toml_waves_blob',
	input: files(
		'toml_waves/runtime.c',
		'toml_waves/libc.c',
		'toml_waves/waves.c',
		'../vendor/tomlc17/src/tomlc17.c',
	),
	output: 'toml_waves_blob.h',
	depend_files: files(
		'toml_waves/blob.py',
		'toml_waves/link.ld',
		'toml_waves/game.h',
		'toml_waves/include/libc.h',
		'../vendor/tomlc17/src/tomlc17.h',
	),
	command: [
		python, files('toml_waves/blob.py'),
		'--zig', zig,
		'--include', meson.current_source_dir() / 'toml_waves/include',
		'--include', meson.current_source_dir() / 'toml_waves',
		'--include', meson.project_source_root() / 'vendor/tomlc17/src',
		'--linker-script', files('toml_waves/link.ld'),
		'--build-dir', '@PRIVATE_DIR@',
		'--output', '@OUTPUT@',
		'@INPUT@',
	],
)

patch_sources = [git_version, toml_waves_blob] + files(
	'version.c',
	'no_steam.c',
	'disable_bobbing.c',
	'fog.c',
	'maxfps.c',
	'net_defaults.c',
	# 'ambient_sounds.c',
	'screen_shake.c',
	'damage_kick.c',
	'burning_screen.c',
	'landing_dip.c',
	'reverse_weapon_anims.c',
	'death_camera.c',
	'respawn_countdown.c',
	'centerprint_position.c',
	'cursor_size.c',
	'cursor_position.c',
	'main_menu_ad.c',
	'main_menu_github.c',
	'main_menu_top_edge.c',
	'loading_background.c',
	'loading_menu.c',
	# 'autoplay.c',
	'valuable_markers.c',
	'stronghold_markers.c',
	'pickup_markers.c',
	'skip_intro.c',
	'bot_slots.c',
	'flood_protect.c',
	'console_stdout.c',
	'console_visibility.c',
	'window_close.c',
	'use_shaders.c',
	'heat_haze.c',
	'rts_spectator.c',
	'rts_grid.c',
	'difficulty_primordial.c',
	'toml_waves.c',
	'max_vehicles.c',
	# 'rts_buildable.c',

	# balance changes
	# 'spawn_protection.c',
	# 'musket_reload.c',
	# 'musket_zoom.c',
	# 'hmg_reload.c',
	# 'shotgun.c',
	# 'team_repair.c',
)

patch_include_dirs = include_directories('.')
