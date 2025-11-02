#include <spicenet/config.h>
#include <spicenet/snp.h>

#include "IPCHelper.h"

int main(int argv, char **argc)
{
    if(setup()) return 1;
    sendMessage("HELLO FROM C!!!!!", 17);
    while(1) 
    {
        if(checkForMessages()) return 1;
    }
    return 1;
}
