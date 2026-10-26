/* Conservative defaults; external masks can identify arbitrary custom artwork. */
#ifndef DOOM_EMISSIVE_H
#define DOOM_EMISSIVE_H
#include <string.h>
static inline int doom_switch_texture(const char *name) {
    return name[0]=='S' && name[1]=='W' && (name[2]=='1'||name[2]=='2');
}
/* Only the brighter, saturated indicator in this state emits. Comparing the
   companion state excludes colored housing shared by both switch textures. */
static inline unsigned char doom_switch_pixel(const unsigned char *rgb,
                                               const unsigned char *other) {
    int hi=rgb[0]>rgb[1]?rgb[0]:rgb[1];
    int oldhi=other[0]>other[1]?other[0]:other[1];
    if(hi<96 || hi<oldhi+24 || hi<rgb[2]+40) return 0;
    if(!(rgb[0]>rgb[1]*2) && !(rgb[1]>rgb[0]*1.4f)) return 0;
    return (unsigned char)(hi>=160?255:(hi-80)*255/80);
}

enum doom_emissive_kind { DOOM_EMISSIVE_NONE, DOOM_EMISSIVE_LAMP,
    DOOM_EMISSIVE_PANEL, DOOM_EMISSIVE_LAVA, DOOM_EMISSIVE_SLIME, DOOM_EMISSIVE_MAGMA,
    DOOM_EMISSIVE_GLASS /* backlit stained glass: every coloured pane, softly */ };
/* Lists end with NULL; a trailing '*' marks a prefix, otherwise the name must match exactly. */
static inline int doom_emissive_match(const char *name, const char *const *list)
{
    for (; *list; ++list) {
        size_t n=strlen(*list);
        if ((*list)[n-1]=='*' ? !strncmp(name,*list,n-1) : !strcmp(name,*list)) return 1;
    }
    return 0;
}
/* Names are uppercase, NUL-terminated WAD names (at most eight characters).
   Covers Doom, Doom II, Final Doom, SIGIL, Legacy of Rust and Freedoom artwork. */
static inline int doom_emissive_kind(const char *name, int flat)
{
    static const char *const lava[]={"LAVA*","BLAVA*",0};
    static const char *const fireWalls[]={"FIREBLU*","FIRELAV*","FIREMAG*","FIREWAL*","FIREGLO*",
        "FLMWAL*","DBRAIN*","REDWALL*","BFALL*",0};
    static const char *const magmaFlats[]={"RROCK0*","MFLR9_5","MFLR9_6","SLIME09","SLIME10",
        "SLIME11","SLIME12",0};
    static const char *const magmaWalls[]={"CRACKLE*","ROCKRED*","ROCKLAV*",0};
    static const char *const slimeWalls[]={"SFALL*","SLADRIP*","SLADFAL*",0};
    static const char *const lampFlats[]={"TLITE*","CEIL1_2*","CEIL1_3*","GRNLITE*","FLAT22*",
        "MLITE*","KCF_LIT*","FLOOR8_*","FLAT2","FLAT17","FLAT23","FLOOR1_7","TCLTA",0};
    static const char *const lampWalls[]={"TLITE*","LITE*","LIGHT*","AQLITE*","TEKLITE*",
        "METLITE*","SHAWLIT*","WOODLIT*","STONLIT*","BRICKLIT*","DOORBLU*","DOORRED*",
        "DOORYEL*","KCMRK*","TCMRK*","KSW_LOCK","KSW_OPEN","KSW_COM*","BSTONE3","SILVER2",
        "TCGRDB",0};
    static const char *const panelFlats[]={"CONS1_*",0};
    static const char *const panelWalls[]={"COMP*","AQCOMP*","TEKWALL*","PLANET*","SILVER3",
        "EXITSIGN","EIXTSIGN","EXITSTON","PNK4EXIT","DOOR1","DOOR3","BIGDOOR1","BIGDOOR2","BIGDOOR4",0};
    /* Legacy of Rust ships unlit variants such as COMP1OFF and LITE4OFF. */
    if (strstr(name,"OFF")) return DOOM_EMISSIVE_NONE;
    if (doom_emissive_match(name,lava) || (!flat && doom_emissive_match(name,fireWalls)))
        return DOOM_EMISSIVE_LAVA;
    if (doom_emissive_match(name,flat?magmaFlats:magmaWalls)) return DOOM_EMISSIVE_MAGMA;
    if (!strncmp(name,"NUK",3) || (!flat && doom_emissive_match(name,slimeWalls)))
        return DOOM_EMISSIVE_SLIME;
    if (doom_emissive_match(name,flat?lampFlats:lampWalls)) return DOOM_EMISSIVE_LAMP;
    if (doom_emissive_match(name,flat?panelFlats:panelWalls)) return DOOM_EMISSIVE_PANEL;
    return DOOM_EMISSIVE_NONE;
}
/* Machinery that throws electric sparks when shot, glowing or not: computer
   panels, tech walls and their light fixtures (shot_lights.cpp). */
static inline int doom_spark_texture(const char *name, int flat)
{
    static const char *const walls[]={"COMP*","AQCOMP*","TEKWALL*","TEKLITE*","TEKGREN*","PLANET*",
        "SILVER*","SPACEW*","METLITE*","LITE*","TLITE*",0};
    static const char *const flats[]={"CONS1_*","TLITE*",0};
    if (strstr(name,"OFF")) return 0;
    return doom_emissive_match(name,flat?flats:walls);
}
/* Keep dark outlines and housing shaded; hue tests avoid lighting pale metal. */
static inline unsigned char doom_emissive_pixel(int kind, const unsigned char *rgb,
                                               unsigned char coverage)
{
    if (!coverage || kind == DOOM_EMISSIVE_NONE) return 0;
    int r=rgb[0],g=rgb[1],b=rgb[2];
    int hi=r>g?r:g; if (b>hi) hi=b;
    int lo=r<g?r:g; if (b<lo) lo=b;
    int threshold=160;
    if (kind==DOOM_EMISSIVE_LAVA) {
        if (r<g+24 || r<b+40) return 0;
        threshold=80;
    } else if (kind==DOOM_EMISSIVE_MAGMA) {
        /* Red-to-yellow cracks glow; brown and tan rock around them stays shaded. */
        if (!(r>=100 && r>=g+60 && b*2<r) && !(r>=200 && g>=160 && b*2<g)) return 0;
        threshold=96;
    } else if (kind==DOOM_EMISSIVE_SLIME) {
        if (g<r+24 || g<b+24) return 0;
        threshold=64;
    } else if (kind==DOOM_EMISSIVE_GLASS) {
        /* Coloured panes glow in their own colour, dim ones less; the dark
           lead between them and the grey stone frame stay shaded. */
        if (hi-lo<32) return 0;
        int value=(hi-40)*200/160;
        return (unsigned char)(value<0?0:value>200?200:value);
    } else if (kind==DOOM_EMISSIVE_PANEL) {
        if (hi-lo<40) return 0;
        if (!(g>r*1.4 && g>b*1.2) && !(b>r*1.4 && b>g*1.2) &&
            !(r>g*2 && r>b*2)) return 0;
        threshold=64;
    } else {
        /* Bright neutral strips and saturated colored bulbs. */
        if (hi-lo<48 && lo<170) return 0;
        /* Doom's red key-door bulbs use a darker palette ramp than blue/yellow. */
        if(r>g*2&&r>b*2)threshold=64;
    }
    if (hi<=threshold) return 0;
    int value=(hi-threshold)*255/48;
    return (unsigned char)(value>255?255:value);
}
#endif
