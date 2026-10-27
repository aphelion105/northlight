#include "native_local_light_probe.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main(){
    NorthlightNativeLocalLight::Probe probe;NorthlightNativeLocalLight::Sample out;
    float rows[256][4]={};bool known[256]={};
    rows[17][0]=.6f;rows[17][1]=.3929412f;rows[17][2]=.0682353f;rows[17][3]=1;
    for(unsigned i=17;i<28;++i)known[i]=true;
    assert(!probe.sample(false,false,true,true,0,1,rows,known,0x7ff,true,out));
    assert(!probe.sample(true,true,true,true,0,1,rows,known,0x7ff,true,out));
    assert(!probe.sample(true,false,false,true,0,1,rows,known,0x7ff,true,out));
    assert(!probe.sample(true,false,true,false,0,1,rows,known,0x7ff,true,out));
    assert(probe.sample(true,false,true,true,0,1,rows,known,0x7ff,true,out));
    assert(out.known==0x7ff&&out.finite==0x7ff&&out.read==0x7ff&&out.exactReads&&out.rows[0][0]==.6f);
    assert(!probe.sample(true,false,true,true,0,1,rows,known,0x7ff,true,out));
    known[18]=false;rows[19][0]=std::numeric_limits<float>::quiet_NaN();
    assert(probe.sample(true,false,true,true,0,1,rows,known,0,false,out));
    assert(!(out.known&2)&&!(out.finite&4)&&out.rows[1][0]==0&&!out.exactReads);
    for(unsigned i=0;i<6;++i)assert(probe.sample(true,false,true,true,0,100+i,rows,known,0,false,out));
    assert(!probe.sample(true,false,true,true,0,500,rows,known,0,false,out));
    assert(probe.sample(true,false,true,true,127,1,rows,known,0,false,out));
    unsigned remaining=0;
    for(unsigned frame=128;frame<400;++frame)remaining+=probe.sample(true,false,true,true,frame,1,rows,known,0,false,out);
    assert(remaining==119);
    std::puts("PASS native local-light probe: exact/unknown reads, no fabricated rows, inactive gates, dedup, per-frame/session bounds");
}
