/* LAN transport for Doom's existing lockstep/retransmission protocol.
 * The launcher supplies a validated roster and a per-lobby session token.
 * This protocol intentionally requires the exact same build and content. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define boolean win_boolean
#include <winsock2.h>
#include <ws2tcpip.h>
#undef boolean
#undef MAXINT
#undef MININT
#undef MAXLONG
#undef MINLONG
typedef SOCKET lan_socket;
#define LAN_BAD INVALID_SOCKET
#define lan_close closesocket
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
typedef int lan_socket;
#define LAN_BAD (-1)
#define lan_close close
#endif
#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "i_net.h"
#include "m_argv.h"

static lan_socket sock=LAN_BAD;
static struct sockaddr_in peers[MAXPLAYERS];
static char token[33];
static int lastPacket[MAXPLAYERS];
static int started;
static int leftGame[MAXPLAYERS];
static int expectedPlayer[MAXPLAYERS];
static void shutdownLan(void){if(sock!=LAN_BAD){lan_close(sock);sock=LAN_BAD;}}
#include "lan_lobby.h"
static int number(const char *s,int lo,int hi){char *end;long n=strtol(s,&end,10);if(!*s||*end||n<lo||n>hi)I_Error("Invalid LAN number: %s",s);return (int)n;}
static void endpoint(const char *text,struct sockaddr_in *a){
 char host[64];const char *colon=strrchr(text,':');size_t len=colon?(size_t)(colon-text):strlen(text);
 if(!colon||len==0||len>=sizeof(host))I_Error("Invalid LAN endpoint: %s",text);
 memcpy(host,text,len);host[len]=0;memset(a,0,sizeof(*a));a->sin_family=AF_INET;
 a->sin_port=htons((unsigned short)number(colon+1,1024,65535));
 if(inet_pton(AF_INET,host,&a->sin_addr)!=1)I_Error("LAN needs an IPv4 address: %s",host);
}
void I_InitNetwork(void){
 int p=M_CheckParm("-net"),i,t,port;struct sockaddr_in local;
 doomcom=calloc(1,sizeof(*doomcom));if(!doomcom)I_Error("Could not allocate network state");
 doomcom->id=DOOMCOM_ID;doomcom->ticdup=1;doomcom->numplayers=doomcom->numnodes=1;netgame=false;
 if(!p)return;
 t=M_CheckParm("-lan-token");if(!t||t+1>=myargc||strlen(myargv[t+1])!=32)I_Error("Start LAN games from the Multiplayer menu");
 memcpy(token,myargv[t+1],32);
 t=M_CheckParm("-port");if(!t||t+1>=myargc)I_Error("Missing LAN port");port=number(myargv[t+1],1024,65535);
 if(p+2>=myargc)I_Error("Missing LAN player roster");
 doomcom->consoleplayer=(short)(number(myargv[p+1],1,MAXPLAYERS)-1);
 for(i=p+2;i<myargc&&myargv[i][0]!='-';++i){int node=doomcom->numnodes;if(node>=MAXPLAYERS)I_Error("LAN supports up to four players");endpoint(myargv[i],&peers[node]);doomcom->numnodes++;}
 if(doomcom->numnodes<2||doomcom->consoleplayer>=doomcom->numnodes)I_Error("Invalid LAN roster");
#ifdef _WIN32
 {WSADATA data;u_long nonblocking=1;if(WSAStartup(MAKEWORD(2,2),&data))I_Error("Could not initialize Windows networking");sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(sock!=LAN_BAD)ioctlsocket(sock,FIONBIO,&nonblocking);}
#else
 sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(sock!=LAN_BAD)fcntl(sock,F_SETFL,O_NONBLOCK);
#endif
 if(sock==LAN_BAD)I_Error("Could not create LAN socket");
 memset(&local,0,sizeof(local));local.sin_family=AF_INET;local.sin_port=htons((unsigned short)port);local.sin_addr.s_addr=htonl(INADDR_ANY);
 if(bind(sock,(struct sockaddr*)&local,sizeof(local)))I_Error("LAN port %d is busy. Close the other game and try again.",port);
 for(i=0,t=1;i<doomcom->numnodes;i++)if(i!=doomcom->consoleplayer)expectedPlayer[t++]=i;
 atexit(shutdownLan);doomcom->numplayers=doomcom->numnodes;doomcom->extratics=1;netgame=true;
 started=I_GetTime();for(i=1;i<doomcom->numnodes;i++)lastPacket[i]=started;
 printf("LAN: player %d, %d players, UDP port %d\n",doomcom->consoleplayer+1,doomcom->numplayers,port);
}
void I_NetCmd(void){
 unsigned char wire[36+sizeof(doomdata_t)];int i,n;struct sockaddr_in from;
#ifdef _WIN32
 int size=sizeof(from);
#else
 socklen_t size=sizeof(from);
#endif
 I_LanPoll();
 if(sock==LAN_BAD)I_Error("LAN is not connected");
 if(doomcom->command==CMD_SEND){
  int node=doomcom->remotenode;if(node<=0||node>=doomcom->numnodes)I_Error("Invalid LAN destination");
  if(doomcom->datalength<8||doomcom->datalength>(int)sizeof(doomdata_t))I_Error("Invalid LAN packet length");
  memcpy(wire,"TDL1",4);memcpy(wire+4,token,32);memcpy(wire+36,&doomcom->data,doomcom->datalength);
  sendto(sock,(const char*)wire,36+doomcom->datalength,0,(struct sockaddr*)&peers[node],sizeof(peers[node]));return;
 }
 if(doomcom->command!=CMD_GET)I_Error("Invalid LAN command");
 doomcom->remotenode=-1;
 /* Bound work per poll, even when unrelated datagrams arrive continuously. */
 for(int attempt=0;attempt<32;++attempt){
  size=sizeof(from);n=recvfrom(sock,(char*)wire,sizeof(wire),0,(struct sockaddr*)&from,&size);if(n<0)break;
  if(n<44||memcmp(wire,"TDL1",4)||memcmp(wire+4,token,32))continue;
  for(i=1;i<doomcom->numnodes;i++)if(from.sin_addr.s_addr==peers[i].sin_addr.s_addr&&from.sin_port==peers[i].sin_port)break;
  if(i==doomcom->numnodes)continue;
  {unsigned count=wire[43];if(count>BACKUPTICS||n!=36+8+(int)count*(int)sizeof(ticcmd_t))continue;}
  memset(&doomcom->data,0,sizeof(doomcom->data));memcpy(&doomcom->data,wire+36,n-36);
  if(!(doomcom->data.checksum&0x20000000u)&&doomcom->data.player!=expectedPlayer[i])continue;
  if(doomcom->data.checksum&0x80000000u)leftGame[i]=1;
  lastPacket[i]=I_GetTime();doomcom->remotenode=(short)i;doomcom->datalength=(short)(n-36);return;
 }
 for(i=1;i<doomcom->numnodes;i++)if(!leftGame[i]&&I_GetTime()-lastPacket[i]>45*TICRATE)I_Error("LAN connection lost (no packets for 45 seconds). Return to the launcher and reconnect.");
}
