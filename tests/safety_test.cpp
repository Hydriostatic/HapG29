#include "../src/safety.h"
#include <cstdio>
using namespace haptics;
static double run(SafetyConfig c, double target, double secs, bool* held=nullptr, double* avg=nullptr){
  ChannelLimiter ch; DutyMonitor d; double out=0, t=0, dt=0.01, sum=0; int n=0;
  for(; t<secs; t+=dt){ double s=d.update(DutyMonitor::demandOf(target,0,c),dt,c); out=ch.step(target,t,dt,c,s); if(t>secs-10){sum+=out;n++;} }
  if(avg)*avg=sum/n;
  if(held)*held=ch.heldCut(); return out;
}
int main(){
  int fail=0; auto chk=[&](bool ok,const char*m){printf("%s %s\n",ok?"OK  ":"FAIL",m); if(!ok)fail++;};
  SafetyConfig c; c.maxOutput=0.10;
  chk(run(c,1.0,0.5) <= 0.1000001, "ganho 10%: saida nunca passa de 10%");
  c.maxOutput=0.99; chk(run(c,1.0,0.5) <= kHardCeiling+1e-9, "ganho pedido 99%: preso no teto rigido de 40%");
  c.maxOutput=0.40; { ChannelLimiter ch; double o=ch.step(1.0,0,0.01,c,1.0); chk(o<0.40 && o>0, "rampa: nao salta para o maximo num passo"); }
  bool held; run(c,1.0,3.5,&held); chk(held, "valor parado >3s: cortado (jogo travado)");
  c.holdTimeout=1000; double avg; double o=run(c,1.0,60,nullptr,&avg);
  chk(avg > 0.14 && avg < 0.16, "vibracao continua maxima por 60s: media REAL de saida converge para 15%");
  printf("   (saida apos 60s continuos: %.1f%%, media real ultimos 10s %.1f%%)\n", o*100, avg*100);
  { ChannelLimiter ch; SafetyConfig c2; c2.maxOutput=0.4; double t=0,out=0; for(;t<0.5;t+=0.01) out=ch.step(1,t,0.01,c2,1);
    for(int i=0;i<15;i++){out=ch.step(0,t,0.01,c2,1);t+=0.01;} chk(out<=1e-9,"decaimento: chega a zero em <=150ms"); }
  return fail;
}
