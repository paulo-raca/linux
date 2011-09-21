/*
 * Driver for Hitachi HD44780 LCD driven by GPIO pins
 * http://en.wikipedia.org/wiki/HD44780_Character_LCD
 * Currently it will just display the text "ARM Linux" and the linux version.
 * 
 * Based on "arm-charlcd" by Linus Walleij
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/charlcd-gpio.h>
#include <asm/uaccess.h>
#include <generated/utsrelease.h>

#define DRIVERNAME "charlcd-gpio"
#define READ_PULSE_PERIOD  1000 //ns
#define WRITE_PULSE_PERIOD 1000 //ns

struct charlcd_gpio_device {
  struct charlcd_gpio* lcd;
  char in_use;
  char init_cmd;
  unsigned long busy_until;
  char isStalled;
};

static struct charlcd_gpio_device lcds[MAX_CHARLCD_GPIO];
static int major;
static struct class* lcd_class;

static void charlcd_sync_display(struct charlcd_gpio_device* lcd_dev);

static char *line1 = "Linux " UTS_RELEASE;
static char *line2 = "";

module_param(line1, charp, S_IRUGO);
module_param(line2, charp, S_IRUGO);

static void set_output(struct charlcd_gpio_pin* pin, char val) {
  while (pin) {
    if (pin->gpio >= 0) {
      if (gpio_direction_output(pin->gpio, val ^ pin->active_low)) {
//         printk("gpio_direction_output(%s - %ld) failed\n", pin->label, pin->gpio);
        pin->gpio=-1;
      }
    }
    pin = pin->next_pin;
  }
}
static void set_input(struct charlcd_gpio_pin* pin) {
   while (pin) {
    if (pin->gpio >= 0) {
      if (gpio_direction_input(pin->gpio)) {
//         printk("gpio_direction_input(%s - %ld) failed\n", pin->label, pin->gpio);
        pin->gpio=-1;
      }
    }
    pin = pin->next_pin;
  }
}
static void request(struct charlcd_gpio_pin* pin, bool is_output) {
  while (pin) {
    if (pin->gpio >= 0) {
      if (gpio_request(pin->gpio, pin->label)) {
        printk("Skipping unavailable gpio %s(%ld)\n", pin->label, pin->gpio);
        pin->gpio=-1;
      }
      if (is_output) {
        if (gpio_direction_output(pin->gpio, pin->active_low)) {
          printk("gpio_direction_output(%s - %ld) failed\n", pin->label, pin->gpio);
          pin->gpio=-1;
        }
      } else {
        if (gpio_direction_input(pin->gpio)) {
          printk("gpio_direction_input(%s - %ld) failed\n", pin->label, pin->gpio);
          pin->gpio=-1;
        }
      }
    }
    pin = pin->next_pin;
  }
}

static unsigned char charlcd_recv(struct charlcd_gpio_device* lcd_dev, char is_cmd, char blah) {
  struct charlcd_gpio* lcd = lcd_dev->lcd;
  unsigned char result = 0x00;
  
  set_output(lcd->RS, is_cmd?0:1);
  set_output(lcd->RW, 1);
  
  //Upper Nibble
  set_output(lcd->EN, 1);
  ndelay(READ_PULSE_PERIOD/2);
  result |= gpio_get_value(lcd->DATA[7]->gpio) << 7;
  result |= gpio_get_value(lcd->DATA[6]->gpio) << 6;
  result |= gpio_get_value(lcd->DATA[5]->gpio) << 5;
  result |= gpio_get_value(lcd->DATA[4]->gpio) << 4;
  set_output(lcd->EN, 0);
  ndelay(READ_PULSE_PERIOD/2);
  
  //Lower Nibble
  set_output(lcd->EN, 1);
  ndelay(READ_PULSE_PERIOD/2);
  result |= gpio_get_value(lcd->DATA[7]->gpio) << 3;
  result |= gpio_get_value(lcd->DATA[6]->gpio) << 2;
  result |= gpio_get_value(lcd->DATA[5]->gpio) << 1;
  result |= gpio_get_value(lcd->DATA[4]->gpio) << 0;
  set_output(lcd->EN, 0);
  ndelay(READ_PULSE_PERIOD/2);
  
  set_output(lcd->RW, 0); //All data pins in tri-statea (LCD and processor), Just in case... 
  
  return result;
}

static char charlcd_send(struct charlcd_gpio_device* lcd_dev, char is_cmd, unsigned char data) {
  struct charlcd_gpio* lcd = lcd_dev->lcd;
  
  { //Wait busy-flag 
    unsigned long jiffies_begin=jiffies;
    unsigned long busy_count=0;
    
    while (1) { //( (jiffies - lcd_dev->busy_until) <= 0) { //Can skip busy-flag check after "busy_until" time.
      unsigned long millis_elapsed = jiffies_to_msecs(jiffies - jiffies_begin);
      unsigned char state = charlcd_recv(lcd_dev, 1, millis_elapsed>100);
      if (! (state&0x80) ) break; //Device Ready!
      
      if (millis_elapsed >= 50)  {
        printk("Busy flag not responding\n");
	return -1;
//         charlcd_sync_display(lcd_dev);
//         printk("======================================================================\n");
      }
      
      if (millis_elapsed >= 1) 
        msleep(1); //Big sleep
      else
        yield(); //Fast sleep
      
      busy_count++;
    }
  }
  
  set_output(lcd->RS, !is_cmd);
  set_output(lcd->RW, 0);

  //Send upper nibble
  set_output(lcd->DATA[7], (data>>7)&1);
  set_output(lcd->DATA[6], (data>>6)&1);
  set_output(lcd->DATA[5], (data>>5)&1);
  set_output(lcd->DATA[4], (data>>4)&1);
  set_output(lcd->EN, 1);
  ndelay(WRITE_PULSE_PERIOD/2);
  set_output(lcd->EN, 0);
  ndelay(WRITE_PULSE_PERIOD/2);

  //Send lower nibble
  set_output(lcd->DATA[7], (data>>3)&1);
  set_output(lcd->DATA[6], (data>>2)&1);
  set_output(lcd->DATA[5], (data>>1)&1);
  set_output(lcd->DATA[4], (data>>0)&1);
  set_output(lcd->EN, 1);
  ndelay(WRITE_PULSE_PERIOD/2);
  set_output(lcd->EN, 0);
  ndelay(WRITE_PULSE_PERIOD/2);
  
  //All data pins in tri-statea (LCD and processor), Just in case... 
  set_input(lcd->DATA[7]);
  set_input(lcd->DATA[6]);
  set_input(lcd->DATA[5]);
  set_input(lcd->DATA[4]);

  if (is_cmd)
    lcd_dev->busy_until = jiffies + msecs_to_jiffies(5) + 1; //guaranteed to be ready after 5 milli seconds
  else 
    lcd_dev->busy_until = jiffies + msecs_to_jiffies(1) + 1; //guaranteed to be ready after 200 micro seconds
  
  return 0;
}

static void charlcd_print(struct charlcd_gpio_device* lcd, const char* msg) {
  while (msg && *msg) {
    charlcd_send(lcd, false, *(msg++) );
  }
}

static void charlcd_sync_display(struct charlcd_gpio_device* lcd_dev) {
  struct charlcd_gpio* lcd = lcd_dev->lcd;
  
  //Adds 50% do each time, just in case.
  
  set_output(lcd->RS, 0);
  set_output(lcd->RW, 0);
  set_output(lcd->DATA[7], 0);
  set_output(lcd->DATA[6], 0);
  set_output(lcd->DATA[5], 1);
  set_output(lcd->DATA[4], 1);
  
  set_output(lcd->EN, 1);
  ndelay(WRITE_PULSE_PERIOD/2);
  set_output(lcd->EN, 0);  
  msleep(5   * 1.5); //5ms

  set_output(lcd->EN, 1);
  ndelay(WRITE_PULSE_PERIOD/2);
  set_output(lcd->EN, 0);  
  udelay(200 * 1.5); //200us
  
  
  set_output(lcd->EN, 1);
  ndelay(WRITE_PULSE_PERIOD/2);
  set_output(lcd->EN, 0);  
  udelay(200 * 1.5); //200us
  
  //Set to 4 bits mode
  set_output(lcd->DATA[7], 0);
  set_output(lcd->DATA[6], 0);
  set_output(lcd->DATA[5], 1);
  set_output(lcd->DATA[4], 0);
  set_output(lcd->EN, 1);
  ndelay(WRITE_PULSE_PERIOD/2);
  set_output(lcd->EN, 0);  
  udelay(200 * 1.5); //200us
  
  set_input(lcd->DATA[7]);
  set_input(lcd->DATA[6]);
  set_input(lcd->DATA[5]);
  set_input(lcd->DATA[4]);

  charlcd_send(lcd_dev, true, lcd_dev->init_cmd);
}

static void charlcd_init_display(struct charlcd_gpio_device* lcd_dev)
{
  lcd_dev->init_cmd = 0x28;
  charlcd_sync_display(lcd_dev);
  
  charlcd_send(lcd_dev, true, 0x01);
  charlcd_send(lcd_dev, true, 0x08);
  charlcd_send(lcd_dev, true, 0x06);
  charlcd_send(lcd_dev, true, 0x0C);
}












/* TODO:
static ssize_t device_read(struct file* pFile, char __user* pBuf, size_t len, loff_t* offset)
{
    int                    nLen    = len;
    struct _device_data*   pData   = pFile->private_data;

    if(nLen > pData->nPos - *offset)
        nLen = pData->nPos - *offset;

    copy_to_user(pBuf, (pData->pData) + *offset, nLen);

    *offset+=nLen;
    return -EINVAL;
}*/

static ssize_t device_write(struct file* pFile, const char __user* pBuf, size_t len, loff_t* offset)
{
  struct charlcd_gpio_device* lcd = pFile->private_data;
  int i;
  char out_buf[256];
  
  if (len > sizeof(out_buf) ) 
    len = sizeof(out_buf);
  
  if (copy_from_user(out_buf, pBuf, len))
    return -EFAULT;
  
  for (i=0; i<len; i++) {
    //printk("Writing '%c'\n", out_buf[i]);
    if (charlcd_send(lcd, false, out_buf[i]) < 0) {
      if (i)
        return i; //Something was written
      else
        return -EBUSY; //Nothing written, error
    }
  }
  return len;
}

static int device_open(struct inode *inode, struct file *pFile)
{
  int minor = iminor(inode);
  if (minor >=MAX_CHARLCD_GPIO) return -ENODEV;
  if (lcds[minor].lcd == NULL) return -ENODEV;
  if (lcds[minor].in_use) return -EBUSY;

//   printk("LCD Device %i opened\n", minor);
  lcds[minor].in_use = true;
  try_module_get(THIS_MODULE);
  pFile->private_data = &lcds[minor];
  charlcd_init_display(&lcds[minor]);

  return 0;
}

static void set_default_message(unsigned long minor) {
  charlcd_print(&lcds[minor], line1);
  charlcd_send (&lcds[minor], true, 0xC0);
  charlcd_print(&lcds[minor], line2);
}

static int device_release(struct inode *inode, struct file *pFile)
{
  int minor = iminor(inode);
  pFile->private_data = NULL;
  lcds[minor].in_use = false;
//   printk("LCD Device %i closed\n", minor);
  
  return 0;
}
static long device_ioctl(struct file *pFile, unsigned int ioctl_num, unsigned long ioctl_param) {
  struct charlcd_gpio_device* lcd = pFile->private_data;
  //printk("LCD IOCTL %u %lu\n", ioctl_num, ioctl_param);
  switch (ioctl_num) {
    case IOCTL_SEND_COMMAND: {
      if ( (ioctl_param&0xF0) == 0x30)
        ioctl_param = 0x20 | (ioctl_param & 0x0F);
      if (charlcd_send(lcd, true, ioctl_param) < 0)
        return -EBUSY;
      else
        return 0;
    }
    case IOCTL_READ_STATUS: {
      unsigned char data = charlcd_recv(lcd, true, 0);
      if (data<0) return -EBUSY;
      return data;
    }
      
    default: {
      return -EINVAL;
    }
  }
}


static struct file_operations fops = {
  //read: device_read,
  .write          = device_write,
  .unlocked_ioctl = device_ioctl,
  .open           = device_open,
  .release        = device_release
};


static int __init charlcd_gpio_probe(struct platform_device *pdev)
{
  struct charlcd_gpio* lcd = pdev->dev.platform_data;
  int minor;
  int i;
  for (minor=0; minor<MAX_CHARLCD_GPIO; minor++) {
    if (lcds[minor].lcd != NULL) continue;
    
    lcds[minor].lcd = lcd;
    lcds[minor].in_use = 0;
    device_create(lcd_class, NULL, MKDEV(major, minor), NULL, "charlcd%i", (minor+1) );
    
    //Init GPIO pins
    request(lcd->RW, true);
    request(lcd->RS, true);
    request(lcd->EN, true);
    for (i=4; i<8; i++) {
      request(lcd->DATA[i], false);
    }  
  
    //Init / Clear display
    charlcd_init_display(&lcds[minor]);
    set_default_message(minor);
    
    return 0;
  }
  
  return -EBUSY;
}

static int __exit charlcd_gpio_remove(struct platform_device *pdev)
{
  int minor;
  for (minor=0; minor<MAX_CHARLCD_GPIO; minor++) {
    if (lcds[minor].lcd != pdev->dev.platform_data) continue;
    
    device_destroy(lcd_class, MKDEV(major, minor));
    charlcd_init_display(&lcds[minor]);
    set_default_message(minor);
    lcds[minor].lcd = NULL;
    return 0;
  }
  return -EINVAL;
}



static struct platform_driver charlcd_gpio_driver = {
  .driver = {
    .name = "charlcd-gpio",
    .owner = THIS_MODULE,
  },
  .remove = __exit_p(charlcd_gpio_remove),
};

static int __init charlcd_gpio_init(void) {
  int ret;
  
  //Register into /dev
  major = register_chrdev(0, "charlcd", &fops);
  if (major < 0) return major;
  printk("charlcd-gpio: Registed with major number %i\n", major);
  
  lcd_class = class_create(THIS_MODULE, "charlcd");
  
  //Probe platform drivers
  ret = platform_driver_probe(&charlcd_gpio_driver, charlcd_gpio_probe);
  if (ret < 0) {
    unregister_chrdev(major, "charlcd");
    return ret;
  }
  
  return 0;
}

static void __exit charlcd_gpio_exit(void)
{
  class_unregister(lcd_class);
  class_destroy(lcd_class);
  unregister_chrdev(major, "charlcd");
  platform_driver_unregister(&charlcd_gpio_driver);
}

module_init(charlcd_gpio_init);
module_exit(charlcd_gpio_exit);

MODULE_AUTHOR("Paulo Costa <paulo@inutilfutil.com>");
MODULE_DESCRIPTION("GPIO-Driven Character LCD Driver");
MODULE_LICENSE("GPL");
