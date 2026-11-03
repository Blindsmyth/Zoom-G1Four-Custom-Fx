#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
typedef uintptr_t uw;
void Fx_MOD_Chorus(uw*S,uw*ctx);
static uint32_t rs=12345; static uint32_t rnd(void){rs^=rs<<13;rs^=rs>>17;rs^=rs<<5;return rs;}
#define ARENA 8192
int main(void){
  long bad=0, blocks=0;
  for(int trial=0;trial<400;trial++){
    // exact-size heap objects so ASAN flags any out-of-bounds
    float *P=malloc(30*4); uint32_t *st=malloc(14*4); uint8_t *arena=malloc(ARENA);
    uw *dl=malloc(3*sizeof(uw)); uw *S=malloc(4*sizeof(uw)); float *blk=malloc(32*4);
    uint32_t *in=malloc(16*4); uint32_t *probe=malloc(4); uw *probeptr=malloc(sizeof(uw)); *probeptr=(uw)probe; uw *ctx=calloc(20,sizeof(uw));
    int mode=trial%4;   // 0 garbage everything, 1 sane params garbage arena, 2 no guard, 3 sane all
    for(int i=0;i<30;i++){ uint32_t r=rnd(); memcpy(&P[i],&r,4);}    // random bit patterns incl NaN/Inf
    for(int i=0;i<14;i++) st[i]=rnd();
    for(int i=0;i<ARENA;i++) arena[i]=rnd();
    if(mode>=1){ P[0]=1;P[2]=1;P[3]=1;P[5]=1;P[26]=1;P[28]=1;P[29]=(rnd()%101)/100.f;P[10]=.27f;P[11]=.27f;P[12]=.45f; ((int*)P)[15]=9739+rnd()%535653; ((int*)P)[22]=264+rnd()%159; }
    if(mode==2){P[28]=3.5f;}   /* init-not-run: defaults missing */
    dl[0]=(uw)arena; dl[1]=(uw)(arena+ARENA); dl[2]=ARENA;
    S[0]=0;S[1]=(uw)P;S[2]=(uw)st;S[3]=(uw)dl; ctx[1]=(uw)blk; ctx[7]=(uw)probeptr; ctx[8]=(uw)in;
    double ph=0;
    for(int b=0;b<600;b++){
      if(mode==0 && b==300){ for(int i=0;i<30;i++){uint32_t r=rnd();memcpy(&P[i],&r,4);} }   // params turn to garbage mid-run
      for(int i=0;i<16;i++){ float x=0.5f*sinf(ph); ph+=2*M_PI*440/44100; blk[i]=x; blk[16+i]=x; in[i]=rnd(); }
      Fx_MOD_Chorus(S,ctx); blocks++;
      for(int i=0;i<32;i++) if(!isfinite(blk[i])||fabsf(blk[i])>=8.0f) bad++;
    }
    free(P);free(st);free(arena);free(dl);free(S);free(blk);free(in);free(probe);free(probeptr);free(ctx);
  }
  printf("blocks=%ld non-finite/out-of-range output samples=%ld\n",blocks,bad); return bad!=0; }
