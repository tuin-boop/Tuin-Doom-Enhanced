// Heretic source code, Copyright Activision 1996-1998 (Raven Software).
// Released under the GNU General Public License; see THIRD_PARTY.md.

// R_data.c

#include "doomdef.h"
#include "r_local.h"
#include "p_local.h"

extern void CheckAbortStartup(void);

typedef struct
{
	int		originx;	// block origin (allways UL), which has allready
	int		originy;	// accounted  for the patch's internal origin
	int		patch;
} texpatch_t;

// a maptexturedef_t describes a rectangular texture, which is composed of one
// or more mappatch_t structures that arrange graphic patches
typedef struct
{
	char		name[8];		// for switch changing, etc
	short		width;
	short		height;
	short		patchcount;
	texpatch_t	patches[1];		// [patchcount] drawn back to front
								//  into the cached texture
} texture_t;



int		firstflat, lastflat, numflats;
int		firstpatch, lastpatch, numpatches;
int		firstspritelump, lastspritelump, numspritelumps;

int			numtextures;
texture_t	**textures;
int			*texturewidthmask;
fixed_t		*textureheight;		// needed for texture pegging
int			*texturecompositesize;
short		**texturecolumnlump;
uint32_t		**texturecolumnofs;	// DERP: 32-bit, for composites past 64k
byte		**texturecomposite;

int			*flattranslation;		// for global animation
int			*texturetranslation;	// for global animation

fixed_t		*spritewidth;		// needed for pre rendering
fixed_t		*spriteoffset;
fixed_t		*spritetopoffset;

lighttable_t	*colormaps;


/*
==============================================================================

						MAPTEXTURE_T CACHING

when a texture is first needed, it counts the number of composite columns
required in the texture and allocates space for a column directory and any
new columns.  The directory will simply point inside other patches if there
is only one patch in a given column, but any columns with multiple patches
will have new column_ts generated.

==============================================================================
*/

/*
===================
=
= R_DrawColumnInCache
=
= Clip and draw a column from a patch into a cached post
=
===================
*/

void R_DrawColumnInCache (column_t *patch, byte *cache, int originy, int cacheheight)
{
	int		count, position;
	byte	*source, *dest;
	
	dest = (byte *)cache + 3;
	
	while (patch->topdelta != 0xff)
	{
		source = (byte *)patch + 3;
		count = patch->length;
		position = originy + patch->topdelta;
		if (position < 0)
		{
			count += position;
			position = 0;
		}
		if (position + count > cacheheight)
			count = cacheheight - position;
		if (count > 0)
			memcpy (cache + position, source, count);
		
		patch = (column_t *)(  (byte *)patch + patch->length
+ 4);
	}
}


/*
===================
=
= R_GenerateComposite
=
===================
*/

void R_GenerateComposite (int texnum)
{
	byte		*block;
	texture_t	*texture;
	texpatch_t	*patch;	
	patch_t		*realpatch;
	int			x, x1, x2;
	int			i;
	column_t	*patchcol;
	short		*collump;
	uint32_t *colofs;
	
	texture = textures[texnum];
	block = Z_Malloc (texturecompositesize[texnum], PU_STATIC, 
		&texturecomposite[texnum]);	
	collump = texturecolumnlump[texnum];
	colofs = texturecolumnofs[texnum];
		
//
// composite the columns together
//
	patch = texture->patches;
		
	for (i=0 , patch = texture->patches; i<texture->patchcount ; i++, patch++)
	{
		realpatch = W_CacheLumpNum (patch->patch, PU_CACHE);
		x1 = patch->originx;
		x2 = x1 + SHORT(realpatch->width);

		if (x1<0)
			x = 0;
		else
			x = x1;
		if (x2 > texture->width)
			x2 = texture->width;

		for ( ; x<x2 ; x++)
		{
			if (collump[x] >= 0)
				continue;		// column does not have multiple patches
			patchcol = (column_t *)((byte *)realpatch + 
				LONG(realpatch->columnofs[x-x1]));
			R_DrawColumnInCache (patchcol, block + colofs[x], patch->originy,
			texture->height);
		}
						
	}

// now that the texture has been built, it is purgable
	Z_ChangeTag (block, PU_CACHE);
}


/*
===================
=
= R_GenerateLookup
=
===================
*/

void R_GenerateLookup (int texnum)
{
	texture_t	*texture;
	byte		*patchcount;		// [texture->width]
	texpatch_t	*patch;	
	patch_t		*realpatch;
	int			x, x1, x2;
	int			i;
	short		*collump;
	uint32_t	*colofs;
	
	texture = textures[texnum];

	texturecomposite[texnum] = 0;	// composited not created yet
	texturecompositesize[texnum] = 0;
	collump = texturecolumnlump[texnum];
	colofs = texturecolumnofs[texnum];
	
//
// count the number of columns that are covered by more than one patch
// fill in the lump / offset, so columns with only a single patch are
// all done
//
	patchcount = (byte *)alloca (texture->width);
	memset (patchcount, 0, texture->width);
	patch = texture->patches;
		
	for (i=0 , patch = texture->patches; i<texture->patchcount ; i++, patch++)
	{
		realpatch = W_CacheLumpNum (patch->patch, PU_CACHE);
		x1 = patch->originx;
		x2 = x1 + SHORT(realpatch->width);
		if (x1 < 0)
			x = 0;
		else
			x = x1;
		if (x2 > texture->width)
			x2 = texture->width;
		for ( ; x<x2 ; x++)
		{
			patchcount[x]++;
			collump[x] = patch->patch;
			colofs[x] = LONG(realpatch->columnofs[x-x1])+3;
		}
	}
	
	for (x=0 ; x<texture->width ; x++)
	{
		if (!patchcount[x])
		{
			printf ("R_GenerateLookup: column without a patch (%s)\n", texture->name);
			return;
		}
//			I_Error ("R_GenerateLookup: column without a patch");
		if (patchcount[x] > 1)
		{
			collump[x] = -1;	// use the cached block
			colofs[x] = texturecompositesize[texnum];
			// DERP: no 64k limit; the 2025 IWAD has larger multi-patch textures.
			texturecompositesize[texnum] += texture->height;
		}
	}	
}


/*
================
=
= R_GetColumn
=
================
*/

byte *R_GetColumn (int tex, int col)
{
	int	lump, ofs;
	
	col &= texturewidthmask[tex];
	lump = texturecolumnlump[tex][col];
	ofs = texturecolumnofs[tex][col];
	if (lump > 0)
		return (byte *)W_CacheLumpNum(lump,PU_CACHE)+ofs;
	if (!texturecomposite[tex])
		R_GenerateComposite (tex);
	return texturecomposite[tex] + ofs;
}


/*
==================
=
= R_InitTextures
=
= Initializes the texture list with the textures from the world map
=
==================
*/

void R_InitTextures (void)
{
	maptexture_t	*mtexture;
	texture_t		*texture;
	mappatch_t	*mpatch;
	texpatch_t	*patch;
	int			i,j;
	int			*maptex, *maptex2, *maptex1;
	char		name[9], *names, *name_p;
	int			*patchlookup;
	int			totalwidth;
	int			nummappatches;
	int			offset, maxoff, maxoff2;
	int			numtextures1, numtextures2;
	int			*directory;

//
// load the patch names from pnames.lmp
//
	name[8] = 0;
	names = W_CacheLumpName ("PNAMES", PU_STATIC);
	nummappatches = LONG ( *((int *)names) );
	name_p = names+4;
	patchlookup = alloca (nummappatches*sizeof(*patchlookup));
	for (i=0 ; i<nummappatches ; i++)
	{
		strncpy (name,name_p+i*8, 8);
		patchlookup[i] = W_CheckNumForName (name);
	}
	Z_Free (names);

//
// load the map texture definitions from textures.lmp
//
	maptex = maptex1 = W_CacheLumpName ("TEXTURE1", PU_STATIC);
	numtextures1 = LONG(*maptex);
	maxoff = W_LumpLength (W_GetNumForName ("TEXTURE1"));
	directory = maptex+1;

	if (W_CheckNumForName ("TEXTURE2") != -1)
	{
		maptex2 = W_CacheLumpName ("TEXTURE2", PU_STATIC);
		numtextures2 = LONG(*maptex2);
		maxoff2 = W_LumpLength (W_GetNumForName ("TEXTURE2"));
	}
	else
	{
		maptex2 = NULL;
		numtextures2 = 0;
		maxoff2 = 0;
	}
	numtextures = numtextures1 + numtextures2;

	//
	//	Init the startup thermometer at this point...
	//
	{
		int	spramount;
		spramount = W_GetNumForName("S_END") - W_GetNumForName("S_START") + 1;
		InitThermo(spramount + numtextures + 6);
	}

	textures = Z_Malloc (numtextures*sizeof(*textures), PU_STATIC, 0);
	texturecolumnlump = Z_Malloc (numtextures*sizeof(*texturecolumnlump), PU_STATIC, 0);
	texturecolumnofs = Z_Malloc (numtextures*sizeof(*texturecolumnofs), PU_STATIC, 0);
	texturecomposite = Z_Malloc (numtextures*sizeof(*texturecomposite), PU_STATIC, 0);
	texturecompositesize = Z_Malloc (numtextures*sizeof(*texturecompositesize), PU_STATIC, 0);
	texturewidthmask = Z_Malloc (numtextures*sizeof(*texturewidthmask), PU_STATIC, 0);
	textureheight = Z_Malloc (numtextures*sizeof(*textureheight), PU_STATIC, 0);

	totalwidth = 0;

	for (i=0 ; i<numtextures ; i++, directory++)
	{
		#ifdef __NEXT__
		if(!(i&63))
			printf (".");
		#else
		IncThermo();
		#endif
		if (i == numtextures1)
		{	// start looking in second texture file
			maptex = maptex2;
			maxoff = maxoff2;
			directory = maptex+1;
		}

		offset = LONG(*directory);
		if (offset > maxoff)
			I_Error ("R_InitTextures: bad texture directory");
		mtexture = (maptexture_t *) ( (byte *)maptex + offset);
		texture = textures[i] = Z_Malloc (sizeof(texture_t) 
			+ sizeof(texpatch_t)*(SHORT(mtexture->patchcount)-1), PU_STATIC,
			0);
		texture->width = SHORT(mtexture->width);
		texture->height = SHORT(mtexture->height);
		texture->patchcount = SHORT(mtexture->patchcount);
		memcpy (texture->name, mtexture->name, sizeof(texture->name));
		mpatch = &mtexture->patches[0];
		patch = &texture->patches[0];
		for (j=0 ; j<texture->patchcount ; j++, mpatch++, patch++)
		{
			patch->originx = SHORT(mpatch->originx);
			patch->originy = SHORT(mpatch->originy);
			patch->patch = patchlookup[SHORT(mpatch->patch)];
			if (patch->patch == -1)
				I_Error (
				"R_InitTextures: Missing patch in texture %s",texture->name);
		}		
		texturecolumnlump[i] = Z_Malloc (texture->width*2, PU_STATIC,0);
		texturecolumnofs[i] = Z_Malloc (texture->width*sizeof(**texturecolumnofs), PU_STATIC,0);
		j = 1;
		while (j*2 <= texture->width)
			j<<=1;
		texturewidthmask[i] = j-1;
		textureheight[i] = texture->height<<FRACBITS;
		
		totalwidth += texture->width;
	}

	Z_Free (maptex1);
	if (maptex2)
		Z_Free (maptex2);

//
// precalculate whatever possible
//		
	for(i = 0; i < numtextures; i++)
	{
		R_GenerateLookup(i);
		CheckAbortStartup();
	}

//
// translation table for global animation
//
	texturetranslation = Z_Malloc ((numtextures+1)*sizeof(*texturetranslation), PU_STATIC, 0);
	for (i=0 ; i<numtextures ; i++)
		texturetranslation[i] = i;
}


/*
================
=
= R_InitFlats
=
=================
*/

void R_InitFlats (void)
{
	int		i;
	
	firstflat = W_GetNumForName ("F_START") + 1;
	lastflat = W_GetNumForName ("F_END") - 1;
	numflats = lastflat - firstflat + 1;
	
// translation table for global animation
	flattranslation = Z_Malloc ((numflats+1)*sizeof(*flattranslation), PU_STATIC, 0);
	for (i=0 ; i<numflats ; i++)
		flattranslation[i] = i;
}


/*
================
=
= R_InitSpriteLumps
=
= Finds the width and hoffset of all sprites in the wad, so the sprite doesn't
= need to be cached just for the header during rendering
=================
*/

void R_InitSpriteLumps (void)
{
	int		i;
	patch_t	*patch;

	firstspritelump = W_GetNumForName ("S_START") + 1;
	lastspritelump = W_GetNumForName ("S_END") - 1;
	numspritelumps = lastspritelump - firstspritelump + 1;
	spritewidth = Z_Malloc (numspritelumps*sizeof(*spritewidth), PU_STATIC, 0);
	spriteoffset = Z_Malloc (numspritelumps*sizeof(*spriteoffset), PU_STATIC, 0);
	spritetopoffset = Z_Malloc (numspritelumps*sizeof(*spritetopoffset), PU_STATIC, 0);

	for (i=0 ; i< numspritelumps ; i++)
	{
		#ifdef __NEXT__
		if (!(i&63))
			printf (".");
		#else
		IncThermo();
		#endif
		patch = W_CacheLumpNum (firstspritelump+i, PU_CACHE);
		spritewidth[i] = SHORT(patch->width)<<FRACBITS;
		spriteoffset[i] = SHORT(patch->leftoffset)<<FRACBITS;
		spritetopoffset[i] = SHORT(patch->topoffset)<<FRACBITS;
	}
}


/*
================
=
= R_InitColormaps
=
=================
*/

void R_InitColormaps (void)
{
	int	lump, length;
//
// load in the light tables
// 256 byte align tables
//
	lump = W_GetNumForName("COLORMAP");
	length = W_LumpLength (lump) + 255;
	colormaps = Z_Malloc (length, PU_STATIC, 0);
	colormaps = (byte *)( ((uintptr_t)colormaps + 255) & ~(uintptr_t)255);
	W_ReadLump (lump,colormaps);
}


/*
================
=
= R_InitData
=
= Locates all the lumps that will be used by all views
= Must be called after W_Init
=================
*/

void R_InitData (void)
{
	tprintf("\nR_InitTextures ",0);
	R_InitTextures ();
//printf (".");
	tprintf("R_InitFlats\n",0);
	R_InitFlats ();
	IncThermo();
//printf (".");
	tprintf("R_InitSpriteLumps ",0);
	R_InitSpriteLumps ();
	IncThermo();
//printf (".");
	R_InitColormaps ();
}


//=============================================================================

/*
================
=
= R_FlatNumForName
=
================
*/

// DERP: a flat by name within F_START..F_END only, so a same-named patch or
// sound elsewhere in the WADs is never taken for it.
int	R_CheckFlatNumForName (char *name)
{
	int		i = W_CheckFlatLump (name, firstflat, lastflat+1);

	return i == -1 ? -1 : i - firstflat;
}

int	R_FlatNumForName (char *name)
{
	int		i;
	char	namet[9];

	i = R_CheckFlatNumForName (name);
	if (i == -1)
	{
		namet[8] = 0;
		memcpy (namet, name,8);
		I_Error ("R_FlatNumForName: %s not found",namet);
	}
	return i;
}

// DERP: texture access for the accelerated renderer and the sound code.
void R_TextureDimensions(int texture, int *width, int *height)
{
	if (texture < 0 || texture >= numtextures) I_Error("Invalid GPU texture");
	*width = textures[texture]->width;
	*height = textures[texture]->height;
}

void R_TextureName(int texture, char name[9])
{
	if (texture < 0 || texture >= numtextures) I_Error("Invalid GPU texture");
	memcpy(name, textures[texture]->name, 8);
	name[8] = 0;
}

void R_CopyIndexedTexture(int texture, byte *pixels)
{
	texture_t *tex = textures[texture];
	memset(pixels, 0, (size_t)tex->width * tex->height * 2);
	for (int n = 0; n < tex->patchcount; ++n)
	{
		texpatch_t *part = &tex->patches[n];
		int size = W_LumpLength(part->patch);
		patch_t *patch = W_CacheLumpNum(part->patch, PU_CACHE);
		int width = SHORT(patch->width);
		if (size < 8 || width < 1 || 8 + (size_t)width * 4 > (size_t)size)
			I_Error("Invalid texture patch header");
		for (int x = 0; x < width; ++x)
		{
			int destx = x + part->originx;
			if (destx < 0 || destx >= tex->width) continue;
			size_t offset = (uint32_t)LONG(patch->columnofs[x]);
			int previous = -1;
			while (offset < (size_t)size && ((byte*)patch)[offset] != 255)
			{
				byte *post = (byte*)patch + offset;
				if (offset + 4 > (size_t)size || offset + 4 + post[1] > (size_t)size)
					I_Error("Invalid texture patch column");
				int top = post[0];
				if (top <= previous) top += previous;
				previous = top;
				for (int y = 0; y < post[1]; ++y)
				{
					int desty = top + y + part->originy;
					if (desty < 0 || desty >= tex->height) continue;
					size_t pixel = ((size_t)desty * tex->width + destx) * 2;
					pixels[pixel] = post[3 + y];
					pixels[pixel + 1] = 255;
				}
				offset += post[1] + 4;
			}
		}
	}
}


/*
================
=
= R_CheckTextureNumForName
=
================
*/

int	R_CheckTextureNumForName (char *name)
{
	int		i;
	
	if (name[0] == '-')		// no texture marker
		return 0;
		
	for (i=0 ; i<numtextures ; i++)
		if (!strncasecmp (textures[i]->name, name, 8) )
			return i;
		
	return -1;
}


/*
================
=
= R_TextureNumForName
=
================
*/

int	R_TextureNumForName (char *name)
{
	int		i;
	//char	namet[9];
	
	i = R_CheckTextureNumForName (name);
	if (i==-1)
		I_Error ("R_TextureNumForName: %s not found",name);
	
	return i;
}


/*
=================
=
= R_PrecacheLevel
=
= Preloads all relevent graphics for the level
=================
*/

int		flatmemory, texturememory, spritememory;

void R_PrecacheLevel (void)
{
	char			*flatpresent;
	char			*texturepresent;
	char			*spritepresent;
	int				i,j,k, lump;
	texture_t		*texture;
	thinker_t		*th;
	spriteframe_t	*sf;

	if (demoplayback)
		return;
			
//
// precache flats
//	
	flatpresent = alloca(numflats);
	memset (flatpresent,0,numflats);	
	for (i=0 ; i<numsectors ; i++)
	{
		flatpresent[sectors[i].floorpic] = 1;
		flatpresent[sectors[i].ceilingpic] = 1;
	}
	
	flatmemory = 0;
	for (i=0 ; i<numflats ; i++)
		if (flatpresent[i])
		{
			lump = firstflat + i;
			flatmemory += lumpinfo[lump].size;
			W_CacheLumpNum(lump, PU_CACHE);
		}
		
//
// precache textures
//
	texturepresent = alloca(numtextures);
	memset (texturepresent,0, numtextures);
	
	for (i=0 ; i<numsides ; i++)
	{
		texturepresent[sides[i].toptexture] = 1;
		texturepresent[sides[i].midtexture] = 1;
		texturepresent[sides[i].bottomtexture] = 1;
	}
	
	texturepresent[skytexture] = 1;
	
	texturememory = 0;
	for (i=0 ; i<numtextures ; i++)
	{
		if (!texturepresent[i])
			continue;
		texture = textures[i];
		for (j=0 ; j<texture->patchcount ; j++)
		{
			lump = texture->patches[j].patch;
			texturememory += lumpinfo[lump].size;
			W_CacheLumpNum(lump , PU_CACHE);
		}
	}
	
//
// precache sprites
//
	spritepresent = alloca(numsprites);
	memset (spritepresent,0, numsprites);
	
	for (th = thinkercap.next ; th != &thinkercap ; th=th->next)
	{
		if (th->function == P_MobjThinker)
			spritepresent[((mobj_t *)th)->sprite] = 1;
	}
	
	spritememory = 0;
	for (i=0 ; i<numsprites ; i++)
	{
		if (!spritepresent[i])
			continue;
		for (j=0 ; j<sprites[i].numframes ; j++)
		{
			sf = &sprites[i].spriteframes[j];
			for (k=0 ; k<8 ; k++)
			{
				lump = firstspritelump + sf->lump[k];
				spritememory += lumpinfo[lump].size;
				W_CacheLumpNum(lump , PU_CACHE);
			}
		}
	}
}




