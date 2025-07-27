#include "TeensyControls.h"
#include <map>
#include <string>
#include <math.h>
#include "pi.h"
#include "gpio.h"

const char* VersionString = "v1.0.1";

const int MaxDataMappings = 256;
const int MaxButtons = 9;

bool quit = false;
int dataMappings = 0;
int readMappings = 0;
DataMapping dataMapping[MaxDataMappings];
std::map<std::string, int> dataMap;
int buttonCount = 0;
ButtonData buttonData[MaxButtons];


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
    return dataMapping[refNum].testValue;
}

void dataRefWrite(int refNum, double value, bool isAdjust)
{
    double origVal = dataMapping[refNum].testValue;
    value = round(value * 1000.0) / 1000.0;

    if (isAdjust) {
        value += origVal;
    }
    else if (origVal == value) {
        return;
    }

#ifdef DEBUG
    printf("Value changed by Pi - Change %s from %.3f to %.3f\n", dataRefName(refNum), origVal, value);
#endif

    dataMapping[refNum].testValue = value;
}

bool dataRefWritten(int refNum)
{
    return false;
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

bool loadDataMappings(const char* exe, const char* filename)
{
    char path[256];

    if (strchr(filename, '/')) {
        // Absolute path
        strcpy(path, filename);
    }
    else {
        // Relative path
        strcpy(path, exe);
        char* pos = strrchr(path, '/');
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

int buttonToGpioPin(int button) {
    switch (button) {
        case 1: return 2;
        case 2: return 3;
        case 3: return 4;
        case 4: return 17;
        case 5: return 27;
        case 6: return 22;
        case 7: return 10;
        case 8: return 9;
        case 9: return 11;
    }

    return -1;
}

bool loadButtons(const char* filename)
{
    FILE* inf = fopen(filename, "r");
    if (!inf) {
        printf("File not found: %s\n", filename);
        return false;
    }

    printf("Loading hardware buttons from %s\n", filename);

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
        *(end + 1) = '\0';

        if (strlen(line) == 0) {
            continue;
        }

        if (strcmp(&line[strlen(line)-4], ".wav") == 0) {
            printf("Skipping sound (.wav) line: %s\n", line);
            continue;
        }


        pos = strchr(line, '=');
        if (!pos) {
            printf("Ignored bad line (no =): %s\n", line);
            continue;
        }

        *pos = '\0';
        pos++;

        int button = atoi(line);
        buttonData[buttonCount].button = button;
        buttonData[buttonCount].gpioPin = buttonToGpioPin(button);
        buttonData[buttonCount].prevGpioVal = 1;

        if (buttonData[buttonCount].gpioPin == -1) {
            printf("Unknown button (no pin): %d\n", button);
            continue;
        }

        char* sepPos = strchr(pos, ' ');
        if (!sepPos) {
            printf("Ignored bad line (no space): %s\n", line);
            continue;
        }

        *sepPos = '\0';
        sepPos++;
        buttonData[buttonCount].refNum = dataRefNum(pos, 0);
        if (buttonData[buttonCount].refNum == -1) {
            continue;
        }

        if (isdigit(*sepPos)) {
            buttonData[buttonCount].initValue = atof(sepPos);
        }
        else {
            buttonData[buttonCount].initValue = MAXINT;
        }

        pos = strchr(sepPos, '+');
        if (!pos) {
            pos = strchr(sepPos, '-');
        }

        if (pos) {
            buttonData[buttonCount].adjust = atof(pos);
        }
        else {
            buttonData[buttonCount].adjust = 0;
        }

        gpioAdd(buttonData[buttonCount].gpioPin);

        if (buttonData[buttonCount].initValue == MAXINT) {
            printf("Added hardware button %d gpio %d to adjust %s by %.3f\n",
                buttonData[buttonCount].button, buttonData[buttonCount].gpioPin, dataRefName(buttonData[buttonCount].refNum),
                buttonData[buttonCount].adjust);
        }
        else {
            printf("Added hardware button %d gpio %d to adjust %s from %.3f by %.3f\n",
                buttonData[buttonCount].button, buttonData[buttonCount].gpioPin, dataRefName(buttonData[buttonCount].refNum),
                buttonData[buttonCount].initValue, buttonData[buttonCount].adjust);
        }

        buttonCount++;
    }

    fclose(inf);

    printf("Loaded %d hardware buttons\n", buttonCount);
    return true;
}

void hardwareInit()
{
    for (int i = 0; i < buttonCount; i++) {
        if (buttonData[i].initValue != MAXINT) {
            dataRefWrite(buttonData[i].refNum, buttonData[i].initValue);
        }
    }
}

int main(int argc, char* argv[])
{
    printf("Teensy Pi Plugin %s Copyright (c) 2025 Scott Vincent\n", VersionString);

    if (argc < 2) {
        //printf("Please supply the mapping file to load\n");
        //printf("Note: All variables in the mapping file will be read constantly so only\n");
        //printf("include the variables you actually need for your Teensy's to read.\n");
        //Sleep(10000);
        //return 1;
        if (!loadDataMappings(argv[0], "data_mapping.txt")) {
            printf("Failed to read data mappings from %s\n", argv[1]);
            return 1;
        }
    }
    else {
        if (!loadDataMappings(argv[0], argv[1])) {
            printf("Failed to read data mappings from %s\n", argv[1]);
            return 1;
        }
    }

    gpioInit();

    char buttonFile[256];
    strcpy(buttonFile, "/media/sounds/Buttons.txt");
    if (!loadButtons(buttonFile)) {
        printf("Failed to read hardware buttons from %s\n", buttonFile);
        return 1;
    }

    int loopMillis = 30;
    int retryDelay = 0;

    bool firstTime = true;
    while (!quit)
    {
        TeensyControls_delete_offline_teensy();
        TeensyControls_find_new_usb_devices();
        TeensyControls_input(0, 0);
        TeensyControls_update_xplane(0);
        TeensyControls_output(0, 0);

        if (firstTime) {
            firstTime = false;
            hardwareInit();
        }

        gpioReadAll();

        for (int i = 0; i < buttonCount; i++) {
            int val = gpioGetState(buttonData[i].gpioPin);
            //if (buttonData[i].prevGpioVal != val) {
                buttonData[i].prevGpioVal = val;
                if (val == 0) {
                    printf("Adjust %s by %.3f\n", dataRefName(buttonData[i].refNum), buttonData[i].adjust);
                    dataRefWrite(buttonData[i].refNum, buttonData[i].adjust, true);
                }
            //}
        }

        usleep(loopMillis * 1000);
    }

    TeensyControls_usb_close();
    TeensyControls_delete_offline_teensy();

    printf("Teensy Pi Plugin stopping\n");

    return 0;
}
