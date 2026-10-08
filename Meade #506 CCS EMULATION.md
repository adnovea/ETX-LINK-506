#506 CCS Emulation

I tough it would be easy to emulate the Meade DATA/CLK pseudi I2C bus at 1.5 kHZ with a 240 MHz ESP2.
I was totally wrong!
I develop pieces of software but I'm not a software engineer and completly fail to handle a Real-Time task on an ESP32.

Some trials were working standalone but fail once integrated into the whole code due to other tasks.
The issues is the bus desynchronization after few tenth of seconds that bring the Autostar to detect bus corruption and pause for about 5 seconds.
I also noticed that the #506 CCS module is far from being perfect and can also encountered troubles.


My test

#Pseudo I2C bus# : based on Wire.h lib wiht callback function. I failed to implement this because of the specificities.
Dedicated Core : I ended with the ESP32 keep rebooting
Interrupt Level 3 with IRAM_ATTR : run a fex tenth of sec and desync.
Interrupt Level 4/5 with ASM code to handle the bus: cannot make it work
RMT (Remote Control)
SPI
