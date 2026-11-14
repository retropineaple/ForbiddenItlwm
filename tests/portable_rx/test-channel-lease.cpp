#include <cassert>
#include <cstdio>
#include <cstring>
#include <AWDLControlReceiveProbe.hpp>
int main(){const uint8_t peer[6]={2,3,4,5,6,7};ItlAWDL::ControlReceiveProbe p;assert(p.arm(peer,10000000,7,9,149));assert(p.wants(true,true,true,149,149,10000001,7,9));assert(!p.wants(true,true,true,44,44,10000001,7,9));ItlAWDL::ControlReceiveTag t;t.record(true,peer,7,9,149);assert(!t.take(true,7,9,44));t.record(true,peer,7,9,44);assert(t.take(true,7,9,44));assert(p.arm(peer,10000002,7,9,44));assert(p.wants(true,true,true,44,44,10000003,7,9));assert(!p.wants(true,true,true,149,149,10000003,7,9));assert(!p.arm(peer,10000004,7,9,0));assert(!p.arm(peer,10000004,7,9,256));puts("Channel-bound completion/window regression passed");}
