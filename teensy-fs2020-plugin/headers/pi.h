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

struct ButtonData {
    int button;
    int gpioPin;
    int gpioVal;
    int prevGpioVal;
    int refNum;
    double initValue;
    double adjust;
};

int dataRefNum(const char* dataRef, int id);
char* dataRefName(int refNum);
double dataRefRead(int refNum);
void dataRefWrite(int refNum, double value, bool isAdjust = false);
bool dataRefWritten(int refNum);
