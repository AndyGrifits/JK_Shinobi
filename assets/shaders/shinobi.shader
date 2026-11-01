// JK_Shinobi effect shaders.
// Additive textures are white-on-black: the FX system fades them by scaling the
// vertex colour, so they must not depend on vertex alpha (it is 0 unless the
// particle has 'flags useAlpha'). Alpha-blended ones (smoke, leaf, chip) need
// 'flags useAlpha' on every particle that uses them.

gfx/shinobi/glow
{
	cull none
	{
		map gfx/shinobi/glow
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

gfx/shinobi/swirl
{
	cull none
	{
		map gfx/shinobi/swirl
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

gfx/shinobi/ring
{
	cull none
	{
		map gfx/shinobi/ring
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

gfx/shinobi/wisp
{
	cull none
	{
		map gfx/shinobi/wisp
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

gfx/shinobi/spark
{
	cull none
	{
		map gfx/shinobi/spark
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
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

gfx/shinobi/bolt
{
	cull none
	{
		map gfx/shinobi/bolt
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

gfx/shinobi/chip
{
	cull none
	{
		map gfx/shinobi/chip
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// Earth wall pillars (models/shinobi/earth_pillar*.md3), lit like any model.
models/shinobi/earth_rock
{
	cull none
	{
		map models/shinobi/earth_rock
		rgbGen lightingDiffuse
	}
}

// Great fireball layers (full-colour texture, additive, tinted by the cgame)
gfx/shinobi/fire
{
	cull none
	{
		map gfx/shinobi/fire
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}
