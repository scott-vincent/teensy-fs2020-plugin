#include "TeensyControls.h"
#include "..\jetbridge\Client.h"

jetbridge::Client* jetbridgeClient = 0;


void jetbridgeInit(HANDLE hSimConnect)
{
    if (jetbridgeClient != 0) {
        delete jetbridgeClient;
    }

    jetbridgeClient = new jetbridge::Client(hSimConnect);
}

void writeJetbridgeVar(const char* var, const char* units, double val)
{
    // FS2020 uses RPN (Reverse Polish Notation).
    char rpnCode[128];
    if (strchr(var, ':')) {
        sprintf(rpnCode, "%f (>%s,%s)", val, var, units);
    }
    else {
        sprintf(rpnCode, "%f (>A:%s,%s)", val, var, units);
    }

    jetbridgeClient->request(rpnCode);
    //printf("%s\n", rpnCode);
}
