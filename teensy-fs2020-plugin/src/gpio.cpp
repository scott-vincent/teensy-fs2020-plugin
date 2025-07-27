#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpio.h"

// If you have a newer Raspberry Pi uncomment the next line to use gpiod instead of WiringPi
//#define UseGpiod

#ifdef UseGpiod

#include <gpiod.h>

const int MaxGpio = 28;
char chipName[16];
struct gpiod_chip* gpioChip;
struct gpiod_line_bulk gpioLines;
int gpioValues[MaxGpio];

void gpioInit()
{
    unsigned int offsets[MaxGpio];

    // Raspberry Pi 5 uses gpiochip4 rather than gpiochip0
    FILE* inf = fopen("/dev/gpiochip4", "r");
    if (inf) {
        fclose(inf);
        strcpy(chipName, "gpiochip4");
    }
    else {
        strcpy(chipName, "gpiochip0");
    }

    gpioChip = gpiod_chip_open_by_name(chipName);
    if (!gpioChip) {
        printf("Failed to open chip %s\n", chipName);
        return;
    }

    for (int i = 0; i < MaxGpio; i++) {
        offsets[i] = i;
    }

    if (gpiod_chip_get_lines(gpioChip, offsets, MaxGpio, &gpioLines)) {
        printf("Failed to get chip %s lines\n", chipName);
        return;
    }

    struct gpiod_line_request_config config;
    memset(&config, 0, sizeof(config));
    config.consumer = "teensy-pi-plugin";
    config.request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
    config.flags = GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;

    if (gpiod_line_request_bulk(&gpioLines, &config, gpioValues)) {
        printf("Failed to bulk request chip %s lines\n", chipName);
        return;
    }
}

void gpioAdd(int gpioNum)
{
    // Nothing to do as all lines have been set to inputs and pulled-up
}

void gpioReadAll()
{
    // Just read every gpio state in a single call
    if (gpiod_line_get_value_bulk(&gpioLines, gpioValues)) {
        printf("Failed to get gpio values\n");
        return;
    }
}

int gpioGetState(int gpioNum)
{
    // States have already been read so just return the requested one
    return gpioValues[gpioNum];
}

#else

#include <wiringPi.h>

void gpioInit()
{
    // Use BCM GPIO pin numbers
    wiringPiSetupGpio();
}

void gpioAdd(int gpioNum)
{
    // NOTE: pullUpDnControl does not work on RasPi4 so have
    // to use raspi-gpio command line to pull up resistors.
    char command[256];

    pinMode(gpioNum, INPUT);
    sprintf(command, "raspi-gpio set %d pu", gpioNum);

    if (system(command) != 0) {
        printf("Failed to run raspi-gpio command\n");
    }
}

void gpioReadAll()
{
    // Nothing to do as states are read individually
}

int gpioGetState(int gpioNum)
{
    return digitalRead(gpioNum);
}

#endif