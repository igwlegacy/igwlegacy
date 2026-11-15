// Glowing, see-through ground markers drawn under spawnable items.
// Each MD3 surface references one of these shader names, set by
// MODEL_RULES in dodo.py; all share the same look and differ only in their
// texture, shipped as models/mapobjects/item_highlight/<name>.dds.

// Round cyan marker.
models/mapobjects/item_highlight/item_highlight
{
	surfaceparm nomarks
	surfaceparm nolightmap
	surfaceparm trans
	// Visible from below as well, e.g. through grates or glass floors.
	cull none
	// The marker lies exactly on the floor; offset it to avoid z-fighting.
	polygonOffset
	sort additive
	{
		// Clamp so the transparent border never wraps onto the opposite edge.
		clampmap models/mapobjects/item_highlight/item_highlight.dds
		// Additive light weighted by the texture alpha, unaffected by lighting.
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen identity
		// Gentle pulse between 70% and 100% intensity, one cycle per 2 s.
		alphaGen wave sin 0.85 0.15 0 0.5
	}
}

// Round yellow marker with a lightning bolt.
models/mapobjects/item_highlight/item_highlight_power
{
	surfaceparm nomarks
	surfaceparm nolightmap
	surfaceparm trans
	// Visible from below as well, e.g. through grates or glass floors.
	cull none
	// The marker lies exactly on the floor; offset it to avoid z-fighting.
	polygonOffset
	sort additive
	{
		// Clamp so the transparent border never wraps onto the opposite edge.
		clampmap models/mapobjects/item_highlight/item_highlight_power.dds
		// Additive light weighted by the texture alpha, unaffected by lighting.
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen identity
		// Gentle pulse between 70% and 100% intensity, one cycle per 2 s.
		alphaGen wave sin 0.85 0.15 0 0.5
	}
}
