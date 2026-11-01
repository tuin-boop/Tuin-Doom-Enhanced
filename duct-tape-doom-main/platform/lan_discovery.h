/* Fixed-size, nonblocking discovery. UDP advertises only; TCP validates content. */
#include <time.h>
lan_game_t lan_games[16];
static uint64_t discoveryId;
int lan_game_count;
static lan_socket discovery=LAN_BAD;
static int discoveryHost,discoveryScan;
static void discoveryClose(void){if(discovery!=LAN_BAD)lan_close(discovery);discovery=LAN_BAD;lan_game_count=0;}
static int discoveryOpen(int host){
    discoveryClose();
#ifdef _WIN32
    if(!networkStarted){WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w))return 0;networkStarted=1;atexit(I_LanClose);}
#endif
    discovery=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(discovery==LAN_BAD)return 0;
#ifdef _WIN32
    u_long nb=1;ioctlsocket(discovery,FIONBIO,&nb);
#else
    fcntl(discovery,F_SETFL,O_NONBLOCK);
#endif
    int yes=1;setsockopt(discovery,SOL_SOCKET,SO_BROADCAST,(const char*)&yes,sizeof(yes));
    struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(host?5032:0);
    if(bind(discovery,(struct sockaddr*)&a,sizeof(a))){discoveryClose();return 0;}
    discoveryHost=host;discoveryScan=-TICRATE;if(host)discoveryId=((uint64_t)time(NULL)<<32)^(uint32_t)clock();return 1;
}
void I_LanBrowse(void){
    if(lan_lobby||lan_session)return;
    if(discoveryOpen(0))strcpy(lan_status,"SEARCHING LAN - R TO REFRESH");
    else strcpy(lan_status,"DISCOVERY UNAVAILABLE - USE JOIN BY IP");
}
static void discoveryPoll(void){
    if(discovery==LAN_BAD)return;
    int now=I_GetTime();
    if(!discoveryHost&&now-discoveryScan>=TICRATE){
        discoveryScan=now;struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(5032);
        a.sin_addr.s_addr=htonl(INADDR_BROADCAST);sendto(discovery,"TUINLAN?",8,0,(struct sockaddr*)&a,sizeof(a));
        a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);sendto(discovery,"TUINLAN?",8,0,(struct sockaddr*)&a,sizeof(a));
    }
    for(int i=0;i<16;i++){
        unsigned char b[128];struct sockaddr_in a;
#ifdef _WIN32
        int len=sizeof(a);
#else
        socklen_t len=sizeof(a);
#endif
        int n=recvfrom(discovery,(char*)b,sizeof(b),0,(struct sockaddr*)&a,&len);if(n<0)break;
        if(discoveryHost&&n==8&&!memcmp(b,"TUINLAN?",8)){
            memset(b,0,sizeof(b));memcpy(b,"TUINLAN2",8);
            b[8]=(unsigned char)lan_count;b[9]=lan_session!=0;b[10]=(unsigned char)(lan_session?gameepisode:lan_episode);
            b[11]=(unsigned char)(lan_session?gamemap:lan_map);b[12]=(unsigned char)lan_skill;b[13]=(unsigned char)lan_mode;
            b[14]=historyOverflow!=0;gethostname((char*)b+16,31);memcpy(b+48,lanSignature,64);memcpy(b+112,&discoveryId,8);
            sendto(discovery,(const char*)b,120,0,(struct sockaddr*)&a,sizeof(a));
        }else if(!discoveryHost&&n==120&&!memcmp(b,"TUINLAN2",8)&&b[8]<=4&&b[12]<=4&&b[13]<=2){
            uint64_t id;memcpy(&id,b+112,8);char ip[16];inet_ntop(AF_INET,&a.sin_addr,ip,sizeof(ip));int slot;
            for(slot=0;slot<lan_game_count;slot++)if(lan_games[slot].id==id)break;
            if(slot==16)continue;if(slot==lan_game_count)lan_game_count++;
            lan_game_t *g=&lan_games[slot];
            if(g->id==id&&strncmp(g->address,"127.",4)&&!strncmp(ip,"127.",4)){g->seen=now;continue;}
            memset(g,0,sizeof(*g));g->id=id;strcpy(g->address,ip);
            for(int c=0;c<31&&b[16+c];c++)g->name[c]=(b[16+c]>=32&&b[16+c]<127)?b[16+c]:'?';
            const char *sig=getenv("TUIN_LAN_SIGNATURE");g->compatible=sig&&strlen(sig)==64&&!memcmp(sig,b+48,64)&&!b[14];
            g->players=b[8];g->running=b[9]!=0;g->episode=b[10];g->map=b[11];g->skill=b[12];g->mode=b[13];g->seen=now;
        }
    }
    if(!discoveryHost)for(int i=lan_game_count-1;i>=0;i--)if(now-lan_games[i].seen>5*TICRATE){
        memmove(&lan_games[i],&lan_games[i+1],(lan_game_count-i-1)*sizeof(lan_game_t));lan_game_count--;
    }
}
