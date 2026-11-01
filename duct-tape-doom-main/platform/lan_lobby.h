/* In-game LAN: a host sequences inputs on a reliable TCP stream. A late
 * arrival replays that bounded stream, checks state hashes, then joins at
 * an explicit frame boundary. No native save structures cross the network. */
#include "i_lan.h"
#include "p_fluid.h"
#include "g_game.h"
#include "r_main.h"
#include <math.h>
extern float tourpitch;extern boolean freeaim;
extern int p_fluidfastpistol;
int lan_lobby,lan_host,lan_count,lan_pending,lan_session,lan_catchingup,lan_slot;
int lan_episode=1,lan_map=1,lan_skill=3,lan_mode;
int lan_friendlyfire=0,lan_color=0,lan_wheelweapon=0;
uint32_t lan_wheelserial;
int16_t lan_pitch[4];uint8_t lan_colors[4];
char lan_status[80]="HOST OR JOIN A LAN GAME";
#define LAN_BUFFER 32768
/* About four hours of commands, under 48 MB. Existing games continue
 * after the limit, but further late joins are refused explicitly. */
#define LAN_HISTORY_MAX 500000
typedef struct {
    lan_socket fd;
    unsigned char in[LAN_BUFFER],out[LAN_BUFFER];
    int inSize,outSize,hello,ready,init,next,last,connected;
    lan_input_t input;
} lan_peer_t;
static lan_peer_t connections[4];
static lan_socket listener=LAN_BAD;
static lan_frame_t *history;
static int historySize,historyCapacity,historyOverflow,lanMask,lanLastReady;
static char lanSignature[65];
static int networkStarted,heartbeat;
#include "lan_discovery.h"
static void peerClose(lan_peer_t *p){if(p->connected)lan_close(p->fd);memset(p,0,sizeof(*p));p->fd=LAN_BAD;}
static int socketPending(void){
#ifdef _WIN32
    int e=WSAGetLastError();return e==WSAEWOULDBLOCK||e==WSAEINPROGRESS||e==WSAEALREADY||e==WSAENOTCONN;
#else
    return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINPROGRESS||errno==ENOTCONN;
#endif
}
static lan_socket tcpSocket(void){
    lan_socket s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==LAN_BAD)return s;
#ifdef _WIN32
    u_long nb=1;ioctlsocket(s,FIONBIO,&nb);
#else
    fcntl(s,F_SETFL,O_NONBLOCK);
#endif
    int yes=1;setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&yes,sizeof(yes));return s;
}
static void peerInit(lan_peer_t *p,lan_socket fd){memset(p,0,sizeof(*p));p->fd=fd;p->connected=1;p->last=I_GetTime();}
static int queuePacket(lan_peer_t *p,int kind,const void *data,unsigned n){
    if(!p->connected||n>240||p->outSize+(int)n+3>LAN_BUFFER)return 0;
    unsigned char *b=p->out+p->outSize;b[0]=(unsigned char)kind;b[1]=(unsigned char)n;b[2]=(unsigned char)(n>>8);
    if(n)memcpy(b+3,data,n);p->outSize+=(int)n+3;return 1;
}
static int appendFrame(const lan_frame_t *frame){
    if(historySize>=LAN_HISTORY_MAX){historyOverflow=1;history[historySize++%LAN_HISTORY_MAX]=*frame;return 1;}
    if(historySize==historyCapacity){
        int cap=historyCapacity?historyCapacity*2:1024;if(cap>LAN_HISTORY_MAX)cap=LAN_HISTORY_MAX;
        void *mem=realloc(history,(size_t)cap*sizeof(*history));if(!mem){historyOverflow=1;return 0;}
        history=mem;historyCapacity=cap;
    }
    history[historySize++]=*frame;return 1;
}
void I_LanClose(void){
    discoveryClose();
    if(listener!=LAN_BAD){lan_close(listener);listener=LAN_BAD;}
    for(int i=0;i<4;i++)peerClose(&connections[i]);
    free(history);history=NULL;historySize=historyCapacity=historyOverflow=0;
    lan_lobby=lan_pending=lan_count=lan_session=lan_catchingup=0;lanMask=0;
}
int I_LanOpen(const char *address){
    const char *sig=getenv("TUIN_LAN_SIGNATURE");
    if(netgame){strcpy(lan_status,"ALREADY IN A NETWORK GAME");return 0;}
    I_LanClose();
    if(!sig||strlen(sig)!=64){strcpy(lan_status,"START FROM THE UPDATED LAUNCHER FOR LAN");return 0;}
    if(!networkStarted){
#ifdef _WIN32
        WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w)){strcpy(lan_status,"NETWORK INITIALIZATION FAILED");return 0;}
#endif
        networkStarted=1;atexit(I_LanClose);
    }
    memcpy(lanSignature,sig,65);lan_host=!address;lan_slot=0;lanLastReady=-1;
    struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(5031);
    if(address&&inet_pton(AF_INET,address,&a.sin_addr)!=1){strcpy(lan_status,"ENTER THE HOST IPV4 ADDRESS");return 0;}
    lan_socket fd=tcpSocket();if(fd==LAN_BAD){strcpy(lan_status,"COULD NOT CREATE LAN SOCKET");return 0;}
    if(lan_host){
        if(bind(fd,(struct sockaddr*)&a,sizeof(a))||listen(fd,4)){lan_close(fd);strcpy(lan_status,"LAN PORT 5031 IS BUSY");return 0;}
        listener=fd;discoveryOpen(1);lan_count=1;strcpy(lan_status,"READY - YOU CAN START WITHOUT GUESTS");
    }else{
        if(connect(fd,(struct sockaddr*)&a,sizeof(a))&& !socketPending()){lan_close(fd);strcpy(lan_status,"COULD NOT CONNECT TO HOST");return 0;}
        peerInit(&connections[0],fd);queuePacket(&connections[0],'H',lanSignature,64);strcpy(lan_status,"CONNECTING TO HOST...");
    }
    lan_lobby=1;return 1;
}
static void sendInit(lan_peer_t *p,int slot){
    unsigned char b[9]={(unsigned char)slot,(unsigned char)lan_episode,(unsigned char)lan_map,(unsigned char)lan_skill,(unsigned char)lan_mode,
        (unsigned char)p_netfluidweapons,(unsigned char)p_netfluidfastpistol,3,(unsigned char)lan_friendlyfire};
    queuePacket(p,'S',b,sizeof(b));p->init=1;p->next=0;
}
void I_LanStart(void){
    if(!lan_lobby||!lan_host||lan_session||lan_pending)return;
    if(!G_MapExists(lan_episode,lan_map)){strcpy(lan_status,"MAP NOT AVAILABLE");return;}
    p_netfluidweapons=p_fluidweapons&&M_CheckParm("-fluidweapons");p_netfluidfastpistol=p_fluidfastpistol;
    lan_pending=1;lanMask=1;strcpy(lan_status,"HOSTING - PLAYERS CAN JOIN NOW");
    for(int i=1;i<4;i++)if(connections[i].hello)sendInit(&connections[i],i);
}
static void processPacket(lan_peer_t *p,int slot,int kind,const unsigned char *b,int n){
    if(lan_host){
        if(kind=='H'&&!p->hello&&n==64){
            if(memcmp(b,lanSignature,64)||historyOverflow){
                const char *why=historyOverflow?"MATCH HISTORY LIMIT - START A NEW MATCH":"BUILD OR GAME FILES DO NOT MATCH";
                queuePacket(p,'E',why,(unsigned)strlen(why));p->hello=-1;return;
            }
            p->hello=1;lan_count++;unsigned char number=(unsigned char)slot;queuePacket(p,'W',&number,1);
            if(lan_session||lan_pending)sendInit(p,slot);return;
        }
        if(p->hello!=1)return;
        if(kind=='C'&&n==sizeof(lan_input_t)&&p->ready){lan_input_t input;memcpy(&input,b,n);if(input.color>3||input.weapon>=NUMWEAPONS||(input.pitch!=32767&&(input.pitch < -15500||input.pitch > 15500)))return;p->input=input;p->input.cmd.consistancy=0;return;}
        if(kind=='R'&&n==4&&p->init){uint32_t tic;memcpy(&tic,b,4);if(tic<=(unsigned)historySize&&tic+TICRATE>=(unsigned)historySize){p->ready=1;lanMask|=1<<slot;}return;}
    }else{
        if(kind=='E'&&n<sizeof(lan_status)){memcpy(lan_status,b,n);lan_status[n]=0;peerClose(p);lan_lobby=0;return;}
        if(kind=='W'&&n==1&&b[0]>0&&b[0]<4){lan_slot=b[0];p->hello=1;strcpy(lan_status,"CONNECTED - WAITING FOR HOST");return;}
        if(kind=='S'&&n==9&&!p->init&&p->hello==1&&b[0]==lan_slot&&b[3]<=4&&b[4]<=2&&b[7]==3&&b[8]<=1&&G_MapExists(b[1],b[2])){
            lan_episode=b[1];lan_map=b[2];lan_skill=b[3];lan_mode=b[4];lan_friendlyfire=b[8];p_netfluidweapons=b[5]!=0;p_netfluidfastpistol=b[6]!=0;
            p->init=1;lan_pending=1;lan_catchingup=1;lan_count=1;strcpy(lan_status,"CATCHING UP WITH THE RUNNING MATCH...");return;
        }
        if(kind=='F'&&n==sizeof(lan_frame_t)&&p->init){lan_frame_t f;memcpy(&f,b,sizeof(f));if((f.mask&~15)||!(f.mask&1)||!appendFrame(&f))I_Error("LAN catch-up history unavailable");return;}
    }
    /* Unknown or malformed messages never modify simulation state. */
}
void I_LanPoll(void){
    discoveryPoll();
    if(!lan_lobby&&!lan_session&&!lan_pending)return;
    int now=I_GetTime();
    if(now-heartbeat>=TICRATE){heartbeat=now;for(int i=0;i<4;i++)if(connections[i].connected)queuePacket(&connections[i],'P',NULL,0);}
    if(lan_host&&listener!=LAN_BAD){
        lan_socket fd=accept(listener,NULL,NULL);
        if(fd!=LAN_BAD){
#ifdef _WIN32
            u_long nb=1;ioctlsocket(fd,FIONBIO,&nb);
#else
            fcntl(fd,F_SETFL,O_NONBLOCK);
#endif
            int yes=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,(const char*)&yes,sizeof(yes));
            int i;for(i=1;i<4;i++)if(!connections[i].connected)break;
            if(i==4)lan_close(fd);else peerInit(&connections[i],fd);
        }
    }
    for(int slot=lan_host?1:0;slot<(lan_host?4:1);slot++){
        lan_peer_t *p=&connections[slot];if(!p->connected)continue;
        int n=recv(p->fd,(char*)p->in+p->inSize,LAN_BUFFER-p->inSize,0);
        if(n>0){p->inSize+=n;p->last=now;}
        if(n==0||(n<0&&!socketPending())||now-p->last>60*TICRATE){
            int was=p->hello==1;peerClose(p);
            if(lan_host){if(was)lan_count--;lanMask&=~(1<<slot);continue;}
            if(lan_session)I_Error("LAN host disconnected. Rejoin from Multiplayer.");
            lan_lobby=0;strcpy(lan_status,"HOST UNAVAILABLE - CHECK IP AND FIREWALL");continue;
        }
        int used=0,messages=0;
        while(p->connected&&p->inSize-used>=3&&messages++<512){
            unsigned char *b=p->in+used;int size=b[1]+256*b[2];
            if(size>240){I_Error("Invalid LAN packet");break;}if(p->inSize-used<3+size)break;
            processPacket(p,slot,b[0],b+3,size);used+=3+size;
        }
        if(!p->connected)continue;
        if(used){p->inSize-=used;memmove(p->in,p->in+used,p->inSize);}
        if(lan_host&&p->hello==1&&p->init){
            for(int batch=0;batch<256&&p->next<historySize;batch++){
                if(historySize-p->next>LAN_HISTORY_MAX){peerClose(p);lanMask&=~(1<<slot);lan_count--;break;}
                if(!queuePacket(p,'F',&history[p->next%LAN_HISTORY_MAX],sizeof(lan_frame_t)))break;p->next++;
            }
        }
        if(p->outSize){int sent=send(p->fd,(const char*)p->out,p->outSize,0);if(sent>0){p->outSize-=sent;memmove(p->out,p->out+sent,p->outSize);}}
        if(p->hello==-1&&!p->outSize)peerClose(p);
    }
}
void I_LanActivate(void){
    memset(doomcom,0,sizeof(*doomcom));doomcom->id=DOOMCOM_ID;doomcom->ticdup=1;doomcom->numnodes=doomcom->numplayers=1;
    /* Catch-up follows player zero until this client's activation frame. */
    doomcom->consoleplayer=0;lan_session=1;lan_pending=lan_lobby=0;netgame=true;
}
int I_LanGetFrame(int tic,lan_frame_t *frame){if(tic<0||tic>=historySize)return 0;if(historySize-tic>LAN_HISTORY_MAX)I_Error("LAN client fell too far behind");*frame=history[tic%LAN_HISTORY_MAX];return 1;}
void I_LanInput(const ticcmd_t *cmd){
    lan_input_t input;memset(&input,0,sizeof(input));input.cmd=*cmd;
    input.pitch=freeaim&&isfinite(tourpitch)?(int16_t)(fmax(-1.55,fmin(1.55,tourpitch))*10000):32767;
    input.color=(uint8_t)(lan_color&3);input.weapon=(uint8_t)lan_wheelweapon;input.weaponserial=lan_wheelserial;
    if(lan_host)connections[0].input=input;
    else if(!lan_catchingup)queuePacket(&connections[0],'C',&input,sizeof(input));
}
unsigned I_LanMask(void){return (unsigned)lanMask;}
void I_LanHostCommands(ticcmd_t *cmds){for(int i=0;i<4;i++){cmds[i]=connections[i].input.cmd;cmds[i].consistancy=0;}}
void I_LanHostInputs(lan_input_t *inputs){for(int i=0;i<4;i++){inputs[i]=connections[i].input;inputs[i].cmd.consistancy=0;}}
void I_LanRecord(const lan_frame_t *frame){
    if(!appendFrame(frame))I_Error("LAN history allocation failed");
}
void I_LanCaughtUp(int tic){
    if(lan_host)return;
    /* Also serves as a heartbeat during long catch-ups and menu use. */
    if(tic!=lanLastReady&&(tic==historySize||tic%35==0)){uint32_t t=(uint32_t)tic;queuePacket(&connections[0],'R',&t,4);lanLastReady=tic;}
}
