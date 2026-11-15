// Tint drawn over buildable terrain in RTS mode. Constant colours skip
// the engine's overbright compensation, so an alpha-blended pure green
// buries the mostly dark ground. Instead, damp red and blue to shift
// the ground towards green, then add a little green. This keeps the
// ground's texture fully readable beneath the tint.
igwlegacy/buildable
{
	surfaceparm nomarks
	surfaceparm nolightmap
	surfaceparm nodlight
	polygonOffset
	sort decal
	{
		map $whiteimage
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen const ( 0.6 1 0.6 )
	}
	{
		map $whiteimage
		blendFunc GL_ONE GL_ONE
		rgbGen const ( 0 0.1 0 )
	}
}
