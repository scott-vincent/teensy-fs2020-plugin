# If you have a newer Raspberry Pi uncomment the next line to use gpiod instead of WiringPi
#UseGpiod=1

if [ "$UseGpiod" = 1 ]
then
  gpioLib="gpiod"
else
  gpioLib="wiringPi"
fi

echo Building teensy-pi-plugin
cd teensy-fs2020-plugin
g++ -o teensy-pi-plugin -I headers \
    src/io.cpp \
    src/memory.cpp \
    src/TeensyControls.cpp \
    src/thread.cpp \
    src/usb.cpp \
    src/pi.cpp \
    src/gpio.cpp \
    -l${gpioLib} -ludev -lpthread || exit
echo Done
