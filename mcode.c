#include "mcode.h"


MODULE_LICENSE("GPL");       ///< The license type -- this affects runtime behavior
MODULE_AUTHOR("Jorge Lima");       ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("Linux Driver for Blinking LED USR3 with Mrse code");  ///< The description -- see modinfo
MODULE_VERSION("1.0");              ///< The version of the module

/***************************
//file operations for driver
***************************/
static struct file_operations mcode_fops =
{
   .open = mcode_open,
   .write = mcode_write,
   .release = mcode_release,
   .unlocked_ioctl = mcode_ioctl,
};



/***************
//init function
***************/
static int __init mcode_init(void){
   printk(KERN_ALERT "mcode: Initializing driver\n");

   //get major number
   major_number = register_chrdev(0, DEVICE_NAME, &mcode_fops);
   if (major_number<0)
   {
      printk(KERN_ALERT "mcode: failed to register a major number\n");
      major_number = -1;
      return major_number;
   }
   printk(KERN_ALERT "mcode: driver registered with major number %d\n", major_number);

   // Register device class
   mcode_class = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(mcode_class)){                
      unregister_chrdev(major_number, DEVICE_NAME);
      printk(KERN_ALERT "mcode: failed to register device class\n");
      return PTR_ERR(mcode_class);          
   }

   // Register the device driver
   mcode_device = device_create(mcode_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
   if (IS_ERR(mcode_device)){               
      class_destroy(mcode_class);
      unregister_chrdev(major_number, DEVICE_NAME);
      printk(KERN_ALERT "mcode: failed to create the device\n");
      return PTR_ERR(mcode_device);
   }

   return 0;
}



/***********************
****** exit device *****
***********************/
static void __exit mcode_exit(void){
   device_destroy(mcode_class, MKDEV(major_number, 0));     
   class_unregister(mcode_class);                          
   class_destroy(mcode_class);                             
   unregister_chrdev(major_number, DEVICE_NAME);             
   printk(KERN_ALERT "mcode: device driver removed from kernel\n");
}



/**********************
//device open function
***********************/
static int mcode_open(struct inode *inodep, struct file *filep){
   if(!mcode_mutex)mcode_mutex++;//lock devise mcode_mutex
   num_of_dev_open++;
   return 0;
}



/******************************
//device write
******************************/
static ssize_t mcode_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
  if(mcode_mutex <= 1)//not in use by other process
  {    
    if (message) kfree(message);

    message = kmalloc(len, GFP_KERNEL);//allocate len+1 bytes (char = 1 byte)

    if (!message) return -1;

    if (copy_from_user(message, buffer, len))//copy not sucsessfull
    {
      kfree(message);
      return -1;
    }

    gpio_addr = ioremap(GPIO1_START_ADDR, GPIO1_SIZE);
    if(!gpio_addr) 
    {
      printk (KERN_ERR "mcode: ERROR: Failed to remap memory for GPIO Bank 1.\n");
      kfree(message);
      return -1;
    }

    gpio_setdataout_addr   = gpio_addr + GPIO_SETDATAOUT;
    gpio_cleardataout_addr = gpio_addr + GPIO_CLEARDATAOUT;

    BBBremoveTrigger();
    BBBledOff();   
    send_morse(message);
    kfree(message);
    iounmap(gpio_addr);//release memmory from ioremap
    return len;
  }
  else
  {
    return -1;
  }
}


/********************
/device release
********************/
static int mcode_release(struct inode *inodep, struct file *filep)
{
   if(mcode_mutex)mcode_mutex--;
   num_of_dev_open--;
   printk(KERN_ALERT "mcode: device closed\n");
   BBBstartHeartbeat();
   return 0;
}


/*
 * ioctl has 2 commads, and always gets passed a NULL argument
 * DEVICE_IN_USE_IOCTL checks the status of the mcode_mutex
 * NUMBER_OF_DEVICES_IOCTL returns the number of process using the devise, starting at 0 if 1 is using it, if unused -1
 * */
static long    mcode_ioctl (struct file *filp, unsigned int cmd, unsigned long arg){
    char ret = 0;
    switch(cmd){
        case DEVICE_IN_USE_IOCTL:
            ret = mcode_mutex;
            break;
        case NUMBER_OF_DEVICES_IOCTL:
            ret = num_of_dev_open;
            break;
        default:
            ret = -1;
            break;
    }
    
    return ret;
}



/****************************************
********** send morse code to LED *******
****************************************/
static int send_morse(const char * str)
{
  if (str != NULL) 
  {
    int index = 0;

    while (str[index] != STREND) 
    {
      if (str[index] == ' ') 
      {
        msleep(NEXT_WORD_WAIT);
        index++;
      } 
      else 
      {
        if (!bbb_write(mcodestring(str[index]))) 
        {
          /*wait for next char*/
          msleep(NEXT_CHAR_WAIT);
          index++;
        } 
        else 
        {
          return -1; /*error occurred */
        }
      }
    }
    return 0;
  } 
  else 
  {
    return -1; /* error occurred */
  }
}


/********************************************
*********** write to LED hardware ***********
********************************************/
static int bbb_write(const char * morse)
{
  int on = 0;
  int index = 0;

  msleep(DASH_ON_TIME);

  while (morse[index] != STREND) 
  {
    if (morse[index] == '.') 
    {
      BBBledOn();
      on = 1;
      msleep(DOT_ON_TIME);
    }
    if (morse[index] == '-') 
    {
      BBBledOn();
      on = 1;
      msleep(DASH_ON_TIME);
    }
    if (on) 
    {
      BBBledOff();
      on = 0;
    }

    /*next morse symbol wait*/
    msleep(DOT_ON_TIME);
    index++;
  }
  return 0;
}


/********************************************
****** convert char to morse string *********
********************************************/
char * mcodestring(int asciicode)
{
   char *mc;   // this is the mapping from the ASCII code into the mcodearray of strings.

   if (asciicode > 122)  // Past 'z'
      mc = morse_code[CQ_DEFAULT];
   else if (asciicode > 96)  // Upper Case
      mc = morse_code[asciicode - 96];
   else if (asciicode > 90)  // uncoded punctuation
      mc = morse_code[CQ_DEFAULT];
   else if (asciicode > 64)  // Lower Case
      mc = morse_code[asciicode - 64];
   else if (asciicode == 63)  // Question Mark
      mc = morse_code[39];    // 36 + 3
   else if (asciicode > 57)  // uncoded punctuation
      mc = morse_code[CQ_DEFAULT];
   else if (asciicode > 47)  // Numeral
      mc = morse_code[asciicode - 21];  // 27 + (asciicode - 48)
   else if (asciicode == 46)  // Period
      mc = morse_code[38];  // 36 + 2
   else if (asciicode == 44)  // Comma
      mc = morse_code[37];   // 36 + 1
   else
      mc = morse_code[CQ_DEFAULT];
   return mc;
}


/**************************************
******* remove trigger (heartbit) *****
**************************************/
void BBBremoveTrigger()
{
  // remove the trigger from the LED
  int err = 0;
  
  strcpy(fullFileName, LED0_PATH);
  strcat(fullFileName, "/");
  strcat(fullFileName, "trigger");
  filepath = fullFileName; // set for disk write code
  err = setup_disk();
  err = write_vaddr_disk("none", 4);
  cleanup_disk();
}

/**************************************
******** restore heartbit *************
**************************************/
void BBBstartHeartbeat()
{
  // start heartbeat from the LED
  int err = 0;

  strcpy(fullFileName, LED0_PATH);
  strcat(fullFileName, "/");
  strcat(fullFileName, "trigger");
  filepath = fullFileName; // set for disk write code
  err = setup_disk();
  err = write_vaddr_disk("heartbeat", 9);
  cleanup_disk();
}


/**********************************
***** turn LED ON *****************
**********************************/
void BBBledOn()
{
*gpio_setdataout_addr = USR_LED;
}


/**********************************
***** turn LED OFF *****************
**********************************/
void BBBledOff()
{
*gpio_cleardataout_addr = USR_LED;
}

/*********************************
***** disabkle dio ***************
*********************************/
static void disable_dio() 
{
   dio = 0;
   reopen = 1;
   cleanup_disk();
   setup_disk();
}

/*********************************
****** disk setup ****************
*********************************/
int setup_disk() 
{
   mm_segment_t fs;
   int err;

   fs = get_fs();
   set_fs(KERNEL_DS);
	
   if (dio && reopen) {	
      f = filp_open(filepath, O_WRONLY | O_CREAT | O_LARGEFILE | O_SYNC | O_DIRECT, 0444);
   } else if (dio) {
      f = filp_open(filepath, O_WRONLY | O_CREAT | O_LARGEFILE | O_TRUNC | O_SYNC | O_DIRECT, 0444);
   }
	
   if(!dio || (f == ERR_PTR(-EINVAL))) {
      f = filp_open(filepath, O_WRONLY | O_CREAT | O_LARGEFILE | O_TRUNC, 0444);
      dio = 0;
   }
   if (!f || IS_ERR(f)) {
      set_fs(fs);
      err = (f) ? PTR_ERR(f) : -EIO;
      f = NULL;
      return err;
   }

   set_fs(fs);
   return 0;
}


/*******************************************
********* clean up disk ********************
*******************************************/
void cleanup_disk() 
{
   mm_segment_t fs;

   fs = get_fs();
   set_fs(KERNEL_DS);
   if(f) filp_close(f, NULL);
   set_fs(fs);
}


/*****************************************
***** write vaddr disk *******************
*****************************************/
ssize_t write_vaddr_disk(void * v, size_t is)
{
  mm_segment_t fs;

   ssize_t s;
   loff_t pos;

   fs = get_fs();
   set_fs(KERNEL_DS);
	
   pos = f->f_pos;
   s = vfs_write(f, v, is, &pos);
   if (s == is)
   {
      f->f_pos = pos;
   }					
   set_fs(fs);
   if (s != is && dio) 
   {
      disable_dio();
      f->f_pos = pos;
      return write_vaddr_disk(v, is);
   }
   return s;
}




module_init(mcode_init);
module_exit(mcode_exit);