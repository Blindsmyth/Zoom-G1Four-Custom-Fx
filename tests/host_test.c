#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
typedef uintptr_t uw;
void Fx_MOD_Chorus(uw*S,uw*ctx);
int main(int argc,char**argv){
  int depthv=argc>1?atoi(argv[1]):423, ratev=argc>2?atoi(argv[2]):300000; float mix=argc>3?atof(argv[3]):1.0f;
  static float P[30]; int*Pi=(int*)P; P[0]=1;P[2]=1;P[3]=1;P[5]=1;P[26]=1;P[28]=1;P[29]=mix;
  P[7]=0.9989f;P[8]=-0.9989f;P[9]=0.9979f; P[10]=0.9965f;P[11]=0.9965f;P[12]=-0.9929f;   /* Tone 7: flat */ Pi[15]=ratev; Pi[22]=687-depthv;
  static uint32_t st[14]; static float dbuf[2500]; for(int i=0;i<2500;i++)dbuf[i]=NAN;
  static uw dl[3]; dl[0]=(uw)dbuf; dl[1]=(uw)(dbuf+2500); dl[2]=2500*4;
  static uw S[4]; S[1]=(uw)P; S[2]=(uw)st; S[3]=(uw)dl;
  static float blk[32]; static uint32_t in[16]; static uint32_t *dst=0, probe; dst=&probe;
  static uw ctx[20]; ctx[1]=(uw)blk; ctx[7]=(uw)&dst; ctx[8]=(uw)in;
  int N=44100*4; float *out=malloc(N*sizeof(float)); double ph=0; int bad=0;
  for(int b=0;b<N/16;b++){ for(int i=0;i<16;i++){ float x=0.5f*sinf(ph); ph+=2*M_PI*1000.0/44100; blk[i]=x; blk[16+i]=x; }
    Fx_MOD_Chorus(S,ctx); for(int i=0;i<16;i++){ out[b*16+i]=blk[i]; if(!isfinite(blk[i])||fabsf(blk[i])>2) bad++; } }
  // measure instantaneous freq via zero crossings (rising) in last 3 s
  double lastz=-1,fmin=1e9,fmax=0,mx=0; for(int i=44100;i<N-1;i++){ if(fabsf(out[i])>mx)mx=fabsf(out[i]);
    if(out[i]<=0&&out[i+1]>0){ double z=i+(-out[i])/(out[i+1]-out[i]); if(lastz>0){ double f=44100.0/(z-lastz); if(f<fmin)fmin=f; if(f>fmax)fmax=f;} lastz=z; } }
  printf("depth=%d rate=%d mix=%.1f  bad=%d peak=%.3f  freq range %.1f..%.1f Hz (%.2f%%..%.2f%%)\n",depthv,ratev,mix,bad,mx,fmin,fmax,(fmin/1000-1)*100,(fmax/1000-1)*100);
  return 0;}
