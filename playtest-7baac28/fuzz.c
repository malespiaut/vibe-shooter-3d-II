#include <stdio.h>
#include <stdlib.h>
#include "bast.h"
#include "game/state.h"
int main(int argc,char**argv){
  bast_map*m=bast_map_load(argv[1]); if(!m){puts("load fail");return 1;}
  static game_state gs; game_init(&gs,m,1,0);
  srand(atoi(argv[2]));
  long bad=0,stuck=0; fix_t maxsp=0; int visited[64*64]={0};
  ticcmd c={0};int hold=0;
  printf("start %.3f %.3f %dx%d\n",gs.player.x/65536.0,gs.player.y/65536.0,m->width,m->height);
  for(long t=0;t<400000;t++){
    if(hold--<=0){hold=rand()%60;c.fwd=(rand()%3-1)*127;c.side=(rand()%3-1)*127;
      c.turn=(rand()%5-2)*(rand()%2000);c.buttons=rand()%2?BT_RUN:0;}
    game_tic(&gs,&c);
    player_t*p=&gs.player;
    if(game_circle_blocked(m,p->x,p->y,PLAYER_RADIUS)){if(bad<5)printf("OVERLAP tic %ld at %.4f %.4f\n",t,p->x/65536.0,p->y/65536.0);bad++;}
    fix_t sp=fix_dist(p->momx,p->momy); if(sp>maxsp)maxsp=sp;
    visited[(p->y>>16)*m->width+(p->x>>16)]=1;
    if(t%997==0){ /* escape check: can the player move in any of 16 directions? */
      int ok=0; for(int d=0;d<16&&!ok;d++){game_state g2=gs;g2.player.angle=(angle_t)(d*4096);g2.player.momx=g2.player.momy=0;
        ticcmd f={127,0,0,0}; for(int k=0;k<8;k++)game_tic(&g2,&f);
        if(g2.player.x!=gs.player.x||g2.player.y!=gs.player.y)ok=1;}
      if(!ok){stuck++;printf("STUCK tic %ld at %.4f %.4f\n",t,p->x/65536.0,p->y/65536.0);}
    }
  }
  int open=0,vis=0; for(int y=0;y<m->height;y++)for(int x=0;x<m->width;x++){if(!game_tile_blocks(m,x,y))open++; vis+=visited[y*m->width+x];}
  printf("overlaps=%ld stuck=%ld maxspeed=%.3f u/s visited=%d/%d open tiles\n",bad,stuck,maxsp/65536.0*35,vis,open);
}
