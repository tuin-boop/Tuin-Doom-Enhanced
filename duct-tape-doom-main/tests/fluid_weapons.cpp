#include "../platform/fluid_weapons.h"
#include "../platform/fluid_gloves.h"
#include <iostream>
#include <stdexcept>
#include <set>
static void check(bool good,const char*message){if(!good)throw std::runtime_error(message);}
int main(){try{
    {
        doomfluid::Weapons fists;fists.reset(0);
        fists.attack(0);check(fists.layers(1).back().sprite=="PUN3D0","first punch missing");
        fists.attack(0);auto left=fists.layers(1).back();
        check(left.sprite=="PUN3E0"&&left.flipX&&left.mirror,"alternate punch missing or incorrectly mirrored");
        fists.punchDirection(-800,0);fists.attack(0);
        check(fists.layers(1).back().sprite=="PUN3D0","turn-directed punch incorrect");
        fists.punchDirection(0,-20);fists.attack(0);
        check(fists.layers(1).back().sprite=="PUN3E0","strafe-directed punch incorrect");
    }
    {
        doomfluid::Weapons pistol;pistol.reset(1);
        for(int tic=0;tic<250;++tic){pistol.movement(800,0,0,0,false,false,tic);pistol.tick(1);}
        check(std::abs(pistol.layers(1)[0].transform.x+4)<.1f,"turn sway never reached the source target");
        pistol.reset(1);pistol.movement(0,0,0,0,false,false,0);pistol.tick(1);
        check(std::abs(pistol.layers(1)[0].transform.x)<.001f,"weapon switch retained turn sway");
    }
    std::array<uint8_t,768> palette{};for(int i=0;i<256;++i)palette[i*3]=palette[i*3+1]=palette[i*3+2]=uint8_t(i);
    auto tan=doomfluid::gloveTranslation(palette.data(),0,true);
    check(tan[112]==55&&tan[126]==79&&tan[127]==1,"classic glove range");
    check(tan[198]==9&&tan[201]==12&&tan[250]==125&&tan[254]==127&&tan[175]==152&&tan[181]==159,"pistol metal and sleeve restoration");
    check(tan[100]==100&&tan[210]==210,"gloves recolored unrelated artwork");
    for(const char* name:{"PL1GA0","PL1GB0","PL1GH0","PLSRA0","PLSRB0","PLSOI0","SH1GH0"})
        check(doomfluid::spriteTranslation(name)==2,"weapon frame lost metal-marker translation");
    for(const char* name:{"PLSOA0","PLSOB0","PL1FA0","BFGMA0"})
        check(doomfluid::spriteTranslation(name)==0,"muzzle artwork incorrectly recolored as gloves");
    for(int color=0;color<=7;++color){auto t=doomfluid::gloveTranslation(palette.data(),color,true);check(t[198]==9&&t[201]==12&&t[175]==152&&t[181]==159&&t[202]==202,"glove choice changed gun material or firing blue");}
    auto black=doomfluid::gloveTranslation(palette.data(),1,false);
    check(black[112]==40&&black[127]==0&&black[198]==198,"black glove gradient");
    for(int w:{1,2,4,5,6,8}) {
        doomfluid::Weapons controller;controller.reset(w);controller.shot(w,1);
        unsigned down=0,up=0;std::set<std::string> frames;
        for(int tick=0;tick<90;++tick){auto layers=controller.layers(.5f);check(!layers.empty(),"missing weapon");for(const auto&layer:layers){check(doomweapon::valid(layer.transform),"invalid transform");frames.insert(layer.sprite);}for(auto&sound:controller.takeSounds()){if(sound=="ShotgunPumpDown")++down;if(sound=="ShotgunPumpUp")++up;}controller.tick(w);}
        auto settled=controller.layers(1);check(settled.size()==(w==5||w==6?2:1)&&settled[0].transform.scaleX==1&&settled[0].transform.x==0,"animation never settled");
        if(w==1)check(frames.count("P1SGJ0")&&frames.count("P1SGK0")&&frames.count("P1SFC0"),"pistol frames skipped");
        if(w==2)check(down==1&&up==1&&frames.count("SH1GH0"),"pump sequence or sound events missing");
        controller.reset(1);check(controller.layers(1).size()==1,"switch leaked an old flash");
    }
    for(int w:{0,3,7}){
        doomfluid::Weapons controller;controller.reset(w);
        for(int tic=0;tic<500;++tic){
            if(tic<250&&tic%8==0){controller.attack(w);if(w==3)controller.shot(w);}
            for(bool bare:{false,true})for(const auto&layer:controller.layers(.5f,bare))check(doomweapon::valid(layer.transform),"held fire generated invalid overlay");
            controller.tick(w);
        }
        if(w==0)check(controller.layers(1,false)[0].sprite=="PUN1A0"&&controller.layers(1,true)[0].sprite=="PUN3A0","fist glove choice lost");
    }
    doomfluid::Weapons bfg;bfg.reset(6);bfg.attack(6);
    doomfluid::Weapons chaingun;chaingun.reset(3);chaingun.shot(3);
    check(chaingun.layers(1).front().sprite=="CH1FA0","chaingun flash covers barrel geometry");
    // At every spin angle the tip's top-center must stay on its orbit;
    // scaling around the bottom moves outer tips behind the opaque disk.
    for(int tic=0;tic<80;++tic){
        if(tic%8==0)chaingun.shot(3);
        for(float fraction:{0.f,.5f,1.f}){
            int tips=0;
            for(const auto& layer:chaingun.layers(fraction)){
                if(layer.sprite.rfind("CH1M",0)!=0&&layer.sprite.rfind("CH2M",0)!=0)continue;
                ++tips;auto t=layer.transform;t.pivotX*=114;t.pivotY*=83;
                auto top=doomweapon::point(t,57,0);
                check(std::abs(top[0]-(t.x+57))<.001f&&std::abs(top[1]-t.y)<.001f,
                    "rotating barrel tip sinks behind the disk");
            }
            check(tips==6,"chaingun lost a rotating barrel tip");
        }
        chaingun.tick(3);
    }
    for(int w:{1,3,4,8}){
        doomfluid::Weapons c;c.reset(w);c.shot(w);bool casing=false,expansion=false;
        for(int tic=0;tic<90;++tic){
            auto layers=c.layers(1);
            for(const auto& l:layers){casing|=l.sprite==(w==8?"SHTCA0":"PISCA0");expansion|=l.sprite=="MI1FE0";}
            if(w==3&&tic==5)check(layers.front().sprite=="CH1FB0"&&layers.front().transform.scaleX<.6f,"chaingun shrinking flash missing");
            if(w==4&&tic==3)check(layers.front().sprite=="MI1FD0","rocket expansion must be behind launcher");
            c.tick(w);
        }
        check(w==4?expansion:casing,"firing casing or rocket expansion missing");
    }
    {
        doomfluid::Weapons rocket;rocket.reset(4);rocket.shot(4);
        rocket.tick(4);auto flash=rocket.layers(1).back().transform;
        for(int tic=2;tic<10;++tic){
            rocket.tick(4);auto layers=rocket.layers(1);const auto &fire=layers.front();
            check(fire.sprite.rfind("MI1F",0)==0,"rocket fire expansion missing");
            check(std::abs(fire.transform.x-flash.x)<.001f&&std::abs(fire.transform.y-flash.y)<.001f,
                  "rocket fire followed launcher recoil instead of source flash position");
            check(fire.transform.rotation==0,"rocket fire inherited launcher rotation");
        }
    }
    for(bool reload:{false,true}){
        doomfluid::Weapons c;c.reset(8);int load=0,close=0;bool shells=false,catchHand=false,split=false;int catchTick=-1;
        for(int tic=0;tic<150;++tic){
            c.input(!reload&&tic<60,reload&&tic<60);c.tick(8);
            auto layers=c.layers(1);
            if(!split&&layers[0].sprite=="SH12J0"){
                split=true;catchTick=tic;
                check(layers[0].transform.x==45&&layers[0].transform.y==45&&layers[0].transform.rotation==50,"SSG split pose changed");
            }
            if(split&&tic==catchTick+1)check(layers[0].sprite=="SH12J0"&&layers[0].transform.x==25&&layers[0].transform.y==25&&layers[0].transform.rotation==35,"SSG fidget took normal reload steps before catching");
            if(split&&tic==catchTick+2)check(layers[0].sprite=="SH12J0"&&layers[0].transform.x==10&&layers[0].transform.rotation==25,"SSG loaded breech vanished before shell ejection");
            if(split&&tic==catchTick+3)check(layers[0].sprite=="SH12F0"&&layers[0].transform.rotation==16,"SSG catch failed to switch to empty breech");
            for(const auto& l:c.layers(1)){shells|=l.sprite=="SHTCA0";catchHand|=l.sprite=="SH12H0"&&l.transform.x<-50;}
            for(const auto& s:c.takeSounds()){load+=s=="SSGLoad";close+=s=="SSGClose";}
        }
        check(split&&shells&&catchHand&&load==1&&close==1,"SSG catch fidget or sound sequence missing");
        check(c.layers(1).size()==1&&c.layers(1)[0].sprite=="SH12A0","SSG catch did not settle");
    }
    for(bool reload:{false,true}){
        doomfluid::Weapons shotgun;shotgun.reset(2);bool caught=false;int down=0,up=0,catchSound=0;
        for(int tic=0;tic<160;++tic){
            shotgun.input(!reload&&tic<90,reload&&tic<90);shotgun.tick(2);
            auto layers=shotgun.layers(1);
            if(tic==80)check(layers[0].sprite=="SH1GH0","held shotgun pump closed before release");
            for(const auto& layer:layers)if(layer.sprite=="MRD1J0")caught=true;
            for(const auto& s:shotgun.takeSounds()){down+=s=="ShotgunPumpDown";up+=s=="ShotgunPumpUp";catchSound+=s=="ShotgunCatch";}
            if(tic==80)check(down==1&&up==0,"held pump repeats or closes sound early");
        }
        check(caught&&catchSound==1&&down==1&&up==1,"shotgun catch or release sequence missing");
        check(shotgun.layers(1)[0].sprite=="SH1GA0","shotgun failed to return after release");
    }
    check(bfg.layers(1)[0].sprite=="BFGMA0"&&bfg.layers(1)[1].sprite=="BFG1A0","BFG rear barrel obscures front casing");
    doomfluid::Weapons plasma;plasma.reset(5);
    check(plasma.layers(1)[0].sprite=="PLSOA0"&&plasma.layers(1)[1].sprite=="PL1GA0","plasma rear barrel obscures front casing");
    for(int tic=0;tic<45;++tic){for(const auto&layer:bfg.layers(1))check(layer.sprite.rfind("BF1F",0)!=0,"BFG fired before gameplay projectile");bfg.tick(6);}
    bfg.shot(6);check(bfg.layers(1).front().sprite=="BF1FA0","BFG release missing");
    for(int weapon=0;weapon<9;++weapon){
        doomfluid::Weapons controller;controller.reset(weapon);std::set<std::string> frames;
        for(int tic=0;tic<400;++tic){
            controller.input(tic<40,tic>=200&&tic<210);
            controller.tick(weapon);
            for(const auto&layer:controller.layers(.5f)){check(doomweapon::valid(layer.transform),"fidget generated invalid transform");frames.insert(layer.sprite);}
        }
        if(weapon==1)check(frames.count("P1SHA0")&&frames.count("P2SGA0")&&frames.count("MRD1H0")&&frames.count("PISCD0"),"pistol slide rack, bullet catch, or grip toggle absent");
        if(weapon==2)check(frames.count("SH1GH0")&&frames.count("SHTCA0"),"manual shotgun pump or ejected shell absent");
        if(weapon==8)check(frames.count("SH12G0"),"manual SSG reload absent");
        if(weapon>=3&&weapon<=6)check(frames.count("MRD1G0"),"inspection hand absent");
        if(weapon==7)check(frames.count("SAW1Y0")&&frames.count("SAW1Z0"),"chainsaw starter pull absent");
        controller.reset(weapon);check(controller.ambientSound()[0]==0,"switch/reset leaked fidget loop");
    }
    for(int weapon=0;weapon<9;++weapon){
        doomfluid::Weapons controller,stationary;controller.reset(weapon);stationary.reset(weapon);
        bool moved=false;
        for(int tic=0;tic<600;++tic){
            const bool active=tic<100;
            controller.movement(active?800.f:0,active?.5f:0,active?8.f:0,active?-4.f:0,active,true,tic);
            controller.tick(weapon);
            stationary.tick(weapon);
            const auto baseline=stationary.layers(1)[0].transform;
            for(const auto& layer:controller.layers(.5f))check(doomweapon::valid(layer.transform),"movement produced invalid transform");
            const auto pose=controller.layers(1)[0].transform;
            if(active&&std::abs(pose.x-baseline.x)>.1f)moved=true;
            if(tic==599)check(std::abs(pose.x-baseline.x)<.01f&&std::abs(pose.y-baseline.y)<.1f,"sway or landing bounce failed to return to rest");
        }
        check(moved,"walking and turning did not move weapon");
    }
    for(float pitch:{-1.55334f,1.55334f}){
        doomfluid::Weapons saw,baseline;saw.reset(7);baseline.reset(7);
        for(int tic=0;tic<150;++tic){saw.movement(0,pitch,0,0,false,false,tic);saw.tick(7);baseline.tick(7);}
        check(std::abs(saw.layers(1)[0].transform.y-baseline.layers(1)[0].transform.y)<3.91f,"chainsaw pitch sway exposes bottom edge");
    }
    for(int held:{1,40}){
        doomfluid::Weapons fists;fists.reset(0);int impacts=0;
        for(int tic=0;tic<70;++tic){
            fists.input(tic<held,false);fists.tick(0);
            auto layers=fists.layers(.5f);
            if(tic==2)check(layers.size()==2&&layers[0].sprite=="PUN3E0"&&layers[1].sprite=="PUN3H0"&&layers[0].flipX,"fist bump layer order or handedness incorrect");
            for(const auto&s:fists.takeSounds())if(s=="FistPuch")++impacts;
            for(const auto&layer:layers)check(doomweapon::valid(layer.transform),"invalid fist fidget transform");
        }
        check(impacts==(held==1?1:2),"fist tap/hold impact sequence incorrect");
        check(fists.layers(1).size()==1&&fists.layers(1)[0].sprite=="PUN3A0","fists did not return to ready");
    }
    {doomfluid::Weapons p;p.reset(5);for(int tic=0;tic<60;++tic){if(tic%3==0)p.shot(5);p.tick(5);for(const auto&layer:p.layers(.5f))check(layer.sprite.substr(0,4)!="PLSR","plasma entered recovery between continuous shots");}}
    {doomfluid::Weapons s;s.reset(2);bool spawned=false;float x=0,y=0;
        for(int tic=0;tic<20&&!spawned;++tic){s.input(true,false);s.tick(2);for(const auto&l:s.layers(1))if(l.sprite=="SHTCA0"){spawned=true;x=l.transform.x;y=l.transform.y;}}
        check(spawned,"shotgun shell not spawned");s.tick(2);
        for(const auto&l:s.layers(1))if(l.sprite=="SHTCA0")check(l.transform.x==x&&l.transform.y==y,"shell moved before its first source movement tick");
        s.tick(2);for(const auto&l:s.layers(1))if(l.sprite=="SHTCA0")check(l.transform.x>x&&l.transform.y<y,"shell did not start moving on its source tick");
    }
    {doomfluid::Weapons c;c.reset(3);c.shot(3);float last=c.ambientLevel();for(int tic=0;tic<60;++tic){c.tick(3);if(tic>5)check(c.ambientLevel()<=last,"chaingun spin volume increased after release");last=c.ambientLevel();}check(c.ambientLevel()<.025f,"chaingun spin sound did not fade to silence");}
    {doomfluid::Weapons c;c.reset(3);
        // Alt starts the idle spin. Primary can interrupt it without releasing alt.
        for(int tic=0;tic<20;++tic){c.input(true,false,true);c.tick(3);}
        check(c.ambientLevel()>.30f,"held alt did not spin idle chaingun");
        for(int burst=0;burst<3;++burst){
            for(int tic=0;tic<12;++tic){c.input(true,false,false);if(tic%4==0)c.shot(3);c.tick(3);}
            check(c.ambientLevel()>.95f,"chaingun did not reach firing speed");
            for(int tic=0;tic<90;++tic){c.input(true,false,true);c.tick(3);check(c.ambientLevel()>.95f,"held alt slowed or reversed chaingun after firing");}
            c.shot(3);check(c.layers(1).front().sprite=="CH1FA0","primary did not resume firing while alt held");
        }
        c.input(false,false,true);for(int tic=0;tic<70;++tic)c.tick(3);
        check(c.ambientLevel()<.025f,"chaingun failed to stop after both buttons released");
        c.input(true,false,true);c.tick(3);c.reset(1);
        check(std::string(c.ambientSound()).empty(),"chaingun spin persisted after switching weapons");
    }
    if(std::getenv("DOOM_TEST_HAND_TRACE")){doomfluid::Weapons s;s.reset(2);for(int t=0;t<30;++t){s.input(true,false);s.tick(2);for(const auto&l:s.layers(1))if(l.sprite=="MRD1I0"||l.sprite=="SHTCA0")std::cout<<t<<" "<<l.sprite<<" "<<l.transform.x<<" "<<l.transform.y<<" "<<l.transform.rotation<<"\n";}}
    std::cout<<"Fluid animation, movement, shell and glove translation tests passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}

