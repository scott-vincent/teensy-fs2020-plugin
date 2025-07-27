enum EVENT_ID {
    EVENT_SIM_START,
    EVENT_QUIT,
    EVENT_SIM_STOP,
};

enum REQUEST_ID {
    REQUEST_DATA,
};

enum DEFINITION_ID {
    DEF_READ,
    DEF_WRITE,  // Do not add any defs after this one (gets incremented for each var)
};

struct DataMapping {
    char dataRef[256];
    char readVar[256];
    char readVarUnits[256];
    char writeVar[256];
    char writeVarUnits[256];
    int readOffset;
    double testValue;
    double testAdjust;
    double setValue;
    int setDelay;
};

int dataRefNum(const char* dataRef, int id);
char* dataRefName(int refNum);
double dataRefRead(int refNum);
void dataRefWrite(int refNum, double value, bool isAdjust = false);
bool dataRefWritten(int refNum);
