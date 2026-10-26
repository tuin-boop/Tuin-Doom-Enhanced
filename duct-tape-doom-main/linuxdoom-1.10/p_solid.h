#ifndef P_SOLID_H
#define P_SOLID_H
#ifdef __cplusplus
extern "C" {
#endif
int SV_Active(void);
int SV_MapMode(void);
int SV_Edited(float x,float y,float z);
extern int sv_chain_collapses,sv_hanging_fragments,sv_collapse_size;
int SV_MapPosition(mobj_t *thing,fixed_t x,fixed_t y);
void SV_SectorChanged(sector_t *sector);
int SV_Slide(mobj_t *thing);
int SV_Position(mobj_t *thing,fixed_t x,fixed_t y,fixed_t *floor,fixed_t *ceiling);
void SV_Refresh(mobj_t *thing);
void SV_Tick(void);
void SV_Use(player_t *player);
void SV_Player(player_t *player);
void SV_Missile(mobj_t *thing);
int SV_Explode(mobj_t *thing);
int SV_ActorGroundImpact(mobj_t *missile,mobj_t *victim);
void SV_MonsterContact(mobj_t *missile,mobj_t *victim);
float SV_Trace(float x,float y,float z,float dx,float dy,float dz,float *point,int *owner);
void SV_Impact(float *point,int owner,float radius);
void SV_Blast(mobj_t *thing,int radius);
int SV_Sight(mobj_t *a,mobj_t *b);
int SV_SaveSize(void);
int SV_Save(unsigned char *data);
void SV_Load(const unsigned char *data,int length);
#ifdef __cplusplus
}
#endif
#endif
