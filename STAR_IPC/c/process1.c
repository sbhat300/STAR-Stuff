#include <stdio.h>

#include "IPCHelper.h"

void doSomething(int len, const char* msg)
{
    printf("RECEIVED MESSAGE WITH LENGTH %d:\n", len);
    //for(int i = 0; i < len; i++) printf("%02hhx", *(msg + i));
    for(int i = 0; i < len; i++) printf("%c", *(msg + i));
    printf("\n");
}

int main(int argv, char **argc)
{
    if(setup()) return 1;
    sendMessage("HELLO FROM C!!!!!", 17);
    while(1) 
    {
        if(checkForMessages(doSomething)) return 1;
    }
    return 1;
}
