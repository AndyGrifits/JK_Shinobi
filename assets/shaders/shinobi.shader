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

// Kawarimi log (models/shinobi/log.md3)
models/shinobi/log
{
	cull none
	{
		map models/shinobi/log
		rgbGen lightingDiffuse
	}
}

// ---- elements -------------------------------------------------------------
// Ground decals are drawn by the cgame as flat polys with vertex colour/alpha.
gfx/shinobi/puddle
{
	cull none
	polygonOffset
	{
		map gfx/shinobi/puddle
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
		tcMod rotate 4
	}
}

gfx/shinobi/mud
{
	cull none
	polygonOffset
	{
		map gfx/shinobi/mud
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// a frozen puddle (SZ_ICE decal): frost plus a moving glint
gfx/shinobi/ice
{
	cull none
	polygonOffset
	{
		map gfx/shinobi/ice
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
	{	// a slow shimmer over the ice itself (weighted by its alpha: no square around it)
		map gfx/shinobi/ice
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen wave sin 0.18 0.1 0 0.4
		alphaGen vertex
	}
}

// the same ice as a floe on map water: drawn after the (translucent) water, which would
// otherwise cover it - decals sort before it
gfx/shinobi/icefloe
{
	cull none
	sort nearest
	{
		map gfx/shinobi/ice
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
	{
		map gfx/shinobi/ice
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen wave sin 0.18 0.1 0 0.4
		alphaGen vertex
	}
}

// loose earth left by Doton (SZ_DUST decal)
gfx/shinobi/sand
{
	cull none
	polygonOffset
	{
		map gfx/shinobi/sand
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// puddle surface (a raised lens drawn by the cgame): rippling water + a moving sheen.
// Vertex colour = how bright the sheen is, vertex alpha = how solid the water is.
gfx/shinobi/puddlewater
{
	cull none
	sort 9
	{
		map gfx/shinobi/watertile
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen const ( 0.55 0.7 0.85 )
		alphaGen vertex
		tcMod turb 0 0.03 0 0.12
		tcMod scroll 0.03 0.015
	}
	{
		map gfx/shinobi/watertile
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		tcGen environment
		tcMod scroll -0.02 0.03
	}
}

// the same with the game's own Bespin water texture (used when it is installed)
gfx/shinobi/puddlewater_stock
{
	cull none
	sort 9
	{
		map textures/bespin/water1
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen const ( 0.8 0.85 0.95 )
		alphaGen vertex
		tcMod turb 1 0.03 0 0.3
		tcMod scroll 0.02 0.04
	}
	{
		map textures/bespin/water1
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
		tcMod turb 0 -0.03 0.5 -0.3
		tcMod scroll -0.03 -0.02
	}
}

// Stone Skin / Earth Clone: the body drawn over as rough stone
gfx/shinobi/stoneshell
{
	deformVertexes wave 100 sin 0.6 0 0 0
	{
		map models/shinobi/earth_rock
		rgbGen lightingDiffuse
		tcMod scale 2 2
	}
}

gfx/shinobi/scorch
{
	cull none
	polygonOffset
	{
		map gfx/shinobi/scorch
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// Water Prison ball and water bullet (sprites, alpha from the cgame)
gfx/shinobi/watersphere
{
	cull none
	{
		map gfx/shinobi/watersphere
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/shinobi/droplet
{
	cull none
	{
		map gfx/shinobi/droplet
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

gfx/shinobi/streak
{
	cull none
	{
		map gfx/shinobi/streak
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

gfx/shinobi/shadowband
{
	cull none
	polygonOffset
	{
		map gfx/shinobi/shadowband
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
		tcMod scroll -0.5 0
	}
}

// Kunai (models/shinobi/kunai.md3)
models/shinobi/kunai
{
	cull none
	{
		map models/shinobi/kunai
		rgbGen lightingDiffuse
	}
	{
		map gfx/shinobi/glow
		blendFunc GL_ONE GL_ONE
		rgbGen const ( 0.12 0.12 0.14 )
		tcGen environment
	}
}

// Hidden Mist: a flat sheet of fog right in front of the camera (sprite)
gfx/shinobi/fogscreen
{
	nopicmip
	nomipmaps
	cull none
	sort nearest
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// dark shapes of people seen through the Hidden Mist (custom shader on the model)
gfx/shinobi/mistshell
{
	nopicmip
	nomipmaps
	cull none
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}

// Explosive kunai (models/shinobi/exkunai.md3): the kunai with a tag round the grip
models/shinobi/exkunai
{
	cull none
	{
		map models/shinobi/exkunai
		rgbGen lightingDiffuse
	}
}

// Explosive tag (models/shinobi/tag.md3)
models/shinobi/tag
{
	cull none
	polygonOffset
	{
		map models/shinobi/tag
		rgbGen lightingDiffuse
	}
}

// pills on the ground: a soft glow so they can be spotted
models/shinobi/pill_heal
{
	{
		map models/shinobi/pill_heal
		rgbGen lightingDiffuse
	}
	{
		map models/shinobi/pill_heal
		blendFunc GL_ONE GL_ONE
		rgbGen wave sin 0.25 0.15 0 0.7
	}
}

models/shinobi/pill_chakra
{
	{
		map models/shinobi/pill_chakra
		rgbGen lightingDiffuse
	}
	{
		map models/shinobi/pill_chakra
		blendFunc GL_ONE GL_ONE
		rgbGen wave sin 0.25 0.15 0 0.7
	}
}

// the guard bubble around someone blocking (half sphere turned to the viewer; the
// colour - blue / yellow / red by the guard left - comes from the entity)
gfx/shinobi/guardshell
{
	cull none
	nopicmip
	{
		map gfx/shinobi/guardshell
		blendFunc GL_ONE GL_ONE
		rgbGen entity
		tcGen environment
	}
}

// Kaiten: the spinning dome of chakra (rgbGen entity: the colour from the cgame)
gfx/shinobi/kaitenshell
{
	cull none
	nopicmip
	{
		map gfx/shinobi/guardshell
		blendFunc GL_ONE GL_ONE
		rgbGen entity
		tcGen environment
	}
	{
		map gfx/shinobi/kaitenswirl
		blendFunc GL_ONE GL_ONE
		rgbGen entity
		tcGen environment
		tcMod rotate 720
	}
	{
		map gfx/shinobi/kaitenswirl
		blendFunc GL_ONE GL_ONE
		rgbGen entity
		tcGen environment
		tcMod rotate -450
		tcMod scale 0.8 0.8
	}
}

// Sharingan: a dark red rim around the view (2D, alpha from the texture)
gfx/shinobi/vignette
{
	nopicmip
	{
		clampmap gfx/shinobi/vignette
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// Sharingan: the eye in the corner of the view
gfx/shinobi/tomoe
{
	nopicmip
	{
		clampmap gfx/shinobi/tomoe
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// Sharingan: a thin red rim of chakra around everyone in sight (the body again, a hair
// bigger, bright only at its edges)
gfx/shinobi/sharinganrim
{
	cull none
	deformVertexes wave 100 sin 0.5 0 0 0
	{
		map gfx/shinobi/guardshell
		blendFunc GL_ONE GL_ONE
		rgbGen entity
		tcGen environment
	}
}

// anime outline, 1 of 3 (cg_shinobiOutline 1): back faces of the model pushed out 0.5 units, black
gfx/shinobi/outline1
{
	cull back
	deformVertexes wave 100 sin 0.5 0 0 0
	{
		map $whiteimage
		rgbGen const ( 0 0 0 )
	}
}

// anime outline, 2 of 3 (cg_shinobiOutline 2): back faces of the model pushed out 0.9 units, black
gfx/shinobi/outline2
{
	cull back
	deformVertexes wave 100 sin 0.9 0 0 0
	{
		map $whiteimage
		rgbGen const ( 0 0 0 )
	}
}

// anime outline, 3 of 3 (cg_shinobiOutline 3): back faces of the model pushed out 1.4 units, black
gfx/shinobi/outline3
{
	cull back
	deformVertexes wave 100 sin 1.4 0 0 0
	{
		map $whiteimage
		rgbGen const ( 0 0 0 )
	}
}

// ---- weapons --------------------------------------------------------------
// the mark a blade leaves on a body (g2MarksShader of the shinobi_*.sab): a dark cut,
// not the saber's glowing burn
gfx/shinobi/cutmark
{
	polygonOffset
	{
		map gfx/shinobi/cutmark
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		alphaGen vertex
	}
}

// Wind Edge: the dark crescent flying out (vertex colour and alpha, CG_ShinobiWindSlash)
gfx/shinobi/windarc
{
	cull none
	{
		map gfx/shinobi/windarc
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// Inner Gates (the third and on): the body glows crimson (rgbGen entity: the colour from the cgame)
gfx/shinobi/gateshell
{
	deformVertexes wave 100 sin 0.4 0 0 0
	{
		map gfx/shinobi/gateskin
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
		tcMod scale 2 2
		tcMod scroll 0 0.05
	}
	{
		map gfx/shinobi/guardshell
		blendFunc GL_ONE GL_ONE
		rgbGen entity
		tcGen environment
		tcMod scroll 0 0.4
	}
}

// Inner Gates: the leg weights (tools/gen_models.py)
models/shinobi/leg_weight
{
	{
		map models/shinobi/leg_weight
		rgbGen lightingDiffuse
	}
}

// 64 Seals: Neji's eight-trigram seal on the ground (alpha keeps the black of the yin-yang; the
// second pass makes the green glow in the dark). rgbGen / alphaGen vertex: the cgame reveals it
gfx/shinobi/hakke_seal
{
	cull none
	nopicmip
	polygonOffset
	{
		clampmap gfx/shinobi/hakke_seal
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
	{
		clampmap gfx/shinobi/hakke_seal
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

// a blow landed: the body lit white for a moment (rgbGen entity: the brightness from the cgame)
gfx/shinobi/hitflash
{
	{
		map $whiteimage
		blendFunc GL_ONE GL_ONE
		rgbGen entity
	}
}

// a star circling over a stunned head (cg_shinobi.c CG_ShinobiStunStars)
gfx/shinobi/stunstar
{
	nopicmip
	cull none
	{
		map gfx/shinobi/stunstar
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}
