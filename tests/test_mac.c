#include <assert.h>
#include <string.h>
#include "sx_mac.h"
int main(void){
    unsigned char b[6]={0};
    assert(sx_parse_mac("02:AB:cd:01:23:45",b)==0);
    const unsigned char expected[]={2,171,205,1,35,69};assert(!memcmp(b,expected,6));
    assert(sx_parse_mac("ff:ff:ff:ff:ff:ff",b)!=0);
    assert(sx_parse_mac("00:00:00:00:00:00",b)!=0);
    assert(sx_parse_mac("02:00:00:00:00:0g",b)!=0);
    assert(sx_parse_mac("02-00-00-00-00-01",b)!=0);
    assert(sx_parse_mac("02:00:00:00:00:01x",b)!=0);
    assert(sx_parse_mac("02:00",b)!=0);
    return 0;
}
