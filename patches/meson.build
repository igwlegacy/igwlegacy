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
		'toml_waves/waves.c',
		'../vendor/tomlc17/src/tomlc17.c',
	),
}
blob_include_dirs = {
	'game': [meson.project_source_root() / 'vendor/tomlc17/src'],
}
blob_depend_files = {
	'game': files('../vendor/tomlc17/src/tomlc17.h'),
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
