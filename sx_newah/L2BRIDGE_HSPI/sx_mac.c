/* SPDX-License-Identifier: MIT */
#include "sx_mac.h"
#include <string.h>
static int hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
int sx_parse_mac(const char*t,uint8_t out[6]) {
    if(!t||!out||strlen(t)!=17)return -1;
    uint8_t tmp[6];
    for(unsigned i=0;i<6;i++){int a=hex(t[i*3]),b=hex(t[i*3+1]);if(a<0||b<0||(i<5&&t[i*3+2]!=':'))return -1;tmp[i]=(uint8_t)(a*16+b);}
    if((tmp[0]&1U)||!memcmp(tmp,"\0\0\0\0\0\0",6))return -1;
    memcpy(out,tmp,6);return 0;
}
