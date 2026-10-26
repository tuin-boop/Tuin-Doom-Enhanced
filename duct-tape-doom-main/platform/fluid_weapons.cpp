#include "fluid_weapons.h"
namespace doomfluid {
void Weapons::adoptSwitchPose(const std::vector<Layer>& layers){
    if(weapon!=3)return;
    const Layer *body=nullptr,*tip=nullptr;
    for(const auto&l:layers){
        if(l.sprite.compare(0,4,"CH1G")==0)body=&l;
        if(!tip&&l.sprite.compare(0,4,"CH1M")==0)tip=&l;
    }
    if(body&&tip){
        spin=oldSpin=std::atan2((tip->transform.y-body->transform.y-20)/20,
                              (tip->transform.x-body->transform.x+2)/30)*57.2957795f;
        gunFrame=body->sprite[4]-'A';
        if(age<0)frame=body->sprite[4];
    }
}
float Weapons::random(float a,float b){randomState=randomState*1664525u+1013904223u;return a+(b-a)*(randomState>>8)/16777215.f;}
bool Weapons::supported(int w)const{return w>=0&&w<=8;}
void Weapons::reset(int w){rocketFlash={};displayedSwayX=0;alternatePunch=true;ssgCatchAge=-1;shotgunCatchAge=-1;shotgunReturnWait=0;shotgunCatchMotion.reset();bumpHand={};bumpMotion.reset();fistEnd=-1;shells.clear();movementPose={};idlePose={};movementMotion.reset();idleMotion.reset();swayX=swayY=swayVX=swayVY=returnSpeed=bounce=bounceSpeed=0;bobState=0;idleDelay=0;idleStep=-1;idleWait=0;weapon=w;age=-1;pose={};pose.pivotX=pose.pivotY=.5f;motion.reset();motion.set(pose,false);frame=w==7?'C':'A';muzzle=oldMuzzle=0;plasmaFrame=0;spin=oldSpin=30;spinSpeed=gunFrame=0;soundEvents.clear();sawHold=0;charging=false;hand={};handMotion.reset();auxAction=0;auxAge=gripAge=-1;auxHand={};auxHandMotion.reset();altHeld=reloadHeld=oldAlt=oldReload=bfgIdle=manualReload=false;}
void Weapons::attack(int w){if(w==6||w==7){auxAction=0;auxAge=-1;bfgIdle=false;pose={};}if(w==0){shot(w);return;}if(w==7){if(weapon!=w)reset(w);sawHold=10;if(age<0){age=0;chainsaw();motion.set(pose);}return;}if(w==6){if(weapon!=w)reset(w);age=0;charging=true;power=0;pose.pivotX=.5f;pose.pivotY=0;bfg();motion.set(pose);}}
void Weapons::shot(int w,float strength){if(weapon!=w)reset(w);auxAction=0;auxAge=-1;manualReload=false;ssgCatchAge=-1;bfgIdle=false;if(w==6){charging=false;age=25;bfg();motion.set(pose);return;}age=0;power=std::clamp(strength,0.f,1.f);if(w!=3)pose={};frame='A';if(w==1){pose.pivotX=random(secondGrip?.35f:0,.5f);pose.pivotY=random(0,secondGrip?.3f:.4f);rotation=(pose.pivotX-.5f)*power;pistol();}else if(w==2){pose.pivotX=.5f;pose.pivotY=.5f;shotgun();}else if(w==3)chaingun();else if(w==4)rocket();else if(w==5){muzzle=-1.5f;plasma();}else if(w==8)superShotgun();else if(w==0)fist();else if(w==7){sawHold=10;chainsaw();}motion.set(pose);}
void Weapons::tick(int w){if(weapon!=w)reset(w);motion.beforeTick();handMotion.beforeTick();auxHandMotion.beforeTick();bumpMotion.beforeTick();catchBulletMotion.beforeTick();tickShells();if(sawHold>0)--sawHold;oldMuzzle=muzzle;oldSpin=spin;if(age>=0){++age;if(w==1)pistol();else if(w==2)shotgun();else if(w==3)chaingun();else if(w==4)rocket();else if(w==5)plasma();else if(w==6)bfg();else if(w==8)superShotgun();else if(w==0)fist();else if(w==7)chainsaw();}else if(w==7)chainsaw();if(w==3){if(auxReady&&altHeld&&std::abs(spinSpeed)<10.f)--spinSpeed;if((age<0||age>4)&&!(auxReady&&altHeld))spinSpeed*=.85f;if(std::abs(spinSpeed)<.001f)spinSpeed=0;spin-=spinSpeed;gunFrame=std::abs(std::fmod(gunFrame+spinSpeed*.03f,4.f));frame='A'+int(gunFrame);}auxiliary();tickShotgunCatch();if(movementActive)tickMovement();motion.set(pose,!(w==5&&(age==4||age==7||age==17||age==22)));}
void Weapons::tickMovement(){
    movementMotion.beforeTick();idleMotion.beforeTick();
    static const float mults[]={-.6f,-.5f,.8f,.4f,.5f,.8f,.1f,.6f,1.f};
    static const float rotations[]={.3f,.2f,-.1f,0,-.1f,0,0,-.15f,-.11f};
    float mult=mults[weapon];
    const float floatSpeed=weapon==3?1.f:weapon==8?3.f:weapon==7?.6f:weapon==5?(age>=3&&age<=20?1.f:2.f):weapon==6?2.f:weapon==1&&auxAction==2?1.f:weapon==2&&age>=9?1.5f:4.f;
    const float capMult=floatSpeed/4;
    float target=std::sin(-viewPitch)*-20;
    swayVY=std::clamp(swayVY+(target-swayY)*.1f,-5.f,5.f);
    if((swayVY<0&&swayY<target)||(swayVY>0&&swayY>target))swayVY*=.2f*std::abs(mult);
    swayY+=swayVY;
    target=yawInput*.01f*mult;float add=target-swayX;
    if(target==0){returnSpeed=std::min(1.f,returnSpeed+.075f*std::abs(mult*.5f));add*=returnSpeed*.25f;}
    else {add*=.1f;returnSpeed=0;}
    swayVX+=add;
    if((swayVX<0&&swayX+swayVX<target)||(swayVX>0&&swayX+swayVX>target))swayVX*=.5f;
    swayX+=swayVX;
    if(airborne){bounceSpeed=verticalSpeed;bounce=std::clamp((bounce+(bounceSpeed-bounce)*.5f)*capMult,-floatSpeed*6.5f,20.f);}
    else if(std::abs(bounceSpeed)>0){if(bounce<-bounceSpeed)bounce-=bounceSpeed*.5f;else{bounceSpeed*=.7f;if(std::abs(bounceSpeed)<.05f)bounceSpeed=0;bounce=-bounceSpeed;}}
    const float angle=moveTime*128.f*6.28318530718f/8192.f;
    // The source eases the previous displayed position toward CurSway;
    // halving CurSway every tic permanently halved the turning motion.
    displayedSwayX+=(swayX-displayedSwayX)*.5f;
    movementPose.x=displayedSwayX;
    movementPose.y=std::max(-floatSpeed*6.5f,swayY*capMult)+bounce*.5f;
    // User's Fluid preset: inverse-smooth bob, X/Y .4, Z .2.
    movementPose.x+=moveBob*.4f*std::cos(angle);
    movementPose.y+=moveBob*.2f*(1+std::cos(angle*2));
    float z=moveBob*.001f*(.5f+std::cos(angle*2));
    movementPose.scaleX=movementPose.scaleY=1+z;
    bool idle=auxReady&&age<0&&auxAction==0;
    movementPose.rotation=idle?-movementPose.x*rotations[weapon]:0;
    if(idle&&weapon!=7){
        if(bobState>=0){idlePose.x+=random(-.005f,.015f);idlePose.y+=random(.01f,.02f);if(bobState>90||idlePose.y>1.35f)bobState=-bobState;else ++bobState;}
        else {idlePose.x-=random(-.005f,.015f);idlePose.y-=random(.01f,.02f);if(idlePose.y<0){idlePose={};bobState=0;}else if(!(bobState>=-1&&idlePose.y>0))++bobState;}
        if(walking||std::abs(swayX)>.0001f)idleDelay=std::min(0,idleDelay);else ++idleDelay;
        bool gestures=weapon==0||weapon==1||weapon==2||weapon==4||weapon==8;
        if(gestures&&idleStep<0&&idleDelay>35&&std::abs(bobState)<2){idleStep=0;idleWait=0;idleVariant=random(0,1)<.4f;idleDelay=-int(random(3,10.99f))*70;idlePose={};bobState=0;}
        // GunIdle1/GunIdle2: source offset/rotation steps, each held 1–2 tics.
        if(idleStep>=0&&--idleWait<=0){
            struct Step {int count;float x,y,r;};
            static const Step first[]={{4,-.15f,.05f,0},{2,-.1f,.05f,-.1f},{3,.15f,-.1f,0},{2,.15f,-.025f,0},{1,.1f,0,0},{2,.1f,.025f,.05f},{3,.025f,.05f,.05f},{2,-.05f,-.1f,-.05f},{1,0,.05f,.05f}};
            static const Step second[]={{1,0,.05f,-.05f},{2,.05f,-.1f,.05f},{3,-.025f,.05f,-.05f},{2,-.1f,.025f,-.05f},{1,-.1f,0,0},{2,-.15f,-.025f,0},{3,-.15f,-.1f,0},{2,.1f,.05f,.1f},{4,.15f,.05f,0}};
            int at=idleStep++;bool applied=false;
            for(auto&s:idleVariant?second:first){if(at<s.count){idlePose.x+=s.x;idlePose.y+=s.y;idlePose.rotation+=s.r;applied=true;break;}at-=s.count;}
            if(!applied){idleStep=-1;idlePose={};}idleWait=int(random(1,2.99f));
        }
    }else {idlePose={};bobState=0;idleStep=-1;idleDelay=std::min(0,idleDelay);}
    movementMotion.set(movementPose);idleMotion.set(idlePose);
}
void Weapons::ejectShell(){
    if(shells.size()>=2)shells.erase(shells.begin());
    Shell shell;shell.life=20;shell.direction=random(-.5f,1);
    shell.zMult=std::clamp(pose.rotation/16,-1.f,1.f);
    shell.pose.x=105;shell.pose.y=232;shell.pose.rotation=160;
    shell.pose.pivotX=shell.pose.pivotY=.5f;
    shell.motion.set(shell.pose,false);shells.push_back(shell);
}
void Weapons::tickShells(){
    for(auto&shell:shells){
        shell.motion.beforeTick();
        if(shell.life>=2&&shell.life<20){
            float z=((shell.life-(shell.kind==1?17:shell.kind?16:15))+shell.zMult)/20.f;
            if(z==0)z=-.01f;
            float velocity=random(1,2*shell.speed);
            shell.pose.y-=z*(shell.kind==3?80:100)*velocity*shell.pose.scaleX;
            shell.pose.x+=(shell.kind==3?-21:shell.kind?16:20)*velocity*shell.pose.scaleX;
            shell.pose.scaleX*=1+.05f*shell.direction*shell.speed;
            shell.pose.scaleY=shell.pose.scaleX;shell.pose.rotation+=shell.kind==3?40:-30*velocity;
            shell.motion.set(shell.pose);
        }
        --shell.life;
    }
    shells.erase(std::remove_if(shells.begin(),shells.end(),[](const Shell&s){return s.life<=0||s.pose.y>312;}),shells.end());
}
void Weapons::ejectCasing(int kind,int barrel){
    if(shells.size()>=10)shells.erase(shells.begin());
    Shell shell;shell.life=20;shell.kind=kind;shell.direction=random(-.7f,1.5f);
    shell.pose.pivotX=shell.pose.pivotY=.5f;shell.pose.rotation=140;
    shell.pose.scaleX=shell.pose.scaleY=.8f;
    shell.pose.x=kind==1?170:195;shell.pose.y=(kind==1?115:190)+32;
    if(kind==3){shell.pose.x=119+barrel*19;shell.pose.y=172-barrel*4;shell.pose.scaleX=shell.pose.scaleY=1.14f+barrel*.1f;shell.pose.rotation=20;shell.direction=random(-1.3f,0);}
    shell.motion.set(shell.pose,false);shells.push_back(shell);
}
void Weapons::tickShotgunCatch(){
    if(shotgunCatchAge<0)return;
    shotgunCatchMotion.beforeTick();
    const int t=shotgunCatchAge++;
    if(t>=2&&t<=5){
        if(shells.empty()){shotgunCatchAge=-1;return;}
        auto& shell=shells.back();const auto old=shell.motion.sample(0);
        const float lead=t==2?4.f:t==3?3.f:1.f,speed=t==2?.5f:t==3?.3f:.1f;
        float x=shell.pose.x+(shell.pose.x-old.x)*lead,y=shell.pose.y-32+(shell.pose.y-old.y);
        float oldX=shotgunCatchHand.x;
        shotgunCatchHand.x=x+(shotgunCatchHand.x-x)*speed;
        shotgunCatchHand.y=y+(shotgunCatchHand.y-y)*speed;
        shotgunCatchHand.scaleX=shotgunCatchHand.scaleY=std::clamp(shell.pose.scaleX,.85f,2.f);
        if(t<5)shotgunCatchHand.rotation-=(shotgunCatchHand.x-oldX)*.5f;else shotgunCatchHand.rotation*=.6f;
        if(t==3)soundEvents.push_back("ShotgunCatch");
        if(t==5)shell.speed=.3f;
    }else if(t==6){shotgunCatchHand.rotation*=.6f;shells.clear();}
    else if(t>=7){
        float speed=t<=9&&frame>'G'?.85f:.7f,oldX=shotgunCatchHand.x;
        shotgunCatchHand.x=140+(shotgunCatchHand.x-140)*speed;
        shotgunCatchHand.y=230+(shotgunCatchHand.y-230)*speed;
        shotgunCatchHand.scaleX=1+(shotgunCatchHand.scaleX-1)*(speed+.1f);
        shotgunCatchHand.scaleY=1+(shotgunCatchHand.scaleY-1)*(speed+.1f);
        shotgunCatchHand.rotation-=(shotgunCatchHand.x-oldX)*.25f;
        if(t>=20)shotgunCatchAge=-1;
    }
    shotgunCatchMotion.set(shotgunCatchHand);
}
void Weapons::pistol(){
    // GunFire with the package's default InstantFire, single right hand.
    static const std::string frames="BBCFJGEDKBHHHHHHHAAAAAAAAAAAAAAA";
    if(age>=int(frames.size())){age=-1;frame='A';pose={};return;}
    frame=frames[age];
    if(age==2)ejectCasing(1);
    if(age<2){float scale=secondGrip?.05f+.01f*power:.07f+.02f*power;pose.scaleX+=scale;pose.scaleY+=scale;pose.x+=secondGrip?0:.4f+1.2f*power;pose.y+=secondGrip?2:3+.75f*power;pose.rotation+=rotation;}
    else if(age==2){pose.scaleX+=secondGrip?.008f:.01f+.0075f*power;pose.scaleY=pose.scaleX;pose.x+=secondGrip?0:.2f+.1f*power;pose.rotation+=rotation;misalignment=(pose.scaleX-1.155f)*.8f;recoveryX=random(-2,2);recoveryY=random(2,8)*(random(0,1)<.5f?-1:1);}
    else if(age<6){float recover=secondGrip?.6f:.5f;pose.scaleX-=std::clamp(pose.scaleX-1+misalignment,0.f,secondGrip?.03f:.04f);pose.scaleY=pose.scaleX;pose.x*=recover;pose.y=-misalignment*2+(pose.y-misalignment*2)*recover;if(secondGrip)pose.rotation*=recover;else pose.rotation-=rotation*random(1.4f,1.5f);}
    else if(age<12){pose.scaleX=1-misalignment+(pose.scaleX-1+misalignment)*.3f;pose.scaleY=pose.scaleX;pose.x=recoveryX+(pose.x-recoveryX)*.9f;pose.y=recoveryY+(pose.y-recoveryY)*.9f;if(secondGrip)pose.rotation*=.3f;else pose.rotation+=rotation*random(.8f,1);}
    else {pose.scaleX=1+(pose.scaleX-1)*.8f;pose.scaleY=1+(pose.scaleY-1)*.8f;pose.x*=.7f;pose.y*=.7f;pose.rotation*=.7f;}
}
void Weapons::shotgun(){
    // GunFire -> Pump -> Pumping, matching the source's one-tic frames.
    if(manualReload&&age>=24){
        frame='F';
        if(age<=26){float retain=random(.85f,.95f);pose.x=-71+(pose.x+71)*retain;pose.y=-42+(pose.y+42)*retain;pose.scaleY+=.01f;}
        else if(age==27&&shotgunReturnWait<24){
            pose.scaleX=pose.scaleY=1;float retain=random(.65f,.8f);pose.x=-71+(pose.x+71)*retain;pose.y=-42+(pose.y+42)*retain;
            ++shotgunReturnWait;age=(altHeld||reloadHeld)?16:26;if(age==16)shotgunReturnWait=0;
        }else if(age<=32){if(age==27)age=28;frame=age<=28?'F':'E';pose.x+=11;pose.y+=6;pose.rotation-=pump*.1f;}
        else if(age==33){frame='C';pose.x+=3;pose.y+=6;pose.rotation-=pump*.1f;motion.set(pose,false);}
        else if(age==34){frame='C';pose.x+=10;pose.y+=24;pose.rotation=0;}
        else if(age==35){frame='A';pose.pivotX=.5f;pose.pivotY=0;pose.scaleX=pose.scaleY=random(.95f,1.1f);pose.x=random(-4,4);pose.y=random(1,6);pose.rotation=random(-.5f,.5f);motion.set(pose,false);}
        else if(age<=37){frame='A';pose.scaleX=1+(pose.scaleX-1)*.4f;pose.scaleY=1+(pose.scaleY-1)*.4f;pose.x*=.4f;pose.y*=.4f;pose.rotation*=.4f;}
        else{frame='A';pose={};pose.pivotX=.5f;if(age>=42)age=-1;}
        return;
    }
    static const std::string frames="AAAAAAAAABCCEEFFFFHHHHGFFFFEEEECCAAAAAAA";
    if(age>=int(frames.size())){age=-1;frame='A';pose={};return;}frame=frames[age];
    if(age==0){pose.pivotX=.5f+(random(0,1)<.5f?-.2f:.2f)*power;pose.pivotY=.3f+.3f*power;pose.scaleX=1.3f+.1f*power;pose.scaleY=1+(.3f+.1f*power)*1.5f;pose.x=-3+power;pose.y=14+2*power;pose.rotation=(pose.pivotX-.5f)*-pose.x;}
    else if(age>=2&&age<=6){pose.scaleX-=std::min(.05f,std::max(0.f,pose.scaleX-1));pose.scaleY=age<5?pose.scaleY-std::min(.07f,std::max(0.f,pose.scaleY-1)):pose.scaleX;float f=age<5?.4f:.5f;pose.x*=f;pose.y*=f;pose.rotation*=f;}
    else if(age==7||age==8){pose.x=pose.y=pose.rotation=0;pose.scaleX=pose.scaleY=1;}
    else if(age==9){pump=random(-10,10);pose.pivotX=(pump+10)/20;pose.pivotY=1;pose.rotation+=pump*.1f;}
    else if(age==10||age==11){pose.x-=4;pose.y-=6;pose.rotation+=pump*.1f;}
    else if(age==12){pose.scaleY=.9f;pose.x=-75+(pose.x+75)*.8f;pose.y=-44+(pose.y+44)*.8f;pose.rotation+=pump*.1f;}
    else if(age>=13&&age<=15){pose.scaleX=1+(pose.scaleX-1)*.6f;pose.scaleY=1.05f+(pose.scaleY-1.05f)*.6f;pose.x=-80+(pose.x+80)*.7f;pose.y=-44+(pose.y+44)*.7f;pose.rotation+=pump*.1f;}
    else if(age==16){pose.x=-71;pose.y=-42;pose.rotation+=pump*.1f;}
    else if(age==17){pose.x+=3;pose.y+=8;pose.scaleX=1;pose.scaleY=.9f;soundEvents.push_back("ShotgunPumpDown");}
    else if(age==18||age==19){if(age==18)ejectShell();pose.x+=5;pose.y+=17*random(.9f,1.2f);pose.scaleX=age==18?1.f:1.05f;pose.scaleY=age==18?1.f:.95f;if(manualReload&&age==18){shotgunCatchAge=0;shotgunCatchHand={};shotgunCatchHand.x=300;shotgunCatchHand.y=230;shotgunCatchHand.pivotX=1;shotgunCatchMotion.set(shotgunCatchHand,false);}}
    else if(age==20){if(manualReload){pose.x=-51+(pose.x+51)*.7f;pose.y=10+(pose.y-10)*.7f;if(altHeld||reloadHeld)age=19;}else{pose.x+=4;pose.y+=9*random(.9f,1.2f);}pose.scaleX=1.07f;pose.scaleY=.94f;}
    else if(age==21){pose.x-=4;pose.y-=6*random(.9f,1.2f);pose.scaleX=1.05f;pose.scaleY=.95f;}
    else if(age==22){pose.x-=2;pose.y-=(pose.y+36)*.6f;pose.scaleX=1.02f;pose.scaleY=.94f;}
    else if(age==23){pose.x-=5;pose.y=-36;pose.scaleX=1;pose.scaleY=.96f;soundEvents.push_back("ShotgunPumpUp");}
    else if(age==24||age==25){pose.x+=1;pose.y-=5;pose.rotation-=pump*.1f;pose.scaleX=pose.scaleY=1;}
    else if(age>=26&&age<=30){pose.x+=11;pose.y+=6;pose.rotation-=pump*.1f;}
    else if(age==31){pose.x+=3;pose.y+=6;pose.rotation-=pump*.1f;}
    else if(age==32){pose.x+=10;pose.y+=24;pose.rotation=0;}
    else if(age==33){pose.pivotX=.5f;pose.pivotY=0;pose.scaleX=pose.scaleY=random(.95f,1.1f);pose.x=random(-4,4);pose.y=random(1,6);pose.rotation=random(-.5f,.5f);}
    else if(age==34||age==35){pose.scaleX=1+(pose.scaleX-1)*.4f;pose.scaleY=1+(pose.scaleY-1)*.4f;pose.x*=.4f;pose.y*=.4f;pose.rotation*=.4f;}
    else if(age>=36){pose={};pose.pivotX=.5f;}
}
void Weapons::rocket(){
    if(age>=16){age=-1;frame='A';pose={};return;}
    frame=age<10?'C':age==10?'B':'A';
    if(age==0){pose.pivotX=.5f;pose.pivotY=0;rotation=random(-2,2);}
    else if(age<=2){pose.scaleX+=.4f;pose.scaleY+=.4f;pose.x+=rotation;pose.y+=2;pose.rotation-=rotation;}
    else if(age<=4){pose.scaleX+=.15f;pose.scaleY+=.15f;pose.x+=rotation*.5f;pose.y+=7;}
    else if(age==5){recoveryX=random(-4,4);recoveryY=random(-2,4);misalignment=random(.95f,.98f);}
    else if(age<=10){float r=random(.6f,.7f);pose.x=recoveryX+(pose.x-recoveryX)*r;pose.y=recoveryY+(pose.y-recoveryY)*r;pose.scaleX=misalignment+(pose.scaleX-misalignment)*r;pose.scaleY=misalignment+(pose.scaleY-misalignment)*r;pose.rotation*=r;}
    else if(age<=14){float r=random(.6f,.7f);pose.x*=r;pose.y*=r;pose.scaleX=1+(pose.scaleX-1)*r;pose.scaleY=1+(pose.scaleY-1)*r;pose.rotation*=r;}
    else {pose={};}
    // GunFlash2 copies only the flash position, then expands independently.
    if(age==1){rocketFlash={};rocketFlash.x=pose.x;rocketFlash.y=pose.y;}
}
std::vector<Layer> Weapons::baseLayers(float f,bool bareHands)const{
    if(!supported(weapon))return {};
    auto transform=motion.sample(f);
    float interpolatedMuzzle=oldMuzzle+(muzzle-oldMuzzle)*std::clamp(f,0.f,1.f);
    if(weapon==0){
        const std::string prefix=bareHands?"PUN3":"PUN1";
        auto idle=doomweapon::Transform{};
        idle.y=age>=0?std::min(60.f,age<8?age*10.f:(22-age)*5.f):0;
        std::vector<Layer> result={{prefix+"A0",idle,false}};
        if(age>=0&&age<15)result.push_back({prefix+(alternatePunch?"E0":"D0"),transform,false,1,false,alternatePunch,alternatePunch});
        return result;
    }
    if(weapon==7)return {{std::string("SAW1")+frame+"0",transform,false}};
    if(weapon==8){
        const int displayAge=manualReload&&ssgCatchAge>=0&&ssgCatchAge<17?40:age;
        std::vector<Layer> result;if(displayAge!=41)result.push_back({std::string("SH12")+frame+"0",transform,false});
        if(manualReload&&ssgCatchAge>=0){if(ssgCatchAge>=1&&ssgCatchAge<25)result.push_back({std::string("SH12")+(ssgCatchAge<5||ssgCatchAge>=16?'H':'G')+"0",handMotion.sample(f),false});}
        else if(age>=28&&age<46)result.push_back({std::string("SH12")+(age<41?'G':'H')+"0",handMotion.sample(f),false});
        if(displayAge>=23&&displayAge<42){auto barrels=transform;if(displayAge==41){barrels.x+=90;barrels.y+=130;}result.push_back({"SH12I0",barrels,false});}
        if(age>=0&&age<5)result.push_back({std::string("SH12")+"NOOON"[age]+"0",transform,true,1,true});
        return result;
    }
    if(weapon==3){
        // GunRaise overrides the initial Muzzle pivot for every physical
        // layer. Scaling about the bottom sinks the outer tips into the disk.
        transform.pivotX=.5f;transform.pivotY=0;
        std::vector<Layer> result;float angle=oldSpin+(spin-oldSpin)*std::clamp(f,0.f,1.f);
        bool flash=age>=0&&age<5;
        auto barrel=[&](int n,const char*prefix,bool muzzlePart,bool bright=false,float alpha=1,bool additive=false){
            float rotation=angle-n*60,rad=rotation*.0174532925199433f;
            auto t=transform;
            t.x+=std::cos(rad)*30-2;t.y+=std::sin(rad)*20+20;
            int frame=int((std::cos(rad)*30+32)/10);
            // Reflection frames A/G are intentionally absent in the pack:
            // at those barrel angles there is no reflection to draw.
            if(std::string(prefix)=="CH2B"&&(frame<1||frame>5))return;
            if(muzzlePart){frame=std::clamp(frame-1,0,4);t.scaleX=t.scaleY=1-std::abs(t.x-transform.x)/120;}
            result.push_back({std::string(prefix)+char('A'+std::clamp(frame,0,6))+"0",t,bright,alpha,additive});
        };
        for(int i=0;i<6;++i)barrel(i,flash?"CH2M":"CH1M",true);
        result.push_back({std::string(flash?"CH2G":"CH1G")+frame+"0",transform,false});
        for(int i=0;i<6;++i){float rotation=std::fmod(angle-i*60+36000,360.f);if(rotation>=220&&rotation<=335)barrel(i,"CH2B",false,false,std::max(0.f,std::sin((rotation-220)*1.5f*.0174532925199433f)*.26f),true);}
        for(int i=0;i<6;++i){float rotation=std::fmod(angle-i*60+36000,360.f);if(rotation<=200||rotation>=340){auto t=transform;float rad=rotation*.0174532925199433f;t.x+=std::cos(rad)*30-2;t.y+=std::sin(rad)*20+20;result.push_back({"CH1SA0",t,false});}}
        for(int i=0;i<6;++i)barrel(i,"CH1B",false);
        for(int i=0;i<6;++i){float rotation=std::fmod(angle-i*60+36000,360.f);if(rotation>200&&rotation<340){auto t=transform;float rad=rotation*.0174532925199433f;t.x+=std::cos(rad)*30-2;t.y+=std::sin(rad)*20+20;result.push_back({"CH1SA0",t,false});}}
        for(int i=0;i<6;++i){float rotation=std::fmod(angle-i*60+36000,360.f);if(rotation>200&&rotation<340)barrel(i,"CH1B",false);}
        result.push_back({std::string("CH1R")+frame+"0",transform,false});
        // GunFlash is psprite -2, behind every physical barrel (IDs 2+).
        if(age>=0&&age<6){auto t=transform;t.pivotX=.5f;t.pivotY=1;t.scaleX=t.scaleY=age==0?(spinSpeed<22.5f?.7f:1.f):std::max(.01f,std::min(1.f,spinSpeed/30.f)-.25f*std::max(0,age-3));result.insert(result.begin(),{std::string("CH1F")+"AABBBB"[age]+"0",t,true,1,true});}return result;
    }
    if(weapon==5){
        bool reload=age>=4&&age<=21;
        std::vector<Layer> result={{std::string(reload?"PLSR":"PL1G")+frame+"0",transform,false}};
        if(!reload||(age>=7&&age<=16)){
            // Source psprite IDs: flash -4, muzzle -2, body +2.
            auto front=transform;front.y+=interpolatedMuzzle*(reload?8:3)+(reload&&age==7?3:0);result.insert(result.begin(),{std::string("PLSO")+(reload?'I':frame)+"0",front,false});
            if(age>=0&&age<=3)result.insert(result.begin(),{std::string("PL1F")+frame+"0",front,true,1,true});
        }return result;
    }
    if(weapon==6){
        std::vector<Layer> result={{"BFG1A0",transform,false}};
        auto front=transform;front.y+=interpolatedMuzzle*3;
        result.insert(result.begin(),{"BFGMA0",front,age>=25&&age<37});
        if(age>=0&&age<25)result.push_back({"BFGOA0",transform,true,std::min(.8f,age*.05f),true});
        if(age>=25&&age<37){int tick=age-25;doomweapon::Transform flash;flash.y=9;flash.pivotX=flash.pivotY=.5f;flash.scaleX=flash.scaleY=tick==0?.1f:tick<3?1.f:tick==3?.9f:tick<6?(.9f+.1f*(tick-3)):(1.1f-.05f*(tick-5));if(tick>=6)flash.scaleY=1.1f-.2f*(tick-5);result.insert(result.begin(),{std::string("BF1F")+(tick<2?'A':tick==2?'B':'C')+"0",flash,true,std::max(0.f,1-.15f*std::max(0,tick-5)),true});}return result;
    }
    std::string prefix=weapon==1?"P1SG":weapon==2?"SH1G":"MI1G";
    std::vector<Layer> result={{prefix+frame+"0",transform,false}};
    if(weapon==4&&age>=2&&age<10){
        // GunFlash2 is a separate negative overlay, behind the launcher.
        auto explosion=rocketFlash;explosion.pivotX=explosion.pivotY=.5f;
        float scale=age<5?1+.1f*(age-2):age==5?1.f:1.015f+.025f*std::max(0,age-6);
        explosion.scaleX=explosion.scaleY=scale;
        result.insert(result.begin(),{std::string("MI1F")+(age==2?'C':age<5?'D':'E')+"0",explosion,true,.7f-.2f*std::max(0,age-6),true});
    }
    if(age>=0&&age<(weapon==4?2:4)){const char*flash=weapon==1?"ABBC":weapon==2?"ABBA":"AB";result.push_back({std::string(weapon==1?"P1SF":weapon==2?"SH1F":"MI1F")+flash[age]+"0",transform,true,1,true});}return result;
}
void Weapons::plasma(){
    if(age<=3){plasmaFrame=plasmaFrame%7+1;frame='A'+plasmaFrame;pose.pivotX=.5f;pose.pivotY=1;pose.x=random(-1,1);pose.y=random(3,4);pose.scaleY=random(1,1.1f);pose.scaleX=pose.scaleY+random(-.05f,.05f);muzzle+=.8f;return;}
    const int phase=age-1;
    if(phase==3){frame='A';pose.pivotX=.5f;pose.pivotY=1;pose.x=34;pose.y=20;pose.scaleX=1;pose.scaleY=1.2f;}
    else if(phase==4){frame='B';pose.x-=9;pose.scaleX+=.02f;pose.rotation+=2;}
    else if(phase==5){frame='B';pose.x-=15;pose.y+=2;pose.scaleX+=.04f;pose.scaleY+=.06f;pose.rotation+=4;}
    else if(phase==6){frame='D';pose.pivotX=pose.pivotY=1;pose.scaleX=pose.scaleY=.95f;pose.x=7;pose.y=12;pose.rotation=1;muzzle=std::max(1.f,muzzle);}
    else if(phase<=10){frame='D';pose.rotation=0;muzzle=phase<9?muzzle*.3f:random(0,.5f);pose.x=-4+(pose.x+4)*.7f;pose.y=20+(pose.y-20)*.7f;}
    else if(phase<=15){frame='D';muzzle=phase<=12?random(0,.2f):0;pose.x=10+(pose.x-10)*.9f;pose.y=10+(pose.y-10)*.9f;}
    else if(phase==16){frame='C';pose.x+=3;pose.y-=1;}
    else if(phase<=20){frame="CCBA"[phase-17];muzzle=0;pose.x+=10;}
    else if(phase==21){frame='A';pose={};pose.pivotX=.5f;pose.pivotY=1;pose.x=random(4,7);pose.y=random(5,9);pose.x=10+(pose.x-10)*.7f;pose.y=11+(pose.y-11)*.7f;}
    else if(phase<=23){pose.x=10+(pose.x-10)*.7f;pose.y=11+(pose.y-11)*.7f;}
    else if(phase<=31){pose.x*=.7f;pose.y*=.7f;}
    else {age=-1;frame='A';pose={};muzzle=0;}
}
void Weapons::bfg(){if(charging&&age>24)age=24;
    frame='A';
    if(age==0){muzzle*=.7f;power=0;}
    else if(age<25){pose.x=random(-1,1)*power;pose.y=random(-1,1)*power;muzzle*=.7f;power+=.1f;}
    else if(age==25)++muzzle;
    else if(age<=28){++muzzle;pose.y+=random(6,8);float scale=random(1.03f,1.07f);pose.scaleX*=scale;pose.scaleY*=scale;}
    else if(age<=30)muzzle=5;
    else if(age<=35){muzzle-=.8f;pose.x*=.6f;pose.y*=.6f;pose.scaleX=1+(pose.scaleX-1)*.6f;pose.scaleY=1+(pose.scaleY-1)*.6f;}
    else {age=-1;muzzle=0;pose={};}
}
void Weapons::chaingun(){
    if(age==0)ejectCasing(2);
    if(age<4)spinSpeed=std::min(30.f,spinSpeed+12); // user's source speed multiplier 1.5
    if(age==0){pose.x+=(random(0,1)<.5f?-1.5f:1.5f)*power;pose.y+=3+2*power;}
    else if(age==1){pose.x+=pose.x*.5f;pose.y+=1+power*.75f;}
    else if(age==2){pose.x*=.5f;pose.y*=.5f;}
    else if(age==3){pose.x=0;pose.y*=random(.1f,.2f);}
    else if(age==4){pose.x-=random(-1,1);pose.y-=random(-3,3);}
    else if(age<=9){pose.x*=.5f;pose.y*=.5f;}
    else{age=-1;pose.x=pose.y=0;}
}
void Weapons::superShotgun(){
    // GunFire/GunReload, standard two-handed reload variant.
    frame='A';
    // GunCoolIdle branches immediately after the split barrels appear,
    // before the two normal-reload offset/rotation steps (ages 24 and 25).
    if(manualReload&&age>=24){superShotgunCatch();if(ssgCatchAge<18)return;}
    if(age==27&&!manualReload){ejectCasing(3,0);ejectCasing(3,1);}
    if(age>=81){age=-1;pose={};return;}
    if(age==0){pose.pivotX=.5f+random(-.15f,.15f)*power;pose.pivotY=.3f+.3f*power;pose.scaleX=1.3f+.1f*power;pose.scaleY=1+(.3f+.1f*power)*1.8f;pose.x=-(pose.pivotX-.5f)*2;pose.y=14+4*power;pose.rotation=(pose.pivotX-.5f)*4*power;}
    else if(age==1){pose.scaleX+=.07f+.015f*power;pose.scaleY+=(.07f+.015f*power)*1.5f;pose.x-=pose.pivotX-.5f;pose.y+=7+2*power;pose.rotation+=(pose.pivotX-.5f)*power;}
    else if(age<9){float r=age<6?.7f:.8f;pose.x*=r;pose.y*=r;pose.rotation*=r;pose.scaleX=1+(pose.scaleX-1)*r;pose.scaleY=1+(pose.scaleY-1)*r;}
    else if(age==9){pose={};pose.x=random(-2,2);pose.y=random(0,2);pose.rotation=random(-.5f,.5f);}
    else if(age==10){if(manualReload)soundEvents.push_back("SSGOpen");frame='B';pose.pivotX=.5f;pose.pivotY=1;pose.x=-random(8,12);pose.y=-random(3,5);}
    else if(age<13){frame='C';pose.x-=10;pose.y-=5;pose.scaleY=.9f;if(age==12){pose.pivotX=.6f;pose.pivotY=.3f;pose.scaleX=1.1f;pose.scaleY=1.15f;}}
    else if(age<=22){frame='D';
        if(age<15){pose.x-=20;pose.y-=5;pose.scaleX=age==13?.9f:.95f;pose.scaleY=age==13?.85f:.925f;}
        else if(age<=16){pose.x-=10;pose.y-=2;pose.rotation+=random(2,4);pose.scaleX=pose.scaleY=1;}
        else if(age<=18){pose.x-=3;pose.y-=1;pose.rotation+=random(1.5f,2);pose.pivotX=.5f;pose.pivotY=1;}
        else if(age==19){pose.rotation-=random(.01f,.1f);pose.x-=random(.01f,.1f);pose.y-=random(.25f,.5f);}
        else if(age==20){pose.rotation=-8;pose.scaleX-=.05f;pose.scaleY-=.05f;pose.x+=30;pose.y+=8;}
        else if(age==21){pose.rotation-=20;pose.scaleX-=.1f;pose.scaleY-=.1f;pose.x+=70;pose.y+=20;}
        else {pose.rotation=-90;pose.scaleX-=.025f;pose.scaleY-=.025f;pose.x+=60;pose.y+=16;}
    }else if(age<=25){frame='J';if(age==23){pose={};pose.x=pose.y=45;pose.rotation=50;pose.pivotX=.5f;pose.pivotY=0;}else{pose.x-=25;pose.y-=25;pose.rotation-=age==24?30:20;}}
    else if(age<=40){frame='F';
        if(age==26){pose.x+=random(6,9);pose.y+=random(6,9);}
        else if(age==27){pose.x*=.7f;pose.y*=.7f;}
        else if(age==28||age==36){pose={};}
        else if(age<34){pose.x-=random(.1f,.3f);pose.y+=random(.1f,.3f);}
        else if(age<36){pose.x*=.7f;pose.y*=.7f;}
        else if(age==37){pose.x+=14;pose.y+=10;}
        else if(age==38){pose.x+=10;pose.y+=9;}
        else {pose.x=82+(pose.x-82)*.7f;pose.y=52+(pose.y-52)*.7f;}
    }else if(age==41){frame='W';}
    else if(age<=49){frame='K';if(age==42){pose.x=85;pose.y=55;pose.rotation=-40;}else{
        static const float x[]={0,-4,-10,-24,-40,-40,-22},y[]={-24,-30,-16,-10,6,16,20},r[]={10,16,14,5,10,10,7};
        int n=age-43;pose.x+=x[n];pose.y+=y[n];pose.rotation+=r[n];}}
    else if(age<=51){frame='Q';pose.x=age==50?-25.f:-30.f;pose.y=age==50?21.f:30.f;pose.rotation=0;}
    else if(age==52){if(manualReload)soundEvents.push_back("SSGClose");pose.scaleX=.8f;pose.scaleY=2.5f;pose.pivotX=.5f;pose.pivotY=0;pose.x=-34;pose.y=36;}
    else {pose.x*=.65f;pose.y*=.65f;pose.rotation*=.65f;pose.scaleX=1+(pose.scaleX-1)*.4875f;pose.scaleY=1+(pose.scaleY-1)*.4875f;}
    if(manualReload)return; // GunCoolIdleHand has its own concurrent timeline.
    if(age==28){hand={};hand.pivotX=hand.pivotY=.5f;hand.x=-70+random(-90,80);hand.y=80+random(0,20);hand.rotation=-15;recoveryX=-48+random(-20,0);recoveryY=-3+random(-5,5);rotation=random(4,10);misalignment=random(1.06f,1.31f);handMotion.set(hand,false);}
    else if(age<34&&age>28){hand.x=recoveryX+(hand.x-recoveryX)*.6f;hand.y=recoveryY+(hand.y-recoveryY)*.6f;hand.rotation=rotation+(hand.rotation-rotation)*.7f;hand.scaleX=hand.scaleY=misalignment+(hand.scaleX-misalignment)*.6f;}
    else if(age<36&&age>=34){hand.x=14+(hand.x-14)*.6f;hand.y=5.39f+(hand.y-5.39f)*.6f;hand.rotation=-4+(hand.rotation+4)*.7f;hand.scaleX=hand.scaleY=.96f+(hand.scaleX-.96f)*.6f;}
    else if(age==36){hand.x=14;hand.y=5.39f;hand.rotation=-8;hand.scaleX=hand.scaleY=.96f;}
    else if(age>=38&&age<=40){hand.x+=5;hand.y+=12;hand.rotation-=4;}
    else if(age>=41&&age<46){hand.x+=14;hand.y+=24;hand.rotation-=4;}
    handMotion.set(hand);
}
void Weapons::superShotgunCatch(){
    const int t=++ssgCatchAge;
    if(t<18){
        frame=t<2?'J':'F'; // Keep the loaded breech until the casings eject.
        if(t==0){pose.rotation-=15;pose.x-=20;pose.y-=20;}
        else if(t==1){
            pose.rotation-=10;pose.x-=15;pose.y-=15;
            ejectCasing(3,0);ejectCasing(3,1);
            for(int i=0;i<2;++i){auto& shell=shells[shells.size()-2+i];shell.pose.x=115+i*19; shell.pose.y=172-i*5;shell.pose.scaleX=1.145f+i*.05f;shell.pose.scaleY=1.14f+i*.05f;shell.motion.set(shell.pose,false);}
        }else if(t==2){float miss=random(15,18);pose.x-=miss;pose.y-=miss;pose.rotation-=9;}
        else if(t<=9){pose.x*=.7f;pose.y*=.7f;pose.rotation*=.6f;}
        else if(t<=11){pose.x*=random(.6f,.8f);pose.y*=random(.6f,.8f);}
        else if(t==12)pose.x=pose.y=0;
        else if(t==13){pose.x+=14;pose.y+=10;}
        else if(t==14){pose.x+=8;pose.y+=6;}
        else if(t<=16){pose.x+=9;pose.y+=13;}
        else{age=41;frame='W';}
    }
    if(t==1){hand={};hand.pivotX=hand.pivotY=.5f;hand.x=-150;hand.y=100;handMotion.set(hand,false);}
    else if(t>=2&&t<=4&&!shells.empty()){
        const auto& shell=shells.back();auto old=shell.motion.sample(0);
        float x=t==2?-100.f:(t==4?old.x:shell.pose.x)+shell.pose.x-old.x-180;
        float y=t==2?40.f:shell.pose.y+shell.pose.y-old.y-156;
        float retain=t==2?.5f:t==3?.3f:.1f;
        hand.x=x+(hand.x-x)*retain;hand.y=std::max(-5.f,y+(hand.y-y)*retain);
        hand.scaleX=hand.scaleY=std::clamp(shell.pose.scaleX,1.f,2.f);
    }else if(t==5){shells.clear();hand.y=std::max(-5.f,hand.y);hand.x-=9;}
    else if(t==6){hand.x-=12;hand.rotation+=4;}
    else if(t==7){hand.x-=6;hand.rotation+=2;}
    else if(t<=11&&t>=8){hand.x=14+(hand.x-14)*.65f;hand.y=5.93f+(hand.y-5.93f)*.65f;hand.rotation=-7+(hand.rotation+7)*.7f;hand.scaleX=hand.scaleY=.97f+(hand.scaleX-.97f)*.8f;}
    else if(t==12){soundEvents.push_back("SSGLoad");hand.x=14;hand.y=5.39f;hand.rotation=-8;hand.scaleX=hand.scaleY=.96f;}
    if(t==13){hand.x+=16;hand.y+=10;}
    else if(t==14){hand.x+=8;hand.y+=6;}
    else if(t==15||t==16){hand.x+=11;hand.y+=15;}
    else if(t==17){hand.x+=6;hand.y+=21;hand.scaleX-=.025f;hand.scaleY-=.035f;hand.rotation-=4;}
    else if(t>=18&&t<25){hand.x+=14;hand.y+=24;hand.rotation-=4;}
    handMotion.set(hand);
}
void Weapons::fist(){
    // FluidWeapons' movement-directed punch: turning chooses the hand,
    // strafing chooses the opposite hand, otherwise punches alternate.
    if(age==0){if(yawInput!=0)alternatePunch=yawInput>0;else if(sideInput!=0)alternatePunch=sideInput<0;else alternatePunch=!alternatePunch;}
    if(age==0){pose={};pose.x=-20;pose.y=40;pose.scaleX=.96f;pose.scaleY=1.1f;pose.pivotX=1;pose.pivotY=0;soundEvents.push_back("FistSwing");}
    else if(age==2){pose.x=0;pose.y=10;pose.scaleX+=.04f;}
    else if(age==3){pose.x+=random(70,85);pose.y-=random(66,76);pose.scaleX+=.01f;pose.scaleY-=.025f;}
    else if(age==4){pose.x=80;pose.y=-64;pose.scaleY-=.025f;}
    else if(age==5){pose.x+=random(2,4);pose.y+=2;pose.scaleY-=.025f;}
    else if(age==6){pose.x+=random(-1,1);pose.y+=random(-1,1);}
    else if(age==7||age==8){pose.x-=random(.5f,1);pose.y+=random(0,1);pose.rotation-=random(0,1);pose.scaleX+=.005f;pose.scaleY-=.01f;}
    else if(age==9){pose.x+=2;pose.y+=1;pose.rotation+=1;pose.scaleX+=.005f;pose.scaleY+=.01f;}
    else if(age<15&&age>=10){float r=age<12?.85f:.75f;float x=age<12?-55.f:-85.f,y=age<12?25.f:55.f;pose.x=x+(pose.x-x)*r;pose.y=y+(pose.y-y)*r;pose.rotation+=age<12?9:11;pose.scaleX+=.05f;pose.scaleY+=.05f;}
    else if(age>=22){age=-1;pose={};}
}
void Weapons::chainsaw(){
    pose.pivotX=.5f;pose.pivotY=1;
    if(age<0){frame='C'+int(random(0,3.99f));pose.x=random(-.5f,.5f);pose.y=random(-.5f,.5f);pose.rotation=random(-.25f,.25f);pose.scaleX=pose.scaleY=1+random(0,.025f);}
    else if(age==1){pose.scaleX=pose.scaleY=.8f;pose.x+=6;pose.y-=2;pose.rotation-=3.5f;}
    else if(age==2){frame='G';pose.x=-2;pose.y=9.5f;pose.scaleX=pose.scaleY=1;pose.rotation=0;}
    else if(sawHold>0){frame='G'+age%4;pose.x=random(-2,2);pose.y=9.5f+random(-2,2);pose.rotation=random(-1,1);pose.scaleX=pose.scaleY=random(.97f,1.03f);}
    else {frame='C'+age%4;pose.x*=.7f;pose.y*=.7f;pose.rotation*=.7f;pose.scaleX=1+(pose.scaleX-1)*.7f;pose.scaleY=1+(pose.scaleY-1)*.7f;if(std::abs(pose.y)<.1f){age=-1;pose={};}}
}
void Weapons::auxiliary(){
    bool altPress=altHeld&&!oldAlt,reloadPress=reloadHeld&&!oldReload;
    oldAlt=altHeld;oldReload=reloadHeld;
    if(auxReady&&age<0&&auxAction==0){
        if(weapon==0){if(reloadPress)secondFist=!secondFist;if(altPress){auxAction=3;auxAge=-1;fistEnd=-1;bumpMotion.reset();}}
        if(weapon==1){if(reloadPress){secondGrip=!secondGrip;gripAge=0;soundEvents.push_back("WeapGlove2");}if(altPress){auxAction=2;auxAge=-1;}}
        if(weapon==2&&(altPress||reloadPress)){manualReload=true;shotgunReturnWait=0;age=9;pose={};shotgun();}
        if(weapon==8&&(altPress||reloadPress)){manualReload=true;ssgCatchAge=-1;age=10;pose={};superShotgun();}
        if(weapon>=3&&weapon<=6&&reloadPress){auxAction=1;auxAge=-1;}
        if(weapon==7&&(altPress||reloadPress)){auxAction=4;auxAge=-1;}
        if(weapon==6&&altPress)bfgIdle=!bfgIdle;
    }
    if(auxReady&&age<0){
        // Chaingun rotation is updated before drawing in tick(): held alt
        // preserves firing momentum, as in the source mod, rather than reversing it.
        if(weapon==4){muzzle=std::clamp(muzzle+(altHeld?1.f:-1.f),0.f,2.f);frame='A'+int(muzzle);}
        if(weapon==5){if(altPress)muzzle=std::min(2.f,muzzle+1);else if(altHeld)muzzle=std::min(1.f,muzzle+.2f);else muzzle*=.5f;}
        if(weapon==6&&bfgIdle)muzzle=random(-.2f,.2f);
    }
    if(gripAge>=0&&++gripAge>12)gripAge=-1;
    if(auxAction==0)return;
    ++auxAge;
    auto approach=[](float value,float target,float retention){return target+(value-target)*retention;};
    if(auxAction==1){
        // Shared GunShowingHand/HandShowing states in the four heavy weapons.
        if(auxAge==0){inspectionHold=-1;auxHand={};auxHand.x=180;auxHand.y=80;auxHand.rotation=-30;auxHand.scaleX=auxHand.scaleY=.9f;auxHand.pivotX=auxHand.pivotY=1;auxHandMotion.set(auxHand,false);}
        float targetX=weapon==3?80.f:weapon==4?50.f:40.f;
        if(inspectionHold<0){pose.x=approach(pose.x,targetX,.9f);pose.y=approach(pose.y,20,.9f);if(weapon==4)pose.rotation=approach(pose.rotation,10,.9f);if(std::abs(pose.x-targetX)<1&&std::abs(pose.y-20)<1)inspectionHold=0;}
        else if(inspectionHold<32){++inspectionHold;pose.x=approach(pose.x,targetX+random(-1,2),.95f);pose.y=approach(pose.y,20+random(0,3),.95f);}
        else {float end=weapon==3?-1.f:0.f;pose.x=approach(pose.x,end,.95f);pose.y=approach(pose.y,end,.95f);if(weapon==4)pose.rotation*=.95f;if(std::abs(pose.x)<1&&std::abs(pose.y)<1&&auxAge>=89){auxAction=0;pose={};}}
        if(auxAge>0&&auxAge<=55){float retain=weapon==3?.93f:.95f;auxHand.x=approach(auxHand.x,240,retain);auxHand.y=approach(auxHand.y,-10,retain);auxHand.rotation*=.95f;}
        else if(auxAge>55){float delta=auxHand.y+(weapon==3?9.f:6.f);auxHand.rotation-=delta*(weapon==3?.1f:.21f);auxHand.x+=delta*(weapon==3?-.1f:.2f);auxHand.y+=delta*.2f;}
    }else if(auxAction==2){
        // Pistol's manual slide-rack, including the catching hand.
        pose.pivotX=pose.pivotY=.5f;
        if(auxAge<15){
            frame='H';
            if(auxAge==0){pose.x=approach(pose.x,30,.9f);pose.y=approach(pose.y,10,.9f);pose.rotation=approach(pose.rotation,20,.9f);}
            else if(auxAge<4){pose.x=approach(pose.x,36,.8f);pose.y=approach(pose.y,15,.8f);pose.rotation=approach(pose.rotation,20,.8f);}
            else if(auxAge<10){pose.x=approach(pose.x,30,.8f);pose.y=approach(pose.y,10,.8f);pose.rotation=approach(pose.rotation,20,.8f);}
            else {float retain=auxAge<13?.9f:.5f;pose.x=approach(pose.x,auxAge<13?29.f:25.f,retain);pose.y=approach(pose.y,auxAge<13?10.f:5.f,retain);}
        }
        else if(auxAge<17){frame=auxAge==15?'B':'C';pose.x=approach(pose.x,29,.5f);pose.y=approach(pose.y,40,.5f);}
        else if(auxAge==17){frame='J';pose.x=approach(pose.x,29,.5f);pose.y=approach(pose.y,40,.5f);}
        else if(auxAge<20){frame='J';pose.x=approach(pose.x,20,.7f);pose.y*=.7f;pose.rotation*=.8f;}
        else if(auxAge<26){frame="GEDIBA"[auxAge-20];pose.x*=.7f;pose.y*=.7f;pose.rotation*=.8f;}
        else {frame='A';pose.x*=.8f;pose.y*=.8f;pose.rotation*=.8f;}
        if(auxAge==17)soundEvents.push_back("PistolReload2");
        if(auxAge==22)soundEvents.push_back("PistolReload");
        // Effects.DoCasing with Catch enabled, followed by GunCoolIdleHand's
        // catch and BulletLay. The casing is an absolute screen overlay.
        if(auxAge==17){catchBullet={};catchBullet.x=171;catchBullet.y=129;catchBullet.rotation=140;catchBullet.scaleX=catchBullet.scaleY=.8f;catchBullet.pivotX=catchBullet.pivotY=.5f;catchDirection=random(-.7f,1.5f);catchBulletMotion.set(catchBullet,false);}
        if(auxAge>17&&auxAge<24){float life=20.f-(auxAge-17);float vz=(life-17)/20;if(vz==0)vz=-.01f;float speed=random(1,2);catchBullet.y-=vz*100*speed*catchBullet.scaleX;speed*= -.5f;catchBullet.x+=16*speed*catchBullet.scaleX;catchBullet.scaleX*=1+.05f*catchDirection;catchBullet.scaleY=catchBullet.scaleX;catchBullet.rotation-=30*speed;}
        if(auxAge==4){auxHand={};auxHand.x=-30;auxHand.y=60;auxHand.rotation=60;auxHand.scaleX=auxHand.scaleY=.7f;auxHand.pivotX=auxHand.pivotY=.5f;auxHandMotion.set(auxHand,false);}
        if(auxAge>4&&auxAge<11){auxHand.x=approach(auxHand.x,55,.7f);auxHand.y=approach(auxHand.y,-50,.7f);auxHand.rotation=approach(auxHand.rotation,5,.7f);auxHand.scaleX=auxHand.scaleY=approach(auxHand.scaleX,.9f,.7f);}
        else if(auxAge==11){auxHand.x-=30;auxHand.y+=20;auxHand.rotation*=.7f;auxHand.scaleX=auxHand.scaleY=approach(auxHand.scaleX,1,.6f);auxHandMotion.set(auxHand,false);}
        else if(auxAge<15&&auxAge>11){auxHand.x=approach(auxHand.x,15,.8f);auxHand.y=approach(auxHand.y,-20,.8f);auxHand.rotation*=.7f;auxHand.scaleX=auxHand.scaleY=approach(auxHand.scaleX,1,.6f);}
        else if(auxAge<17&&auxAge>=15){auxHand.x=approach(auxHand.x,20,.5f);auxHand.y=approach(auxHand.y,20,.5f);auxHand.scaleX=auxHand.scaleY=approach(auxHand.scaleX,1.05f,.9f);}
        else if(auxAge==17){auxHand.x=approach(auxHand.x,55,.7f);auxHand.y=approach(auxHand.y,90,.7f);auxHand.rotation=approach(auxHand.rotation,-30,.7f);auxHand.scaleX=auxHand.scaleY=approach(auxHand.scaleX,1,.6f);}
        else if(auxAge==18){auxHand.x=0;auxHand.y=90;auxHand.rotation=0;auxHandMotion.set(auxHand,false);}
        else if(auxAge>18&&auxAge<24){auxHand.x=approach(auxHand.x,catchBullet.x-95,.6f);auxHand.y=approach(auxHand.y,20,.6f);auxHand.scaleX=auxHand.scaleY=std::clamp(catchBullet.scaleX,.1f,2.f);}
        else if(auxAge==24)soundEvents.push_back("WeapGlove2");
        else if(auxAge>24&&auxAge<30){auxHand.x=approach(auxHand.x,10,.8f);auxHand.y=approach(auxHand.y,10,.8f);auxHand.scaleX=auxHand.scaleY=approach(auxHand.scaleX,.9f,.8f);}
        else if(auxAge>=30){auxHand.x=approach(auxHand.x,-30,.8f);auxHand.y=approach(auxHand.y,90,.8f);}
        if(auxAge>=24){catchBullet.x=auxHand.x+(auxAge==24?90:87);catchBullet.y=auxHand.y+145;catchBullet.scaleX=auxHand.scaleX;catchBullet.scaleY=auxHand.scaleY;if(auxAge>24)catchBullet.rotation=approach(catchBullet.rotation,240,.5f);}
        catchBulletMotion.set(catchBullet,auxAge!=24);
        if(auxAge>=37){auxAction=0;pose={};frame='A';}
    }else if(auxAction==3){
        // MainHandCoolIdle[Pre/Ready] and SecndHandCoolIdle, with independent
        // psprite histories. The incoming fist (layer 2) stays behind layer 3.
        const int t=auxAge;
        auto mainSet=[&](float x,float y,float sx,float sy){auxHand.x=x;auxHand.y=y;auxHand.scaleX=sx;auxHand.scaleY=sy;};
        auto bumpSet=[&](float x,float y,float sx,float sy){bumpHand.x=x;bumpHand.y=y;bumpHand.scaleX=sx;bumpHand.scaleY=sy;};
        if(t==0){
            auxHand={};auxHand.pivotX=1;auxHand.pivotY=.5f;
            mainSet(secondFist?0:-40,secondFist?0:60,secondFist?1:.65f,1);
            bumpHand={};bumpHand.pivotX=bumpHand.pivotY=.5f;
            bumpSet(260+random(9,14),-10+random(-2,2),1.35f,1.35f);
            auxHandMotion.set(auxHand,false);bumpMotion.set(bumpHand,false);
        }
        if(fistEnd<0){
            if(t>=1&&t<=3){
                if(secondFist){auxHand.x+=random(5,7);auxHand.y+=random(-1,1);auxHand.scaleX-=.05f;auxHand.scaleY+=random(.01f,.02f);}
                else{auxHand.x=34+(auxHand.x-34)*.7f;auxHand.y=-3+(auxHand.y+3)*.7f;auxHand.scaleX=.75f+(auxHand.scaleX-.75f)*.7f;auxHand.scaleY=1.1f+(auxHand.scaleY-1.1f)*.7f;}
            }
            if(t==4||t==5)mainSet(30,0,.75f,1.1f);
            if(t==6)mainSet(random(29,31),0,.7f,1.05f);
            if(t==7)mainSet(random(29,31),0,.73f,1.075f);
            if(t==8)mainSet(30,0,.75f,1);
            if(t==10||t==11){auxHand.y+=random(-2,2);auxHand.scaleY+=random(.005f,.01f);}
            if(t==12)mainSet(20,random(-2,2),auxHand.scaleX,auxHand.scaleY);
            if(t==14){auxHand.y+=random(-2,2);auxHand.scaleX+=random(.03f,.05f);auxHand.scaleY-=random(.01f,.02f);}
            if(t==15)mainSet(30,0,.67f,1.1f);
            if(t==16)mainSet(random(29,31),0,.7f,1.05f);
            if(t==17)mainSet(random(29,31),0,.73f,1.075f);
            if(t==18)mainSet(30,0,.75f,1);
            if(t==1){soundEvents.push_back("FistSwing");bumpSet(285,-10+random(-4,4),1.31f,1.35f);}
            if(t==2){bumpHand.x-=30;bumpHand.y+=random(-2,2);bumpHand.scaleX=1.25f;bumpHand.scaleY=1.3f;}
            if(t==3){
                if(!altHeld){fistEnd=t;fistAltEnd=true;}
                else{soundEvents.push_back("FistPuch");bumpSet(215,-10+random(-2,2),1.1f,random(1.4f,1.45f));}
            }
            if(t==4)bumpSet(215,-10+random(-1,1),1.17f,1.4f);
            if(t==5)bumpSet(215,-10,1.2f,1.37f);
            if(t==6)bumpSet(215,-10,1.25f,1.35f);
            if(t==8&&!altHeld){fistEnd=t;fistAltEnd=false;}
            if(t==8&&altHeld){bumpHand.x+=random(6,8);bumpHand.y-=3;bumpHand.scaleX-=random(.01f,.02f);bumpHand.scaleY+=random(.005f,.01f);bumpHand.rotation+=2;}
            if(t==9){bumpHand.x+=random(18,24);bumpHand.y-=3;bumpHand.scaleX-=random(.01f,.02f);bumpHand.scaleY+=random(.005f,.01f);bumpHand.rotation+=2;}
            if(t==10){soundEvents.push_back("FistSwing");bumpHand.x=265+random(50,63);bumpHand.y=-19;}
            if(t==12){bumpHand.x-=random(28,34);bumpHand.y+=3;bumpHand.scaleX+=random(.03f,.05f);bumpHand.scaleY-=random(.01f,.02f);bumpHand.rotation-=4;}
            if(t==13){
                if(altHeld){fistEnd=t;fistAltEnd=true;}
                else{soundEvents.push_back("FistPuch");bumpSet(215,-10+random(-3,3),1.08f,1.37f);}
            }
            if(t==14)bumpSet(215,-10+random(-1,1),1.15f,1.36f);
            if(t==15)bumpSet(215,-10,1.2f,1.35f);
            if(t>=19){fistEnd=t;fistAltEnd=false;}
        }
        if(fistEnd>=0){
            const int e=t-fistEnd;
            if(fistAltEnd){
                if(e==0){soundEvents.push_back("FistPuch");bumpSet(210,-10+random(-3,3),1.08f,1.37f);mainSet(25,0,.67f,1.1f);}
                if(e==1){bumpSet(190,-10+random(-1,1),1.15f,1.36f);mainSet(random(6,7),0,.74f,1.03f);}
                if(e>=3&&e<=6){bumpHand.x+=5;bumpHand.y+=(-10-bumpHand.y)*.6f;bumpHand.scaleX+=.0125f;bumpHand.scaleY-=.0025f;auxHand.x+=5;auxHand.scaleX+=e<5?.02f:-.005f;auxHand.scaleY+=e<5?-.02f:-.005f;}
                if(e==7){bumpSet(215,-10,1.2f,1.35f);mainSet(30,0,.75f,1);}
                if(e==11){bumpHand.x+=random(6,8);bumpHand.y+=4;auxHand.scaleX+=.1f;}
                if(e==12||e==13){bumpHand.x=265+(bumpHand.x-265)*.6f;bumpHand.y+=4;auxHand.scaleX+=e==12?.1f:0;}
                if(e>=14){if(secondFist){auxHand.x*=.5f;auxHand.scaleX=auxHand.scaleY=1;}else{auxHand.x-=6;auxHand.y+=10;}}
                if(e>=20)auxAction=0;
            }else{
                if(e<3){bumpHand.x+=e==0?random(6,8):random(16,19);bumpHand.y+=4;bumpHand.scaleX-=random(.01f,.02f);bumpHand.scaleY+=random(.005f,.01f);}
                if(e<2)auxHand.scaleX+=.1f;
                else if(secondFist){auxHand.x=std::max(0.f,auxHand.x-10);auxHand.scaleX=auxHand.scaleY=1;}
                else if(e>=4){auxHand.x-=6;auxHand.y+=10;}
                if(e>=10)auxAction=0;
            }
        }
        bumpMotion.set(bumpHand);
    }else if(auxAction==4){
        // SawIdle + starter-cord pull, repeat while either fidget key is held.
        frame='C'+(auxAge/2)%4;pose.pivotX=.5f;pose.pivotY=.7f;
        if(auxAge<6){pose.x-=5;pose.y+=5;pose.rotation+=2;pose.scaleY=std::max(.8f,pose.scaleY-.04f);}
        if(auxAge==6){soundEvents.push_back("SawUp");auxHand={};auxHand.x=-30;auxHand.y=33;auxHand.scaleX=auxHand.scaleY=.8f;auxHand.pivotY=1;auxHandMotion.set(auxHand,false);}
        int pull=auxAge-6;
        if(pull>=1&&pull<=2){auxHand.x+=27;auxHand.y-=30;auxHand.rotation-=1.5f;auxHand.scaleX=auxHand.scaleY=1-auxHand.y*.01f;pose.y+=6;}
        else if(pull==3){auxHand.x+=2.5f;auxHand.y-=2.5f;pose.y+=4;}
        else if(pull>=6&&pull<=9){auxHand.x-=8;auxHand.y+=10;auxHand.rotation-=2;auxHand.scaleX-=.04f;auxHand.scaleY-=.04f;}
        else if(pull>=10){auxHand.y+=17;auxHand.rotation-=2;}
        if(auxAge>=19){if((altHeld||reloadHeld)&&auxReady){auxAge=5;}else{pose.x*=.7f;pose.y*=.7f;pose.rotation*=.7f;pose.scaleX=1+(pose.scaleX-1)*.7f;pose.scaleY=1+(pose.scaleY-1)*.7f;if(auxAge>=32){auxAction=0;pose={};}}}
    }
    auxHandMotion.set(auxHand);
}
std::vector<Layer> Weapons::layers(float f,bool bareHands)const{
    auto result=baseLayers(f,bareHands);
    auto t=motion.sample(f);
    if(weapon==0&&secondFist&&auxAction!=3&&age<0){auto other=t;result.push_back({std::string(bareHands?"PUN3":"PUN1")+"C0",other,false});}
    if(weapon==1&&secondGrip&&auxAction!=2){
        auto grip=t;if(gripAge>=0){float blend=gripAge/12.f;grip.x-=80*(1-blend);grip.y+=70*(1-blend);}
        result.insert(result.begin(),{"P2SGB0",grip,false});result.push_back({"P2SGA0",grip,false});
    }
    if(weapon==6&&bfgIdle&&age<0)result.push_back({"BFGOA0",t,true,.35f,true});
    if(auxAction==1&&auxAge<(weapon==3?89:88)){
        Layer inspection={"MRD1G0",auxHandMotion.sample(f),false,1,false,false,true};
        if(weapon==3)result.push_back(inspection);
        else result.insert(result.begin(),inspection); // LayHand=-3, behind muzzle/body.
    }
    if(auxAction==2&&auxAge>=4&&auxAge<34)result.push_back({auxAge>=18?"MRD1H0":auxAge>=11&&auxAge<17?"P1SHA0":"CH1HA0",auxHandMotion.sample(f),false});
    if(auxAction==2&&auxAge>=17&&auxAge<34)result.push_back({"PISCD0",catchBulletMotion.sample(f),false,1,false,false,false,auxAge>=24});
    if(auxAction==3){
        std::string prefix=bareHands?"PUN3":"PUN1";
        result.clear();
        const bool bumpVisible=fistEnd<0||auxAge-fistEnd<(fistAltEnd?14:3);
        if(bumpVisible)result.push_back({prefix+"E0",bumpMotion.sample(f),false,1,false,true});
        result.push_back({prefix+(auxAge==0&&secondFist?"C0":"H0"),auxHandMotion.sample(f),false});
    }
    if(auxAction==4&&auxAge>=6&&auxAge<19){auto pull=auxHandMotion.sample(f);auto rope=pull;rope.x+=10;rope.y+=20;rope.scaleX=rope.scaleY=1;rope.pivotX=1;rope.pivotY=0;result.push_back({"SAW1Z0",rope,false});result.push_back({"SAW1Y0",pull,false});}
    if(movementActive){auto movement=movementMotion.sample(f),idle=idleMotion.sample(f);for(auto&layer:result)if(layer.addWeapon){layer.transform.x+=movement.x+idle.x;layer.transform.y+=movement.y+idle.y;layer.transform.rotation+=movement.rotation+idle.rotation;layer.transform.scaleX*=movement.scaleX;layer.transform.scaleY*=movement.scaleY;}}
    for(const auto&shell:shells)result.push_back({shell.kind==1||shell.kind==2?"PISCA0":"SHTCA0",shell.motion.sample(f),false,1,false,false,false,false});
    if(shotgunCatchAge>=0){
        auto catching=shotgunCatchMotion.sample(f);
        if(movementActive){auto movement=movementMotion.sample(f);catching.x+=movement.x;catching.y+=movement.y;}
        result.push_back({shotgunCatchAge<=6?"MRD1I0":"MRD1J0",catching,false});
    }
    return result;
}
std::vector<std::string> Weapons::takeSounds(){auto result=std::move(soundEvents);soundEvents.clear();return result;}
}







