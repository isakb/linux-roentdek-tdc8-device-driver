#include "tdc.h"

/*
 * Require that the computer runs a (real-time) kernel with
 * clock resolution that is at most this many nanoseconds:
 * (If no real-time kernel is running, clock resolution will
 * usually be as high as 400000 ns, in the timer!)
 */
#define HRTIMER_REQ_RES_NS 10000

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Isak Bakken");

struct tdc_device *tdc_device; /* allocated in tdc_init_module */

/*
 *
 * REGARDING THE SCHEDULER, SEE: http://www.linuxjournal.com/node/6916/print
 * for theory,
 * and also: http://www.linuxtopia.org/online_books/Linux_Kernel_Module_Programming_Guide/x1191.html
 * for example code
 */

/*
 * Our parameters which can be set at load time.
 */
int tdc_major =   TDC_MAJOR;
int tdc_minor =   0;
int tdc_nr_devs = 1;

int tdc_base_address = TDC_BASE_ADDRESS;
int tdc_buffer_size = TDC_BUFFER_SIZE;

/*
 * module_param(foo, int, 0000)
 * The first param is the parameters name
 * The second param is it's data type
 * The final argument is the permissions bits,
 * for exposing parameters in sysfs (if non-zero) at a later stage.
 */
module_param(tdc_major, int, S_IRUGO);
module_param(tdc_minor, int, S_IRUGO);
module_param(tdc_base_address, int, S_IRUGO);
MODULE_PARM_DESC(tdc_base_address,
    "The base address of the TDC Card, default: 0x320");
module_param(tdc_buffer_size, int, S_IRUGO);
MODULE_PARM_DESC(tdc_buffer_size,
    "The size of the FIFO buffer (in bytes) used for storing events between reads.");


/*
 * This function outputs information about current measurement.
 * Access it by reading the virtual file /proc/tdc_measurement.
 */
int tdc_proc_measurement(char *buf, char **start, off_t offset,
                   int len, int *eof, void *data)
{
    char *buf2 = buf;
    unsigned short i, j;
    struct timeval tv1;
    struct timespec tv2, tp;

    struct tdc_device *tdc = (struct tdc_device*)data;
    PDEBUG("FIFO real size: %d", tdc_fifo_len(tdc->fifo));

    do_gettimeofday(&tv1);
    tv2 = current_kernel_time();

    /*
     * get the true resolution of the clock, in nanoseconds.
     */
    hrtimer_get_res(CLOCK_MONOTONIC, &tp);
    buf2 += sprintf(buf2,
        "\nCLOCK_MONOTONIC time resolution: %i.%09i seconds.\n",
        (int) tp.tv_sec, (int) tp.tv_nsec);

    buf2 += sprintf(buf2, "current timer callback frequency: %lu Hz\n",
            tdc->timer.callback_rate);

    buf2 += sprintf(buf2,"TDC-card settings:\n");
    buf2 += sprintf(buf2,"t_min = %d; t_max = %d. (Unit: 0.5 ns. Default values: %d; %d)\n",
        tdc->t_min, tdc->t_max, 0, TDC_MAX_DELAY);
    buf2 += sprintf(buf2,"num_channels = %d. (Default: %d)\n",
        tdc->num_channels, TDC_MAX_NUM_CHANNELS);
    buf2 += sprintf(buf2,"com_mode = %s.\n",
        tdc->com_mode == COMMON_START ? "common start" : "common stop");

    PDEBUG("tdc ptr: %p", tdc);
    PDEBUG("FIFO real size: %d", tdc_fifo_len(tdc->fifo));
    buf2 += sprintf(buf2,"buffer contains %d bytes, and has %d bytes free.\n",
        tdc_fifo_len(tdc->fifo), tdc_fifo_spacefree(tdc->fifo));

    buf2 += sprintf(buf2,"\n");
    buf2 += sprintf(buf2,"Measurement info:\n");

    buf2 += sprintf(buf2, "com_limit: %d pulses\n",
        tdc->measurement.max_num_com_signals);

    switch (tdc->measurement.state) {
        case M_NEW:
        buf2 += sprintf(buf2,"state: M_NEW\n");
        break;

        case M_PAUSED:
        buf2 += sprintf(buf2,"state: M_PAUSED\n");
        tp = ktime_to_timespec(tdc->measurement.duration);
        buf2 += sprintf(buf2,"duration: %i.%09i s so far (not incremented while paused).\n",
            (int) tp.tv_sec, (int) tp.tv_nsec);
        break;

        case M_STARTED:
        buf2 += sprintf(buf2,"state: M_STARTED\n");
        // Calculate duration so far:
        tp = ktime_to_timespec(ktime_add(tdc->measurement.duration,
            ktime_sub(ktime_get(), tdc->measurement.time_started)));
        buf2 += sprintf(buf2,"duration: %i.%09i s, and counting...\n",
            (int) tp.tv_sec, (int) tp.tv_nsec);
        break;

        case M_STOPPED:
        buf2 += sprintf(buf2,"state: M_STOPPED\n");
        tp = ktime_to_timespec(tdc->measurement.duration);
        buf2 += sprintf(buf2,"duration: %i.%09i s in total.\n",
            (int) tp.tv_sec, (int) tp.tv_nsec);
        break;

        default: buf2 += sprintf(buf2,"state: N/A\n");
    }

    if (tdc->measurement.state != M_NEW) {
        buf2 += sprintf(buf2, "current error code: %i\n",
                tdc->measurement.error);

        buf2 += sprintf(buf2, "current COM signal rate: %lu Hz\n",
                tdc->measurement.com_rate);

        buf2 += sprintf(buf2, "current hit rate: %lu Hz (valid hits only)\n",
                tdc->measurement.hit_rate);

        buf2 += sprintf(buf2, "buffer_full: %lu times\n",
                tdc->measurement.buf_overflow);

        buf2 += sprintf(buf2, "buffer_full resulted in %lu missed events\n",
                tdc->measurement.buf_overflow_events);
        buf2 += sprintf(buf2, "buffer_full resulted in %lu missed hits\n",
                tdc->measurement.buf_overflow_hits);

        /*
         * Show info about number of singles, doubles, etc...
         */
        buf2 += sprintf(buf2, "\nNumber of singles, doubles, etc:\n");
        for (j = 1; j < TDC_MAX_NUM_HITS_PER_CHANNEL+1; ++j) {
            for (i = 0; i < tdc->num_channels; ++i) {
                if (tdc->measurement.num_hits_of_type[i][j] > 0)
                    buf2 += sprintf(buf2,"%d's on CH %d: %lu\n", j, 1+i,
                        tdc->measurement.num_hits_of_type[i][j]);
            }
        }

    }

    buf2 += sprintf(buf2,"\n");
    buf2 += sprintf(buf2,"Hit-counts for each input channel:\n");
    for (i = 0; i < tdc->num_channels; ++i) {
        buf2 += sprintf(buf2,"  CH %d: %lu\n", 1+i, tdc->measurement.num_hits[i]);
    }
    buf2 += sprintf(buf2,"   COM: %lu\n", tdc->measurement.num_com_signals);
    buf2 += sprintf(buf2,"  (COM signals without detected hits: %lu)\n",
        tdc->measurement.num_com_signals_without_hits);


    *eof = 1;
    return buf2 - buf;
}



/* Called when a process tries to open the device file, like
 * "cat /dev/tdc"
 */
static int tdc_open(struct inode *inode, struct file *filp)
{
    int retval = -ENOMEM;
    struct tdc_device *dev; // device information
    dev = container_of(inode->i_cdev, struct tdc_device, cdev);
    filp->private_data = dev; /* for other methods */

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    if (!dev->fifo) {
        PDEBUG("No FIFO!");
        goto out;
    }
    PDEBUG("FIFO size at %s:%d: %d bytes", __FILE__, __LINE__,
        tdc_fifo_len(dev->fifo));

    if (filp->f_mode & FMODE_READ) {
        if (0 < dev->nreaders++) // only allow one reader at a time
            goto fail_busy;
    }
    if (filp->f_mode & FMODE_WRITE)
        dev->nwriters++;


    retval = nonseekable_open(inode, filp);
    goto out;


fail_busy:
    up(&dev->sem);
    return -EBUSY;

out:
    up(&dev->sem);
    return retval;

}

/* Called when a process closes the device file.
 */
static int tdc_release(struct inode *inode, struct file *filp)
{
    struct tdc_device *dev = filp->private_data;

    down(&dev->sem);
    if (filp->f_mode & FMODE_READ)
        dev->nreaders--;
    if (filp->f_mode & FMODE_WRITE)
        dev->nwriters--;
    up(&dev->sem);
    return 0;
}


/* Called when a process, which already opened the dev file, attempts to
 * read from it.
 */
static ssize_t tdc_read(struct file *filp, char __user *buf,
    size_t count, loff_t *f_pos)
{
    struct tdc_device *dev = filp->private_data;
    long len = 0;
    unsigned char tmp;

    ssize_t retval = -EFAULT;

    PDEBUG("tdc_read, count: %lu, f_pos: %lu",
        (unsigned long)count,
        (unsigned long)*f_pos);

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    PDEBUG("fifo len: %d", tdc_fifo_len(dev->fifo));
    PDEBUG("fifo spacefree: %d", tdc_fifo_spacefree(dev->fifo));

    /*
     * Only read from device if there is data in buffer, or,
     * if a measurement is is started/paused, or about to start.
     * If the measurement is stopped, and there is no data in buffer,
     * consider it end of file and don't wait for more data.
     */
    while (tdc_fifo_len(dev->fifo) == 0 && dev->measurement.state != M_STOPPED)
    {
        up(&dev->sem); /* release the lock */
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
        if (wait_event_interruptible(dev->bufq,
            (tdc_fifo_len(dev->fifo) > 0 || dev->measurement.state == M_STOPPED)))
        {
            return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
        }
        /* otherwise loop, but first reacquire the lock */
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
    }

    PDEBUG("Copying byte by byte from FIFO to userspace... count is: %lu", (unsigned long)count);
    // Copy byte by byte from FIFO buffer to the buf in
    // userspace. Don't copy more than requested number
    // of bytes though!
    for (len = 0; tdc_fifo_len(dev->fifo) && count > len; len++) {
        if (tdc_fifo_getbyte(dev->fifo, &tmp)) {
            PDEBUG("Could not get byte from FIFO!");
            goto out;
        }
        if (put_user(tmp, buf+len)) {
            PDEBUG("put_user failed! len = %lu, count = %lu", len, (unsigned long)count);
            goto out;
        }
    }
    PDEBUG("Done.");
    PDEBUG("fifo_len is now: %u", tdc_fifo_len(dev->fifo));

    PDEBUG("f_pos före: %lu", (long unsigned int)*f_pos);
    *f_pos += len;
    retval = len;
    PDEBUG("f_pos efter: %lu", (long unsigned int)*f_pos);
out:
    up(&dev->sem);
    PDEBUG("tdc_read done");
    return retval;
}

/*  Called when a process writes to dev file. */
static ssize_t tdc_write(struct file *filp, const char __user *buf,
     size_t count, loff_t *f_pos)
{
    /*
     * TODO in future? Tokenize the string, separate different keywords with ';'
     * Parse each token. Use strsep()
     */

    static const short STR_LEN_MAX = 127;
    struct tdc_device *dev = filp->private_data;

    // Variables for sscanf
    char keyword[STR_LEN_MAX]; // the size must be large enough for an additional '\0'.
    int value[2];

    int i, cmd_id=TDC_CMD_INVALID, num_params=0, retval=-EINVAL;
    unsigned char *data;

    PDEBUG("tdc_write");

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    if (*f_pos != 0)
        goto out2;

    if (count >= STR_LEN_MAX) {
        PDEBUG("Too much data.");
        goto out2;
    }

    data = kzalloc(count * sizeof(*data), GFP_KERNEL);
    if (!data) {
        PDEBUG("Sorry, no memory.");
        retval = -ENOMEM;
        goto out2;
    }
    PDEBUG("data: '%s'...", data);
    if (copy_from_user(data, buf, count)) {
        PDEBUG("copy_from_user failed");
        retval = -EFAULT;
        goto out;
    }
    data[count] = '\0';
    PDEBUG("string passed to tdc_write: '%s'...", data);
    PDEBUG("count: %lu", (unsigned long)count);

    /*
     * See http://linux.about.com/library/cmd/blcmdl3_vsscanf.htm
     * for examples and details.
     *
     * %s: Matches a sequence of non-white-space characters; the next pointer
     * must be a pointer to char, and the array must be large enough to accept
     * all the sequence and the terminating NUL character.
     * The input string stops at white space or at the maximum field width,
     * whichever occurs first.
     *
     * %i: The integer is read in base 16 if it begins with `0x' or `0X',
     * in base 8 if it begins with `0', and in base 10 otherwise.
     * Only characters that correspond to the base are used.
     */
    num_params = sscanf(data, "%s %i, %i", keyword, &value[0], &value[1]) - 1;
    if (num_params < 0) {
        PDEBUG("Error: '%s'; At least a keyword is needed.", data);
        goto out;
    }

    PDEBUG("num_params: %d, keyword: %s, value[0]: %#x, value[1]: %#x",
        num_params, keyword, value[0], value[1]);

    // Compare keyword with all valid commands:
    for (i=0; i < TDC_NUM_COMMANDS; ++i) {
        if (strcmp(keyword, TDC_COMMANDS[i]) == 0) {
            PDEBUG("Keyword found: %s", TDC_COMMANDS[i]);
            cmd_id = i;
            break;
        }
    }

    switch (cmd_id) {
    case TDC_CMD_SET_CONFIG: // set_config: Initiate TDC with config data
        PDEBUG("max_delay: %d", (unsigned short)value[0]);
        dev->t_max = (unsigned short)value[0];
        if (num_params > 1) {
            PDEBUG("max_hits_per_channel: %d", (unsigned short)value[1]);
            dev->max_num_hits_per_channel = (unsigned short)value[1];
        }
        break;

    case TDC_CMD_START: // start:
        if (num_params > 0 && value[0] >= 0) {
            /*
             * First parameter tells us how many trigger pulses
             * we shall record, before automatically stopping.
             * 0 = unlimited.
             *
             * NOTE: If we have already recorded a number of COM signals
             * then we set the limit to the number of recorded COM signals
             * so far, plus the limit.
             */
            dev->measurement.max_num_com_signals = (unsigned int)value[0];
            PDEBUG("max_num_com_signals: %d", dev->measurement.max_num_com_signals);
        }
        if (tdc_start_measurement(&dev->measurement)) {
            PDEBUG("Could not start.");
            retval = -EFAULT;
            goto out;
        }
        break;

    case TDC_CMD_PAUSE: // pause
        if (tdc_pause_measurement(&dev->measurement)) {
            retval = -EFAULT;
            goto out;
        }
        break;

    case TDC_CMD_STOP: // stop
        while (tdc_stop_measurement(&dev->measurement) == -1)
        {
            up(&dev->sem); /* release the lock */
            if (filp->f_flags & O_NONBLOCK) {
                PDEBUG("retry writing stop");
                return -EAGAIN;
            }

            PDEBUG("\"%s\" writing: going to sleep\n", current->comm);
            if (wait_event_interruptible(dev->stopq,
                (dev->measurement.state == M_STOPPED)))
            {
                return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
            }
            /* otherwise loop, but first reacquire the lock */
            if (down_interruptible(&dev->sem))
                return -ERESTARTSYS;
        }

        break;

    case TDC_CMD_CLEAR: // clear: Clear data
        if (tdc_clear_data(dev)) {
            retval = -EFAULT;
            goto out;
        }
        break;

    case TDC_CMD_SET_TRIGGER_PERIOD_NS:
    // set_trigger_period_ns: Set the current trigger signal's period.
        PDEBUG("Setting up trigger pulse period.");
        if (num_params > 0) {
            if (unlikely(
                (value[0] < TDC_MIN_TRIGGER_PERIOD_NS
                    ||
                value[0] > TDC_MAX_TRIGGER_PERIOD_NS)
            )) {
                PDEBUG("Invalid update interval!");
                retval = -EINVAL;
                goto out;
            } else {
                dev->timer.callback_interval = ktime_set(0, (unsigned int)value[0]);
                dev->timer.callback_rate = 1e9/value[0];
            }
        } else {
            retval = -EINVAL;
            goto out;
        }
        break;

    case TDC_CMD_SET_TRIGGER_RATE_HZ:
    // set_trigger_rate_hz: Set the current trigger signal's pulse rate.
    /* This is the "inverse" of set_trigger_pulse_period_ns... Units: Hz*/
        PDEBUG("Setting up trigger pulse rate.");
        if (num_params > 0) {
            if (unlikely(
                (value[0] < TDC_MIN_TRIGGER_RATE_HZ
                    ||
                value[0] > TDC_MAX_TRIGGER_RATE_HZ)
            )) {
                PDEBUG("Invalid pulse rate.");
                retval = -EINVAL;
                goto out;
            } else {
                dev->timer.callback_interval = ktime_set(0, 1e9/(unsigned int)value[0]);
                dev->timer.callback_rate = value[0];
            }
        } else {
            retval = -EINVAL;
            goto out;
        }
        break;

    case TDC_CMD_SET_COM_MODE: // set_com_mode
        if (num_params != 1 || value[0] > 1) {
            retval = -EINVAL;
            goto out;
        }
        PDEBUG("Setting trigger mode: COMMON_%s", value[0] ? "START" : "STOP");
        if (value[0])
            tdc_set_com_mode(dev, COMMON_START);
        else
            tdc_set_com_mode(dev, COMMON_STOP);
        break;

    case TDC_CMD_SET_TIME_RANGE: // set_time_range
        if (num_params != 2) {
            retval = -EINVAL;
            goto out;
        }
        if (value[0] < 0 || value[0] > value[1] || value[1] > 0xffff)  {
            retval = -EINVAL;
            goto out;
        }
        PDEBUG("Setting t_min, t_max to %d, %d.", value[0], value[1]);
        dev->t_min = value[0];
        dev->t_max = value[1];
        break;

    case TDC_CMD_INVALID: // Fall through
    default:
        PDEBUG("No keyword found in string!");
        goto out;
    }
    retval = count;
out:
    kfree(data);
out2:
    up(&dev->sem);
    wake_up_interruptible(&dev->bufq);
    /* awake those who are trying to read
      from buffer.*/
    return retval;
}

struct file_operations tdc_fops = {
    .owner =    THIS_MODULE,
    .read =     tdc_read,
    .write =    tdc_write,
    .open =     tdc_open,
    .release =  tdc_release
};

/*
 * Set up the char_dev structure for this device.
 */
static void tdc_setup_cdev(struct tdc_device *dev)
{
    // This function assumes that dev->sem is already locked
    int err, devno = MKDEV(tdc_major, tdc_minor);

    cdev_init(&dev->cdev, &tdc_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &tdc_fops;

    err = cdev_add (&dev->cdev, devno, 1);
    /* Fail gracefully if need be */
    if (err) {
        printk(KERN_ERR "Error %d adding tdc", err);
    }
}


static int __init tdc_init_module(void)
{
    int result;
    struct timespec tp;
    dev_t dev = 0;

    /*
     * get the true resolution of the given clock, in nanoseconds.
     */
    hrtimer_get_res(CLOCK_MONOTONIC, &tp);
    PDEBUG("Clock resolution: %i.%09i seconds.",
        (int) tp.tv_sec, (int) tp.tv_nsec);

    if (tp.tv_sec || tp.tv_nsec > HRTIMER_REQ_RES_NS) {
        printk(KERN_ERR TDC_MODULE_NAME ": Timer resolution: %i.%09i s is inaccurate. "
        TDC_MODULE_NAME " needs accuracy of %d ns. Try Ubuntu with package 'linux-rt'.",
            (int) tp.tv_sec, (int) tp.tv_nsec, HRTIMER_REQ_RES_NS);
        return -ENOPKG; /* "Package not installed", referring to linux-rt */
    }

    /*
     * Get a range of minor numbers to work with, asking for a dynamic
     * major unless directed otherwise at load time.
     */
    if (tdc_major) {
        dev = MKDEV(tdc_major, tdc_minor);
        result = register_chrdev_region(dev, tdc_nr_devs, TDC_MODULE_NAME);
    } else {
        result = alloc_chrdev_region(&dev, tdc_minor, tdc_nr_devs,
                TDC_MODULE_NAME);
        tdc_major = MAJOR(dev);
    }
    if (result < 0) {
        printk(KERN_WARNING "tdc: can't get major %d\n", tdc_major);
        return result;
    }

    PDEBUG("Skapar tdc_device");
    tdc_device = tdc_new(tdc_base_address);
    if (!tdc_device) {
        printk(KERN_ALERT "Could not create tdc_device! Out of memory.\n");
        goto fail_no_mem;
    }
    if (tdc_device->error & E_NO_TDC_CARD) {
        // If no TDC card is detected, it is possible to give an error.
        // Otherwise all readouts from the device will be all bits high.
        PDEBUG("No TDC card found, but we ignore this.");
    }

    init_waitqueue_head(&tdc_device->bufq);
    init_waitqueue_head(&tdc_device->stopq);
    init_MUTEX(&tdc_device->sem);
    tdc_setup_cdev(tdc_device);

    create_proc_read_entry("tdc_measurement", 0, NULL, tdc_proc_measurement, (void *)tdc_device);

    PDEBUG("module loaded successfully");
    return 0; /* succeed */

fail_no_mem:
    return -ENOMEM;
}

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
static void __exit tdc_cleanup_module(void)
{
    dev_t devno = MKDEV(tdc_major, tdc_minor);

    PDEBUG("Cleaning up tdc module.");
    tdc_clear_data(tdc_device);
    cdev_del(&tdc_device->cdev);

    PDEBUG("Will now destroy tdc_device");
    tdc_destroy(tdc_device);

    /* cleanup_module is never called if registering failed */
    unregister_chrdev_region(devno, tdc_nr_devs);

    tdc_device = NULL;

    remove_proc_entry("tdc_measurement", NULL);

}

module_init(tdc_init_module);
module_exit(tdc_cleanup_module);
