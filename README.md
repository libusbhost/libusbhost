###General Information


**This library is in an active development.**  
**WARNING**: None of its features are considered stable !

This library implement usb host driver allowing users use
or write device drivers, which functionality
is abstracted of low level implementation.

Main objectives are:
- provide open-source(Lesser GPL3) usb host library for embedded devices
- execution speed: This library doesn't use blocking sleep,
making low overhead on runtime performance
- uses static allocation for all its buffers,
so no allocation and reallocation is affecting performance
(possibility of memory fragmentation. execution time indeterminism),
so no malloc(), realloc(), free().
- written in C, with the support to use it with C++.
- does not depend on any Operating System. Library libopencm3 is used for testing purposes and to get proper defines.
So no runtime dependency is on this library.



Currently supported devices (yet tested) are:
* stm32f407 (stm32f4 Discovery)

Native device drivers (mostly for demonstration purposes):
- HUB
- Gamepad - XBox compatible Controller
- mouse (draft: only displays raw data)
- USB MIDI devices (raw data + note on/off)

###Practical info

!!! Do not forget to invoke   "make clean"  before new build when defines change(_TODO: remove this warning and fix the Makefile_)


**How to initialize repository**

> ./initRepo.sh

fetch libopencm3 submodule and compile needed libraries


**How to compile demo**

Edit usbh_config.h to configure the library (By default Full speed OTG periphery on stm32f4 is supported)


> ./compileDemo.sh

compiles demo, that can be flashed into stm32f4 Discovery platform and debug by USART


**How to upload firmware (FLASH) to stm32f4 Discovery**

> sudo make flash


**How to view debug data**

connect uart to USART6 pins on gpios:  GPIOC6(TX - data), GPIOC7(RX - not used)
configure uart baud on PC side to 921600 with 1 stop bit, no parity, 8bit data, no handshake


**How to compile library only**

> make lib

**libusbhost.a** is built without usart debug support
(check compileDemo.sh for hint on how to compile with debug)


###Contact
Amir Hammad - *amir.hammad@hotmail.com*

**Library is maintained there**
> http://github.com/libusbhost/libusbhost

