# Refresh at build time so a new commit updates incremental builds as well.
git_version = vcs_tag(
	input: 'version.h.in',
	output: 'version.h',
	command: ['git', 'describe', '--tags', '--always'],
	fallback: 'unknown',
)

# C code that runs inside a retail module goes into that module's blob,
# see monkey-patcher/meson.build: its sources, the include directories
# beyond the blob runtime's, and headers that a rebuild depends on.
blob_sources = {
	'game': files(
		'wave_scripts/scripts.c',
		'wave_scripts/toml.c',
		'../vendor/tomlc17/src/tomlc17.c',
	),
}
blob_include_dirs = {
	'game': [
		meson.project_source_root() / 'vendor/tomlc17/src',
		meson.project_source_root() / 'vendor/lua/src',
		meson.project_source_root() / 'vendor/musl/src/internal',
		meson.current_source_dir() / 'wave_scripts/musl',
	],
}
blob_depend_files = {
	'game': files(
		'wave_scripts/scripts.h',
		'wave_scripts/lua_runtime.h',
		'wave_scripts/musl/endian.h',
		'wave_scripts/musl/features.h',
		'wave_scripts/musl/fp_arch.h',
		'../vendor/tomlc17/src/tomlc17.h',
	),
}
# Further sources, which do their floating point with SSE2, see "C blobs"
# in monkey-patcher/docs/README.md: the Lua wave scripts, whose results
# must not depend on the CPU or on the game's x87 precision, the Lua
# runtime (whose own sources lua_runtime.c includes) and musl's math
# functions for it.
musl_math = []
foreach name : [
	'acos.c', 'asin.c', 'atan.c', 'atan2.c', 'ceil.c', '__cos.c', 'cos.c',
	'exp.c', 'exp_data.c', 'fabs.c', 'floor.c', 'fmod.c', 'frexp.c',
	'ldexp.c', 'log.c', 'log10.c', 'log2.c', 'log2_data.c', 'log_data.c',
	'__math_divzero.c', '__math_invalid.c', '__math_oflow.c',
	'__math_uflow.c', '__math_xflow.c', 'pow.c', 'pow_data.c',
	'__rem_pio2.c', '__rem_pio2_large.c', 'scalbn.c', '__sin.c', 'sin.c',
	'sqrt.c', 'sqrt_data.c', '__tan.c', 'tan.c',
]
	musl_math += files('../vendor/musl/src/math' / name)
endforeach
blob_sse2_sources = {
	'game': files(
		'wave_scripts/lua.c',
		'wave_scripts/lua_runtime.c',
	) + musl_math,
}

patch_sources = [git_version] + files(
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
	'landing_bounce.c',
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
	'difficulty_titan.c',
	'wave_scripts.c',
	'max_vehicles.c',
	'unpurchasable.c',
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
