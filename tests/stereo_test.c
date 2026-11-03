#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
typedef uintptr_t uw; void Fx_MOD_Chorus(uw*S,uw*ctx);
static void run(const char*label,int depth_host,int rate,float mix,int stereo_in){
  static float P[30]; int*Pi=(int*)P; for(int i=0;i<30;i++)P[i]=0; P[0]=1;P[2]=1;P[3]=1;P[5]=1;P[26]=1;P[28]=1;P[29]=mix;
  P[7]=.9989f;P[8]=-.9989f;P[9]=.9979f;P[10]=.9965f;P[11]=.9965f;P[12]=-.9929f; Pi[15]=rate; Pi[22]=depth_host;
  static uint32_t st[14]; static float arena[2000]; for(int i=0;i<2000;i++)arena[i]=1e9f;
  uw dl[3]={(uw)arena,(uw)(arena+2000),8000}; uw S[4]={0,(uw)P,(uw)st,(uw)dl}; float blk[32]; uint32_t in[16]; uint32_t *dst,probe; dst=&probe; uw pp=(uw)dst; uw ctx[20]={0}; ctx[1]=(uw)blk; ctx[7]=(uw)&pp; ctx[8]=(uw)in;
  double pa=0,pb=0,sl=0,sd=0,ss=0; long n=0; 
  for(int b=0;b<44100*3/16;b++){ for(int i=0;i<16;i++){ float xa=0.3f*sinf(pa), xb=stereo_in?0.3f*sinf(pb):xa; pa+=2*M_PI*440/44100; pb+=2*M_PI*660/44100; blk[i]=xa; blk[16+i]=xb; }
    Fx_MOD_Chorus(S,ctx);
    if(b>44100/16) for(int i=0;i<16;i++){ double l=blk[i],r=blk[16+i]; sl+=l*l; sd+=(l-r)*(l-r); ss+=(l+r)*(l+r); n++; } }
  printf("%-46s L rms %.3f   |L-R| rms %.3f  (side/mid %.1f dB)\n",label,sqrt(sl/n),sqrt(sd/n),10*log10((sd+1e-12)/(ss+1e-12))); }
int main(void){
  run("mono in, Depth 100 Rate 100 Mix 100 (wet)",264,545392,1.0f,0);
  run("mono in, Depth 100 Rate 50  Mix 100 (wet)",264,300000,1.0f,0);
  run("mono in, Depth 60  Rate 70  Mix 100 (wet)",330,400000,1.0f,0);
  run("mono in, same but Mix 0 (dry)",264,545392,0.0f,0);
  run("mono in, Depth 0 Mix 100 (no modulation)",423,545392,1.0f,0);
  run("STEREO in (440|660 Hz), Depth 0 Mix 100",423,545392,1.0f,1);
  return 0; }
