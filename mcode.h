
#include <linux/init.h>      
#include <linux/module.h>    
#include <linux/kernel.h>    
#include <linux/errno.h>     
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/types.h>  
#include <linux/kdev_t.h> 
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/pfn.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/ioctl.h>
#include <net/sock.h>
#include <net/tcp.h>
#include "mcode_ioctl.h"

#define DEVICE_NAME "mcode"    
#define CLASS_NAME  "Mcode" 

#define GPIO1_START_ADDR 0x4804C000
#define GPIO1_END_ADDR   0x4804e000
#define GPIO1_SIZE (GPIO1_END_ADDR - GPIO1_START_ADDR)
#define GPIO_SETDATAOUT 0x194
#define GPIO_CLEARDATAOUT 0x190
#define USR2 (1<<23)
#define USR0 (1<<21)
#define USR_LED USR2
#define LED0_PATH "/sys/class/leds/beaglebone:green:usr0"

#define STREND '\0'
#define DOT_ON_TIME 150
#define DASH_ON_TIME 450
#define NEXT_CHAR_WAIT 300
#define NEXT_WORD_WAIT 1050


#define CQ_DEFAULT	0

static int    num_of_dev_open = -1;//start number of devises at -1 so first dev gets count 0
static char   mcode_mutex = 0;//mutex var, concurrency management; 
static int    major_number = 0;//<device number -- initialized to place it on the .data section and not on .bss
static struct class*  mcode_class  = NULL;//< class struct pointer
static struct device* mcode_device = NULL;//< device struct pointer
static char *  message;//< pointer string to allocate memmory on kernel for string passed from userspace

char * mcodestring(int asciicode);
static int bbb_write(const char * morse);
static int send_morse(const char * str);
static int     mcode_open(struct inode *, struct file *);
static int     mcode_release(struct inode *, struct file *);
static ssize_t mcode_write(struct file *, const char *, size_t, loff_t *);
static long    mcode_ioctl (struct file *filp, unsigned int cmd, unsigned long arg);

void BBBremoveTrigger(void);
void BBBstartHeartbeat(void);
void BBBledOn(void);
void BBBledOff(void);


static volatile void *gpio_addr;
static volatile unsigned int *gpio_setdataout_addr;
static volatile unsigned int *gpio_cleardataout_addr;


ssize_t write_vaddr_disk(void *, size_t);
int setup_disk(void);
void cleanup_disk(void);
static void disable_dio(void);

static struct file * f = NULL;
static int reopen = 0;
static char *filepath = 0;
static char fullFileName[1024];
static int dio = 0;

/* the empty string, followed by 26 letter codes, followed by the 10 numeral codes, followed by the comma,
   period, and question mark.  */

char *morse_code[40] = {"",
".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",
".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",
".--","-..-","-.--","--..","-----",".----","..---","...--","....-",
".....","-....","--...","---..","----.","--..--","-.-.-.","..--.."};