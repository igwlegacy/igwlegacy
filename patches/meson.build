# Refresh at build time so a new commit updates incremental builds as well.
git_version = vcs_tag(
	input: 'version.h.in',
	output: 'version.h',
	command: ['git', 'describe', '--tags', '--always'],
	fallback: 'unknown',
)

patch_sources = [git_version] + files(
	'version.c',
	'disable_bobbing.c',
	'fog.c',
	'ambient_sounds.c',
	'screen_shake.c',
	'damage_kick.c',
	'landing_dip.c',
	'death_camera.c',
	'respawn_countdown.c',
	'centerprint_position.c',
	'shotgun.c',
	'hmg_reload.c',
	'main_menu_ad.c',
	'main_menu_github.c',
	'loading_background.c',
	'loading_menu.c',
	# 'autoplay.c',
	'valuable_markers.c',
	'stronghold_markers.c',
	'pickup_markers.c',
	'skip_intro.c',
	'console_stdout.c',
	'use_shaders.c',
	'heat_haze.c',
	'spawn_protection.c',
	'team_repair.c',
	'rts_spectator.c',
	# 'rts_buildable.c',
)

patch_include_dirs = include_directories('.')
