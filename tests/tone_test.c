#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <complex.h>
typedef uintptr_t uw; void Fx_MOD_Chorus(uw*S,uw*ctx);
static const float TAB[11][6]={ {0.9989f,-0.9989f,0.9979f,0.0247f,0.0247f,0.9650f},{0.9989f,-0.9989f,0.9979f,0.0433f,0.0433f,0.9312f},
 {0.9989f,-0.9989f,0.9979f,0.0747f,0.0747f,0.8668f},{0.9989f,-0.9989f,0.9979f,0.1255f,0.1255f,0.7491f},{0.9989f,-0.9989f,0.9979f,0.2030f,0.2030f,0.5941f},
 {0.9989f,-0.9989f,0.9979f,0.2712f,0.2712f,0.4577f},{0.9989f,-0.9989f,0.9979f,0.4634f,0.4634f,0.0731f},{0.9989f,-0.9989f,0.9979f,0.9965f,0.9965f,-0.9929f},
 {0.9825f,-0.9825f,0.9650f,0.9965f,0.9965f,-0.9929f},{1.3363f,-1.3363f,0.8921f,0.9965f,0.9965f,-0.9929f},{1.7450f,-1.7450f,0.7491f,0.9965f,0.9965f,-0.9929f}};
static double ideal(int t,double f){ double w=2*M_PI*f/44100; double complex z=cexp(-I*w);
 double complex H=(TAB[t][0]+TAB[t][1]*z)/(1-TAB[t][2]*z); double complex Lp=(TAB[t][3]+TAB[t][4]*z)/(1-TAB[t][5]*z); return 20*log10(cabs(H*Lp)); }
int main(void){
  double fr[]={100,1000,3000,6000,10000,15000};
  printf("tone | gain dB at 100 / 1k / 3k / 6k / 10k / 15k Hz   (kernel vs ideal HPxLP)\n");
  for(int t=0;t<11;t+=1){
    static float P[30]; int*Pi=(int*)P; for(int i=0;i<30;i++)P[i]=0; P[0]=1;P[2]=1;P[3]=1;P[5]=1;P[26]=1;P[28]=1;P[29]=1; for(int j=0;j<6;j++)P[7+j]=TAB[t][j];
    Pi[15]=9739; Pi[22]=423;   /* Depth 0 -> no modulation (inverted host value) */
    printf("%4d |",t);
    for(int k=0;k<6;k++){
      static uint32_t st[14]; static float arena[2000]; for(int i=0;i<2000;i++)arena[i]=1e9f;   /* garbage */
      uw dl[3]={(uw)arena,(uw)(arena+2000),8000}; uw S[4]={0,(uw)P,(uw)st,(uw)dl}; float blk[32]; uint32_t in[16]; uint32_t *dst,probe; dst=&probe; uw pp=(uw)dst; uw ctx[20]={0}; ctx[1]=(uw)blk; ctx[7]=(uw)&pp; ctx[8]=(uw)in;
      double ph=0,peak=0; for(int b=0;b<3000;b++){ for(int i=0;i<16;i++){ float x=0.1f*sinf(ph); ph+=2*M_PI*fr[k]/44100; blk[i]=x; blk[16+i]=x; }
        Fx_MOD_Chorus(S,ctx); if(b>2000) for(int i=0;i<16;i++) if(fabsf(blk[i])>peak)peak=fabsf(blk[i]); }
      printf(" %6.1f/%-6.1f",20*log10(peak/0.1),ideal(t,fr[k]));
    } printf("\n"); }
  return 0; }
