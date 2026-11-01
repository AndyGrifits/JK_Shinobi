// JK_Shinobi effect shaders.
// Textures are white-on-black; the effect's rgb/alpha (vertex colour) tints and fades them.

gfx/shinobi/glow
{
	cull none
	{
		map gfx/shinobi/glow
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/shinobi/swirl
{
	cull none
	{
		map gfx/shinobi/swirl
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/shinobi/ring
{
	cull none
	{
		map gfx/shinobi/ring
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/shinobi/wisp
{
	cull none
	{
		map gfx/shinobi/wisp
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/shinobi/spark
{
	cull none
	{
		map gfx/shinobi/spark
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

// Alpha-blended (not additive): smoke and leaves must be able to darken/cover.
gfx/shinobi/smoke
{
	cull none
	{
		map gfx/shinobi/smoke
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/shinobi/leaf
{
	cull none
	{
		map gfx/shinobi/leaf
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}
