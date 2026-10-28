namespace doom3d {
GpuTextureRef keyHandTextures[6];
float keyHandAspect[6]={},keyHandVisible=0,keyWeaponDrop=0;
int keyHandIndex=-1;
static void keyHandNotice(){
 static unsigned serial=0,level=~0u;static int viewer=-1,start=-1000,last=-1;
 keyHandVisible=keyWeaponDrop=0;
 if(level!=r_levelserial||viewer!=displayplayer||leveltime<last||gamestate!=GS_LEVEL){
  level=r_levelserial;viewer=displayplayer;serial=p_keypickup_serial[viewer];start=-1000;
 }
 last=leveltime;
 if(serial!=p_keypickup_serial[viewer]){
  serial=p_keypickup_serial[viewer];keyHandIndex=p_keypickup_card[viewer];
  if(settings.keyPickupHands&&keyHandIndex>=0&&keyHandIndex<6)start=leveltime;
 }
 if(!settings.keyPickupHands||gamestate!=GS_LEVEL||players[viewer].health<=0||(players[viewer].cmd.buttons&BT_ATTACK)){start=-1000;return;}
 float t=(leveltime-start+renderFraction)/35.f;
 if(t<0||t>=.8f||keyHandIndex<0||keyHandIndex>=6)return;
 auto smooth=[](float v){v=std::clamp(v,0.f,1.f);return v*v*(3-2*v);};
 keyWeaponDrop=smooth(t/.09f)*(1-smooth((t-.61f)/.19f));
 keyHandVisible=smooth((t-.07f)/.11f)*(1-smooth((t-.48f)/.13f));
}
}
