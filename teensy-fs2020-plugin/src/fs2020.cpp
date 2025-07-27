#include "TeensyControls.h"
#include <map>
#include <string>
#include "SimConnect.h"
#include "jetbridge.h"
#include "fs2020.h"

const char* VersionString = "v1.2.0";

const int MaxDataMappings = 256;

HANDLE hSimConnect;
bool connected = false;
bool quit = false;
double* dataPtr = NULL;
int dataMappings = 0;
int readMappings = 0;
DataMapping dataMapping[MaxDataMappings];
std::map<std::string, int> dataMap;
std::map<DWORD, std::string> packetMap;


void CALLBACK MyDispatchProc(SIMCONNECT_RECV* pData, DWORD cbData, void* pContext)
{
    switch (pData->dwID)
    {
    case SIMCONNECT_RECV_ID_EVENT:
    {
        SIMCONNECT_RECV_EVENT* evt = (SIMCONNECT_RECV_EVENT*)pData;

        switch (evt->uEventID)
        {
        case EVENT_SIM_START:
            break;

        default:
            printf("Unknown event: %d\n", evt->uEventID);
            break;
        }
        break;
    }

    case SIMCONNECT_RECV_ID_SIMOBJECT_DATA:
    {
        auto pObjData = static_cast<SIMCONNECT_RECV_SIMOBJECT_DATA*>(pData);
        if (pObjData->dwRequestID != REQUEST_DATA) {
            break;
        }

        int dataSize = pObjData->dwSize - ((int)(&pObjData->dwData) - (int)pData);

        if (dataSize != readMappings * sizeof(double)) {
            printf("Fatal Error: SimConnect data expected %d bytes but received %d bytes\n", readMappings * (int)sizeof(double), dataSize);
            quit = true;
            break;
        }

        dataPtr = (double*)&pObjData->dwData;

        // Delayed read after write
        for (int i = 0; i < dataMappings; i++) {
            if (dataMapping[i].setDelay > 0) {
                if (*(dataPtr + dataMapping[i].readOffset) == dataMapping[i].setValue) {
                    //printf("Delayed read value is already %f\n", dataMapping[i].setValue);
                    dataMapping[i].setDelay = 0;
                }
                else {
                    dataMapping[i].setDelay--;
                    if (dataMapping[i].setDelay > 0) {
                        //printf("Delayed read %d value %f suppressed (keep at %f)\n", dataMapping[i].setDelay, *(dataPtr + i), dataMapping[i].setValue);
                        *(dataPtr + dataMapping[i].readOffset) = dataMapping[i].setValue;
                    }
                    else {
                        //printf("Delayed read %d value %f not suppressed\n", dataMapping[i].setDelay, *(dataPtr + i));
                    }
                }
            }
        }

        break;
    }

    case SIMCONNECT_RECV_ID_QUIT:
    {
        //printf("Received: Quit\n");
        //quit = true;
        break;
    }

    case SIMCONNECT_RECV_ID_EXCEPTION:
    {
        SIMCONNECT_RECV_EXCEPTION* pObjData = (SIMCONNECT_RECV_EXCEPTION*)pData;
        if (pObjData->dwException == SIMCONNECT_EXCEPTION_NAME_UNRECOGNIZED) {
            int packetId = pObjData->dwSendID;
            if (packetMap.count(packetId) == 0) {
                printf("FS2020 SDK: Fatal Error - Unrecognised Read or Write Var\n");
            }
            else {
                printf("FS2020 SDK: Fatal Error - Unrecognised %s\n", packetMap[packetId].c_str());
            }

            // Terminal
            //quit = true;
        }
        else if (pObjData->dwException == SIMCONNECT_EXCEPTION_UNRECOGNIZED_ID) {
            printf("FS2020 SDK: Error - Unrecognised id\n");
        }
        else if (pObjData->dwException == SIMCONNECT_EXCEPTION_DATA_ERROR) {
            printf("FS2020 SDK: Error - Data error (Write Var may not be writeable!)\n");
        }
        else {
            printf("FS2020 SDK: Error - Exception: %d\n", pObjData->dwException);
        }
        break;
    }

    case SIMCONNECT_RECV_ID_OPEN:
    {
        //printf("Received: Open\n");
        break;
    }

    default:
    {
        //printf("Received SIMCONNECT_RECV_ID_{%d}\n", pData->dwID);
        break;
    }
    }
}

bool addDataDef(SIMCONNECT_DATA_DEFINITION_ID defId, const char* var, const char* units)
{
    if (defId == DEF_READ) {
        //printf("FS2020 add Read Var: %s (%s)\n", var, units);
    }
    else {
        //printf("FS2020 add Write Var: %s (%s)\n", var, units);
    }

    if (strcmp(units, "string") == 0) {
        return (SimConnect_AddToDataDefinition(hSimConnect, defId, var, NULL, SIMCONNECT_DATATYPE_STRING32) == 0);
    }
    else if (stricmp(units, "10khz") == 0) {
        return (SimConnect_AddToDataDefinition(hSimConnect, defId, var, "khz") == 0);
    }
    else {
        return (SimConnect_AddToDataDefinition(hSimConnect, defId, var, units) == 0);
    }
}

int dataRefNum(const char* dataRef, int id)
{
    if (dataMap.count(dataRef) == 0) {
        printf("Teensy requested an unmapped Data Ref #%d: %s\n", id, dataRef);
        return -1;
    }

    return dataMap[dataRef];
}

char* dataRefName(int refNum)
{
    static char testStr[256];

    if (*dataMapping[refNum].readVar != '\0') {
        return dataMapping[refNum].readVar;
    }

    if (dataMapping[refNum].testValue == MAXINT) {
        strcpy(testStr, "Not mapped yet!");
    }
    else {
        sprintf(testStr, "Test value %.3f (%+.3f)", dataMapping[refNum].testValue, dataMapping[refNum].testAdjust);
    }

    return testStr;
}

double dataRefRead(int refNum)
{
    if (dataMapping[refNum].testValue != MAXINT) {
        dataMapping[refNum].testValue += dataMapping[refNum].testAdjust;
        return dataMapping[refNum].testValue;
    }

    if (dataPtr == NULL) {
        return MAXINT;
    }

    double value = *(dataPtr + dataMapping[refNum].readOffset);

    if (stricmp(dataMapping[refNum].readVarUnits, "10khz") == 0) {
        value /= 10;
    }

    return round(value * 1000.0) / 1000.0;
}

void dataRefWrite(int refNum, double value, bool isAdjust)
{
    if (!connected || !dataPtr) {
        return;
    }

    double origVal = -1;
    if (dataPtr) {
        origVal = round(*(dataPtr + dataMapping[refNum].readOffset) * 1000.0) / 1000.0;
    }

    char writeVarUnits[16];
    if (stricmp(dataMapping[refNum].writeVarUnits, "10khz") == 0) {
        value *= 10;
        strcpy(writeVarUnits, "khz");
    }
    else {
        strcpy(writeVarUnits, dataMapping[refNum].writeVarUnits);
    }

    value = round(value * 1000.0) / 1000.0;

    if (origVal == value) {
        return;
    }

#ifdef DEBUG
    printf("Value changed by Teensy - Change %s from %.3f to %.3f\n", dataMapping[refNum].writeVar, origVal, value);
#endif

    writeJetbridgeVar(dataMapping[refNum].writeVar, writeVarUnits, value);

    // Delayed read after write
    dataMapping[refNum].setValue = value;
    dataMapping[refNum].setDelay = 3;
}

bool dataRefWritten(int refNum)
{
    return (dataMapping[refNum].setDelay > 0);
}

void strTrunc(char* dest, char* src)
{
    while (*src == ' ' || *src == '\t') {
        src++;
    }
    strcpy(dest, src);

    while (strlen(dest) > 0 && (dest[strlen(dest) - 1] == ' ' || dest[strlen(dest) - 1] == '\t')) {
        dest[strlen(dest) - 1] = '\0';
    }
}

bool loadDataMappings(const char* filename)
{
    char path[256];

    if (filename[1] == ':') {
        // Absolute path
        strcpy(path, filename);
    }
    else {
        // Relative path
        GetModuleFileNameA(NULL, path, 256);
        char* pos = strrchr(path, '\\');
        strcpy(pos + 1, filename);
    }

    FILE* inf = fopen(path, "r");
    if (!inf) {
        printf("File not found: %s\n", path);
        return false;
    }

    printf("Loading data mappings from %s\n", path);

    char line[1024];
    int lineNum = 0;
    while (fgets(line, 1024, inf) != 0) {
        lineNum++;

        char* pos = strchr(line, '#');
        if (pos) {
            *pos = '\0';
        }

        char* end = &line[strlen(line) - 1];
        while (end >= &line[0] && *end == '\r' || *end == '\n' || *end == ' ' || *end == '\t') {
            end--;
        }
        if (end < &line[0]) {
            continue;
        }
        *(end+1) = '\0';

        if (strlen(line) == 0) {
            continue;
        }

        char* readVarPos = strchr(line, ';');
        if (!readVarPos) {
            printf("Error in data mapping file: Line %d does not contain a semi-colon\n", lineNum);
            return false;
        }
        *readVarPos = '\0';
        strTrunc(dataMapping[dataMappings].dataRef, line);
        if (*dataMapping[dataMappings].dataRef == '\0') {
            printf("Error in data mapping file: Line %d has a missing Data Ref\n", lineNum);
            return false;
        }
        readVarPos++;

        *dataMapping[dataMappings].writeVar = '\0';
        *dataMapping[dataMappings].writeVarUnits = '\0';
        dataMapping[dataMappings].testValue = MAXINT;

        char* writeVarPos = strchr(readVarPos, ';');
        if (writeVarPos) {
            *writeVarPos = '\0';
            strTrunc(dataMapping[dataMappings].writeVar, writeVarPos + 1);
            if (*dataMapping[dataMappings].writeVar != '\0') {
                if (strchr(dataMapping[dataMappings].writeVar, ';')) {
                    printf("Error in data mapping file: Line %d contains more than two semi-colons\n", lineNum);
                    return false;
                }

                char* unitsPos = strchr(dataMapping[dataMappings].writeVar, ',');
                if (!unitsPos) {
                    printf("Error in data mapping file: Line %d Write Var does not contain a comma\n", lineNum);
                    return false;
                }
                *unitsPos = '\0';
                strTrunc(dataMapping[dataMappings].writeVarUnits, unitsPos + 1);
            }
        }

        strTrunc(dataMapping[dataMappings].readVar, readVarPos);
        if (*dataMapping[dataMappings].readVar != '\0') {
            if (isdigit(*dataMapping[dataMappings].readVar)) {
                char* adjustPos = strchr(dataMapping[dataMappings].readVar, '+');
                if (!adjustPos) {
                    adjustPos = strchr(dataMapping[dataMappings].readVar, '-');
                }
                if (adjustPos) {
                    sscanf(adjustPos, "%lf", &dataMapping[dataMappings].testAdjust);
                    *adjustPos = '\0';
                }
                else {
                    dataMapping[dataMappings].testAdjust = 0;
                }

                sscanf(dataMapping[dataMappings].readVar, "%lf", &dataMapping[dataMappings].testValue);
                *dataMapping[dataMappings].readVar = '\0';

#ifdef MORE_DEBUG
                printf("Data Ref Test %d = %s  Value: %.3f  Adjust: %.3f\n", dataMappings, dataMapping[dataMappings].dataRef,
                    dataMapping[dataMappings].testValue, dataMapping[dataMappings].testAdjust);
#endif
            }
            else {
                char* unitsPos = strchr(dataMapping[dataMappings].readVar, ',');
                if (!unitsPos) {
                    printf("Error in data mapping file: Line %d Read Var does not contain a comma\n", lineNum);
                    return false;
                }
                *unitsPos = '\0';
                strTrunc(dataMapping[dataMappings].readVarUnits, unitsPos + 1);
                if (*dataMapping[dataMappings].readVarUnits == '\0') {
                    printf("Error in data mapping file: Line %d Read Var has missing Units\n", lineNum);
                    return false;
                }
            }
        }

        if (*dataMapping[dataMappings].writeVar == '\0') {
            strcpy(dataMapping[dataMappings].writeVar, dataMapping[dataMappings].readVar);
        }

        if (*dataMapping[dataMappings].writeVarUnits == '\0') {
            strcpy(dataMapping[dataMappings].writeVarUnits, dataMapping[dataMappings].readVarUnits);
        }

#ifdef MORE_DEBUG
        printf("Data Mapping %d = %s, %s (%s), %s (%s)\n", dataMappings, dataMapping[dataMappings].dataRef, dataMapping[dataMappings].readVar,
            dataMapping[dataMappings].readVarUnits, dataMapping[dataMappings].writeVar, dataMapping[dataMappings].writeVarUnits);
#endif

        if (!dataMap.insert(std::make_pair(std::string(dataMapping[dataMappings].dataRef), dataMappings)).second) {
            printf("Error in data mapping file: Line %d has duplicate Data Ref\n", lineNum);
            return false;
        }

        dataMapping[dataMappings].setDelay = 0;
        dataMappings++;
    }

    fclose(inf);

    printf("Loaded %d data mappings\n", dataMappings);
    return true;
}

void savePacketId(char* var, char* units, bool isReadVar)
{
    DWORD packetId;
    if (SimConnect_GetLastSentPacketID(hSimConnect, &packetId) != 0) {
        printf("FS2020 SDK: Failed to get packet id\n");
        return;
    }

    char varStr[256];
    if (isReadVar) {
        sprintf(varStr, "Read Var: %s (%s)", var, units);
    }
    else {
        sprintf(varStr, "Write Var: %s (%s)", var, units);
    }

    if (!packetMap.insert(std::make_pair(packetId, std::string(varStr))).second) {
        printf("Error in packet mapping file: Duplicate packetId: %d\n", packetId);
        return;
    }
}

bool dataStart()
{
    if (dataMappings == 0) {
        return true;
    }

    readMappings = 0;
    int readOffset = 0;

    for (int i = 0; i < dataMappings; i++) {
        if (*dataMapping[i].readVar != '\0') {
            // All variables are read at once so add to read def
            if (!addDataDef(DEF_READ, dataMapping[i].readVar, dataMapping[i].readVarUnits)) {
                printf("FS2020 SDK: Unknown Read Var or bad units: %s (%s)\n", dataMapping[i].readVar, dataMapping[i].readVarUnits);
                return false;
            }

            savePacketId(dataMapping[i].readVar, dataMapping[i].readVarUnits, true);
            readMappings++;

            dataMapping[i].readOffset = readOffset;
            readOffset++;
        }

        // Now using Jetbridge to write vars instead of addDataDef etc.
        //if (*dataMapping[i].writeVar != '\0') {
        //    // Each variable can be written individually so create a unique write def
        //    if (!addDataDef(DEF_WRITE + i, dataMapping[i].writeVar, dataMapping[i].writeVarUnits)) {
        //        printf("FS2020 SDK: Unknown Write Var or bad units: %s (%s)\n", dataMapping[i].writeVar, dataMapping[i].writeVarUnits);
        //        return false;
        //    }

        //    savePacketId(dataMapping[i].writeVar, dataMapping[i].writeVarUnits, false);
        //}
    }

    // Start requesting data
    if (SimConnect_RequestDataOnSimObject(hSimConnect, REQUEST_DATA, DEF_READ, SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD_VISUAL_FRAME, 0, 0, 0, 0) != 0) {
        printf("FS2020 SDK: Failed to start requesting data\n");
        return false;
    }

    return true;
}

void dataStop()
{
    if (!connected || dataMappings == 0) {
        return;
    }

    SimConnect_RequestDataOnSimObject(hSimConnect, REQUEST_DATA, DEF_READ, SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD_NEVER, 0, 0, 0, 0);
}

bool simOpen()
{
    HRESULT result = SimConnect_Open(&hSimConnect, "Teensy FS2020 Plugin", NULL, 0, 0, 0);
    if (result == 0) {
        if (!dataStart()) {
            // Fatal
            dataMappings = 0;
            quit = true;
        }

        jetbridgeInit(hSimConnect);

        connected = true;
        return true;
    }

    return false;
}

int __cdecl _tmain(int argc, _TCHAR* argv[])
{
    printf("Teensy FS2020 Plugin %s Copyright (c) 2025 Scott Vincent\n", VersionString);

    if (argc < 2) {
        //printf("Please supply the mapping file to load\n");
        //printf("Note: All variables in the mapping file will be read constantly so only\n");
        //printf("include the variables you actually need for your Teensy's to read.\n");
        //Sleep(10000);
        //return 1;
        if (!loadDataMappings("data_mapping.txt")) {
            printf("Failed to read data mappings from %s\n", argv[1]);
            return 1;
        }
    }
    else {
        if (!loadDataMappings(argv[1])) {
            printf("Failed to read data mappings from %s\n", argv[1]);
            return 1;
        }
    }

    printf("Searching for local MS FS2020...\n");
    connected = false;

    HRESULT result;

    int loopMillis = 30;
    int retryDelay = 0;

    while (!quit)
    {
        if (connected) {
            bool disconnect = false;
            result = SimConnect_CallDispatch(hSimConnect, MyDispatchProc, NULL);
            if (result != 0) {
                // Try immediate reconnect
                if (simOpen()) {
                    result = SimConnect_CallDispatch(hSimConnect, MyDispatchProc, NULL);
                }
                if (result != 0) {
                    printf("Disconnected from MS FS2020\n");
                    connected = false;
                    printf("Searching for local MS FS2020...\n");
                }
            }
        }
        else if (retryDelay > 0) {
            retryDelay--;
        }
        else if (simOpen()) {
            printf("Connected to MS FS2020\n");
        }
        else {
            retryDelay = 200;
        }

        TeensyControls_delete_offline_teensy();
        TeensyControls_find_new_usb_devices();
        TeensyControls_input(0, 0);
        TeensyControls_update_xplane(0);
        TeensyControls_output(0, 0);

        Sleep(loopMillis);
    }

    if (connected) {
        dataStop();
        SimConnect_Close(hSimConnect);
    }

    TeensyControls_usb_close();
    TeensyControls_delete_offline_teensy();

    printf("Teensy FS2020 Plugin stopping\n");

    Sleep(10000);
    return 0;
}
