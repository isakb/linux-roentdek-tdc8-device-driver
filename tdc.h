#ifndef _TDC_H_
#define _TDC_H_

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>    /* printk() */
#include <linux/slab.h>      /* kmalloc() */
#include <linux/fs.h>        /* everything... */
#include <linux/errno.h>     /* error codes */
#include <linux/types.h>     /* size_t */
#include <linux/proc_fs.h>
#include <linux/cdev.h>

#include <linux/capability.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/sched.h>

#include <asm/uaccess.h>    /* copy_*_user */

#include "tdc_fifo.h"
#include "tdc_common.h"
#include "TDC_Device.h"

/*
 * Name of module, as it appears in /proc/devices
 */
#define TDC_MODULE_NAME "tdcmod"


/*
 * Define command strings for the valid commands in tdc_write
 */
static const char *TDC_COMMANDS[] = {
    "set_config",
    "start",
    "pause",
    "stop",
    "clear",
    "set_trigger_period_ns",
    "set_trigger_rate_hz",
    "set_com_mode",
    "set_time_range"
};

enum {
/* Default cmd_id when user passes invalid string to tdc_write: */
    TDC_CMD_INVALID = -1,
/* TDC command_ids for the respective commands in TDC_COMMANDS.
 * The order must match elements in array TDC_COMMANDS. */
    TDC_CMD_SET_CONFIG = 0,
    TDC_CMD_START,
    TDC_CMD_PAUSE,
    TDC_CMD_STOP,
    TDC_CMD_CLEAR,
    TDC_CMD_SET_TRIGGER_PERIOD_NS,
    TDC_CMD_SET_TRIGGER_RATE_HZ,
    TDC_CMD_SET_COM_MODE,
    TDC_CMD_SET_TIME_RANGE,
/* Finally, a counter that must match the number of commands
 * in the array TDC_COMMANDS: */
    TDC_NUM_COMMANDS
};


/*
 * The different configurable parameters
 */
extern int tdc_major;
extern int tdc_nr_devs;
extern int tdc_base_address;
extern int tdc_buffer_size;

/*
 * Prototypes for shared functions
 */
static int __init tdc_init_module(void);
static void __exit tdc_cleanup_module(void);

static ssize_t tdc_read(struct file *filp, char __user *buf, size_t count,
                   loff_t *f_pos);
static ssize_t tdc_write(struct file *filp, const char __user *buf, size_t count,
                    loff_t *f_pos);

static void tdc_setup_cdev(struct tdc_device *dev);
static int tdc_open(struct inode *inode, struct file *filp);
static int tdc_release(struct inode *inode, struct file *filp);

#endif /* _TDC_H_ */
