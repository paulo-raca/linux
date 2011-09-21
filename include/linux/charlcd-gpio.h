/*
 * Driver for Hitachi HD44780 LCD driven by GPIO pins
 * http://en.wikipedia.org/wiki/HD44780_Character_LCD
 * Currently it will just display the text "ARM Linux" and the linux version.
 * 
 * Based on "arm-charlcd" by Linus Walleij
 */

#define MAX_CHARLCD_GPIO 16

#define IOCTL_SEND_COMMAND 1
#define IOCTL_READ_STATUS  2
#define IOCTL_RESET 3

struct charlcd_gpio_pin {
  long gpio;
  unsigned  active_low : 1;
  const char* label;
  struct charlcd_gpio_pin* next_pin; //If Many pins must be toggled
};

struct charlcd_gpio {
  struct charlcd_gpio_pin* EN;
  struct charlcd_gpio_pin* RS;
  struct charlcd_gpio_pin* RW;
  struct charlcd_gpio_pin* DATA[8];
};
