#ifndef DOOM_FLUID_WEAPONS_H
#define DOOM_FLUID_WEAPONS_H
#include "weapon_transform.h"
#include <string>
#include <vector>
#include <cstdint>
namespace doomfluid {
struct Layer {
    std::string sprite;
    doomweapon::Transform transform;
    bool bright=false;
    float alpha=1;
    bool additive=false;
    bool flipX=false;
    bool mirror=false;
    bool addWeapon=true;
};
// Native port of MR's overlay animation arithmetic. This deliberately lives
// outside the Doom simulation: its random choices cannot change demo RNG.
class Weapons {
    int weapon=-1,age=-1;
    uint32_t randomState=0x73ad46b1;
    float power=.5f,rotation=0,misalignment=0,recoveryX=0,recoveryY=0,pump=0;
    doomweapon::Transform pose;
    doomweapon::Transform rocketFlash;
    doomweapon::Motion motion;
    char frame='A';
    float muzzle=0,oldMuzzle=0;
    int plasmaFrame=0;
    float spin=30,oldSpin=30,spinSpeed=0,gunFrame=0;
    int sawHold=0;
    bool charging=false;
    bool altHeld=false,reloadHeld=false,oldAlt=false,oldReload=false,auxReady=false;
    bool secondGrip=false,secondFist=false,bfgIdle=false,manualReload=false;
    bool alternatePunch=true;
    int auxAction=0,auxAge=-1,gripAge=-1;
    int inspectionHold=-1;
    doomweapon::Transform auxHand;
    doomweapon::Motion auxHandMotion;
    doomweapon::Transform bumpHand;
    doomweapon::Motion bumpMotion;
    int fistEnd=-1;
    bool fistAltEnd=false;
    doomweapon::Transform catchBullet;
    doomweapon::Motion catchBulletMotion;
    float catchDirection=0;
    struct Shell { int life=0,kind=0; float direction=0,zMult=0,speed=1; doomweapon::Transform pose; doomweapon::Motion motion; };
    std::vector<Shell> shells;
    void ejectShell();
    void ejectCasing(int kind,int barrel=0);
    void tickShells();
    int shotgunCatchAge=-1,shotgunReturnWait=0;
    int ssgCatchAge=-1;
    void superShotgunCatch();
    doomweapon::Transform shotgunCatchHand;
    doomweapon::Motion shotgunCatchMotion;
    void tickShotgunCatch();
    bool movementActive=false,walking=false,airborne=false;
    float yawInput=0,viewPitch=0,moveBob=0,verticalSpeed=0;
    float swayX=0,swayY=0,swayVX=0,swayVY=0,returnSpeed=0,bounce=0,bounceSpeed=0;
    float displayedSwayX=0,sideInput=0;
    int moveTime=0,bobState=0,idleDelay=0,idleStep=-1,idleWait=0,idleVariant=0;
    doomweapon::Transform movementPose,idlePose;
    doomweapon::Motion movementMotion,idleMotion;
    void tickMovement();
    doomweapon::Transform hand;
    doomweapon::Motion handMotion;
    std::vector<std::string> soundEvents;
    float random(float low,float high);
    void pistol();
    void shotgun();
    void rocket();
    void plasma();
    void bfg();
    void chaingun();
    void superShotgun();
    void fist();
    void chainsaw();
    void auxiliary();
    std::vector<Layer> baseLayers(float fraction,bool bareHands) const;
public:
    void movement(float yaw,float pitch,float bob,float vz,bool inAir,bool moving,int time){movementActive=true;yawInput=yaw;viewPitch=pitch;moveBob=bob;verticalSpeed=vz;airborne=inAir;walking=moving;moveTime=time;}
    void punchDirection(float yaw,float side){yawInput=yaw;sideInput=side;}
    void input(bool alt,bool reload,bool ready=true){altHeld=alt;reloadHeld=reload;auxReady=ready;}
    const char* ambientSound()const{return weapon==3&&std::abs(spinSpeed)>.1f?"ChaingunSpin":weapon==6&&bfgIdle?"BFGCharge":"";}
    float ambientLevel()const{return weapon==3?std::clamp(std::abs(spinSpeed)/30.f,0.f,1.f):1.f;}
    void reset(int weapon);
    void adoptSwitchPose(const std::vector<Layer>& layers);
    void shot(int weapon,float shotPower=.5f);
    void attack(int weapon);
    void tick(int weapon);
    std::vector<Layer> layers(float fraction, bool bareHands=true) const;
    std::vector<std::string> takeSounds();
    bool supported(int weapon) const;
};
}
#endif
