#include "TDC_Device.h"

const char *TDC_DEVICE_NAME = "RoentDek TDC8 prototype card";

static struct tdc_device *tdc_card; // needed for the timer callback function


static inline int tdc_has_detected_com_event(struct tdc_device *self)
{
    return _get_bit(self, PIA1PB, 7);
}

int tdc_timer_callback(struct hrtimer *hrtimer)
{
    ktime_t now = ktime_get();
    unsigned long overruns;
    static unsigned long counter = 1;

    static unsigned long n_com_prev = 0;
    static unsigned long n_hits_prev = 0;
    int retval = HRTIMER_NORESTART;

    PDEBUG_MAYBE("Timer callback function called");

    if (!tdc_card) {
        PDEBUG("tdc_card is null!");
        return retval;
    }
    if (unlikely(&tdc_card->measurement == NULL)) {
        PDEBUG("NO MEASUREMENT!");
        return retval;
    }

    /*
     * NOTE: If the callback rate is very high, it can happen that
     * the previous callback is still executing when this callback
     * is called again. If that happens, don't try to read from the card
     * this time, and don't forward the timer (let the previous callback
     * do that instead.)
     */
    if (down_interruptible(&tdc_card->timer.lock)) {
        PDEBUG("timer_callback could not get lock in %s", __FUNCTION__);
        tdc_card->error |= 0xDEAD; // temp debug error
        return retval; // don't lock up, since lock not acquired
    }
    PDEBUG_MAYBE("timer_callback got lock");

    if (unlikely(tdc_card->measurement.state != M_STARTED)) {
        PDEBUG("Stopping timer now.");
        tdc_reset(tdc_card);
        /*
         * Wake up processes that are waiting for the callback function to
         * terminate, in order to stop the timer.
         */
        wake_up_interruptible(&tdc_card->stopq);
        goto out; // retval = HRTIMER_NORESTART;
    }

    PDEBUG_MAYBE("Checking for COM event in %s...", __FUNCTION__);
    if (tdc_has_detected_com_event(tdc_card)) {
        /*
         * Stop measurement if we have an upper limit
         * to the number of com signals we want to detect,
         * and this limit is hit.
         */
        PDEBUG("COM event detected in %s...", __FUNCTION__);
        if (tdc_card->measurement.max_num_com_signals &&
        (tdc_card->measurement.num_com_signals >=
        tdc_card->measurement.max_num_com_signals))
        {
            PDEBUG("max_num_com_signals reached. Will stop measurement.");
            tdc_card->measurement.state = M_STOPPED; // will terminate on next callback
        } else {
            tdc_card->measurement.num_com_signals++;
        }
        PDEBUG_MAYBE("COMMON START pulse registered.");

        udelay(10); // not really necessary, can be removed,
                    // which makes possible higher callback rates.

        tdc_check_for_events(tdc_card);
        if (tdc_card->has_events) {
            PDEBUG_MAYBE("Hits were found; decoding them...");
            tdc_decode_events(tdc_card);
            if (unlikely(tdc_card->error & E_TOO_MANY_HITS)) {
                PDEBUG("Too many hits %s:%d", __FILE__, __LINE__);
                tdc_card->measurement.error |= E_TOO_MANY_HITS;
            }
        } else {
            PDEBUG_MAYBE("No hits were found");
        }
        PDEBUG_MAYBE("Resetting TDC card...");
        tdc_reset(tdc_card);
        PDEBUG_MAYBE("Preparing to wait...");
        tdc_prepare_wait(tdc_card);
    } else {
        PDEBUG("COM event not found in %s...", __FUNCTION__);
    }

    now = ktime_get();
    overruns = hrtimer_forward(hrtimer, now,
        tdc_card->timer.callback_interval);

    /*
     * Update and display statistical data about the timer
     * and measurement once per second.
     */
    if (unlikely((counter >= tdc_card->timer.callback_rate))) {
        tdc_card->measurement.com_rate =
            tdc_card->measurement.num_com_signals - n_com_prev;
        PDEBUG("Calculated current COM frequency: %lu Hz",
            tdc_card->measurement.com_rate);

        tdc_card->measurement.hit_rate =
            tdc_card->measurement.num_valid_hits_sum - n_hits_prev;
        PDEBUG("Calculated current hit frequency: %lu Hz",
            tdc_card->measurement.hit_rate);

        n_com_prev = tdc_card->measurement.num_com_signals;
        n_hits_prev = tdc_card->measurement.num_valid_hits_sum;

        PDEBUG("overruns: %lu", overruns);
        counter = 1;
    } else {
        counter++;
    }
    retval = HRTIMER_RESTART;
    PDEBUG_MAYBE("Returning HRTIMER_RESTART from %s.", __FUNCTION__);
out:
    up(&tdc_card->timer.lock);
    return retval;
}


struct tdc_device *tdc_new(unsigned int _baseport)
{
    struct tdc_device *tdc;

    PDEBUG("Begär åtkomst till portar 0x%x - 0x%x...", _baseport, _baseport+TDC_NUM_PORTS);
    if (request_region(_baseport, TDC_NUM_PORTS, TDC_DEVICE_NAME) == NULL) {
        PDEBUG("Kunde inte få åtkomst till 0x%x och framåt...", _baseport);
        return NULL;
    }

    PDEBUG("Skapar TDC!");
    tdc = tdc_card =
    kzalloc( sizeof(*tdc), GFP_KERNEL );
    if (!tdc) return NULL;

    tdc->baseport = _baseport;
    tdc->num_channels = TDC_MAX_NUM_CHANNELS;
    tdc->max_num_hits_per_channel = TDC_MAX_NUM_HITS_PER_CHANNEL;

    tdc->fifo = tdc_fifo_new(TDC_BUFFER_SIZE);
    if (!tdc->fifo) {
        PDEBUG("Could not create tdc->fifo in tdc_new");
        kfree(tdc);
        return NULL;
    }

    /* Default values */
    tdc->t_min = 0;
    tdc->t_max = TDC_MAX_DELAY;
    tdc->max_num_hits_per_channel = TDC_MAX_NUM_HITS_PER_CHANNEL;

    tdc_set_com_mode(tdc, COMMON_START);

    tdc->timer.callback_interval = ktime_set(0, TDC_DEFAULT_COM_PERIOD_NS);
    tdc->timer.callback_rate = 1e9/TDC_DEFAULT_COM_PERIOD_NS;
    init_MUTEX(&tdc->timer.lock);
    // Create the callback timer:
    hrtimer_init(&(tdc->timer.hrtimer),
        /*
         * CLOCK_MONOTONIC is guaranteed always to
         * move forward in time. It starts at zero when the
         * system boots and increases monotonically from there.
         *
         * CLOCK_REALTIME matches the current real-world time.
         *
         * - Source: http://lwn.net/Articles/167897/
         */
        CLOCK_MONOTONIC,

        /*
         * HRTIMER_MODE_REL - Time value is relative to now
         * */
        HRTIMER_MODE_REL
    );
    tdc->timer.hrtimer.function = (void *)&tdc_timer_callback;

    /* Try to read a longword from baseport, see if it behaves as expected.
     *   a TDC card will most likely output: 0x????0203
     *   a computer without ISA bus defaults to all bits high
     */
    if (inl(tdc->baseport) == 0xffffffff) {
        printk(KERN_WARNING "TDC card %s seems to be missing!\n",
            TDC_DEVICE_NAME);
        tdc->error |= E_NO_TDC_CARD;
    }

    tdc_setup(tdc);

    return tdc;
}

/*
 * Empty out the tdc device data; must be called with the device
 * semaphore held.
 */
int tdc_clear_data(struct tdc_device *dev)
{
    PDEBUG("Clearing data...");
    if (&dev->measurement) {
        PDEBUG("Measurement exists");
        if (tdc_reset_measurement(&dev->measurement)) {
            PDEBUG("Could not reset measurement.");
            return -1;
        }
    }
    PDEBUG("Resetting FIFO...");
    tdc_fifo_reset(dev->fifo);
    PDEBUG("tdc_clear_data done.");
    return 0;
}

int tdc_reset_measurement(struct tdc_measurement *measurement)
{
    PDEBUG("tdc_reset_measurement called");
    /*
     * Don't allow this unless measurment is stopped.
     * No timer can be running.
     */
    // Cancel all running timers etc...
    if (hrtimer_active(&(tdc_card->timer.hrtimer)) &&
        (hrtimer_try_to_cancel(&(tdc_card->timer.hrtimer)) == -1))
    {
        PDEBUG("There is a timer, and it is currently executing the callback "
            "function, and cannot be stopped!");
        return -1;
    }
    PDEBUG("tdc_reset_measurement done");
    memset(measurement, 0, sizeof(*measurement));
    measurement->state = M_NEW;
    tdc_reset(tdc_card);
    return 0;
}

void tdc_set_com_mode(struct tdc_device *self, enum com_mode mode)
{
    self->com_mode = mode;
    /* if bit 5 in PIA2PB is high then mode is common start else common stop */
}

int tdc_start_measurement(struct tdc_measurement *measurement)
{
    int res;

    switch (measurement->state) {
        case M_STOPPED:
            tdc_reset_measurement(measurement);
            // fall through
        case M_NEW:
            PDEBUG("tdc_start_measurement, starting new measurement.");
            tdc_setup(tdc_card);
            measurement->duration = ktime_set(0,0);
            break;

        case M_PAUSED:
            PDEBUG("Measurement was paused, is now restarting.");
            break;

        case M_STARTED:
            PDEBUG("Measurement is already started!");
            return -1;
            break;

        default:
            PDEBUG("Unknown state");
    }

    measurement->state = M_STARTED;

    // Prepare for measurement
    tdc_prepare_wait(tdc_card);

    measurement->time_started = ktime_get();

    res = hrtimer_start(&(tdc_card->timer.hrtimer),
        tdc_card->timer.callback_interval,
        HRTIMER_MODE_REL);

    PDEBUG("Timer started? %s", res ? "yes" : "no");

    return res;

}

int tdc_pause_measurement(struct tdc_measurement *measurement)
{
    int res;
    if (measurement->state != M_STARTED)
        return -1;

    measurement->state = M_PAUSED;

    // Cancel all running timers etc...
    if (&(tdc_card->timer.hrtimer)) {
        PDEBUG("Trying to stop timer...");
        res = hrtimer_try_to_cancel(&(tdc_card->timer.hrtimer));
        PDEBUG("res = %d", res);
        #if DEBUG
        switch (res) {
            case -1:
            PDEBUG("The timer is currently executing the callback "
                "function, and cannot be stopped!");
            break;

            case 0:
            PDEBUG("Timer is not active, no need to stop.");
            break;

            case 1:
            PDEBUG("Timer was active, and was stopped.");
            break;

            default:
            PDEBUG("hrtimer_try_to_cancel returned an unknown value!");
            return -1;
        }
        #endif
    }

    measurement->duration = ktime_add(measurement->duration,
        ktime_sub(ktime_get(), measurement->time_started));
    return 0;
}

int tdc_stop_measurement(struct tdc_measurement *measurement)
{
    int res;
    PDEBUG("tdc_stop_measurement called.");

    if (measurement == NULL) {
        PDEBUG("There is no measurement!");
        return 0;
    }

    if (&measurement->state == NULL) {
        PDEBUG("Measurement state is null!");
        return -2;
    }

    if (measurement->state != M_PAUSED
    && measurement->state != M_STARTED) {
        PDEBUG("Measurement neither running nor paused!");
        return 0;
    }
    measurement->state = M_STOPPED;
    // Cancel all running timers etc...
    if (&(tdc_card->timer.hrtimer)) {
        PDEBUG("Trying to stop timer...");
        res = hrtimer_try_to_cancel(&(tdc_card->timer.hrtimer));
        PDEBUG("res = %d", res);
        #if DEBUG
        switch (res) {
            case -1:
            PDEBUG("The timer is currently executing the callback "
                "function, and cannot be stopped!");
            return -1;
            break;

            case 0:
            PDEBUG("Timer is not active, no need to stop.");
            break;

            case 1:
            PDEBUG("Timer was active, and was stopped.");
            break;

            default:
            PDEBUG("hrtimer_try_to_cancel returned an unknown value!");
            return -1;
        }
        #endif
    }
    measurement->duration = ktime_add(measurement->duration,
        ktime_sub(ktime_get(), measurement->time_started));
    return 0;

}

void tdc_destroy(struct tdc_device *self)
{
    if (!self) {
        PDEBUG("tdc_destroy: self is NULL");
        return;
    }

    // Stop all timers etc if a measurement is running
    while (tdc_stop_measurement(&self->measurement) == -1) {
        PDEBUG("Trying to stop measurement in tdc_destroy...");
        schedule();

    }
    PDEBUG("Measurement is stopped now...");

    tdc_clear_data(self);
    PDEBUG("Data is cleared now...");

    tdc_fifo_destroy(self->fifo);
    PDEBUG("Fifo buffer is destroyed now...");

    if (!self->baseport) {
        PDEBUG("tdc_destroy: self->baseport is NULL!");
        return;
    }

    PDEBUG("Releasing self->baseport etc: 0x%x - 0x%x", self->baseport, self->baseport + TDC_NUM_PORTS);
    release_region(self->baseport, TDC_NUM_PORTS);
    kfree(self);
};



int tdc_setup(struct tdc_device *self)
{
    unsigned int cfgH, cfgL;
    self->is_initialized = 0;
    self->error = 0;

    // setup max wait time 8ns + bits(4-15)*0.5 ns, and number of Hits bits 0-3 (0 = 16 hits)
    cfgH = self->t_max >> 8;
    cfgL = ((self->t_max % 0xff) & 0xf0) | self->max_num_hits_per_channel;

    // NOTE: The comments to the right are from RoentDek's documentation
    // but seems to be mixed up! It's probably vice versa.
    _outb(self, PIA1PA, cfgH);    // set number of Hits bits 0-3 (0 = 16 hits)
    _outb(self, PIA2PA, cfgL);    // setup max wait time 8ns + bits(4-15)*0.5 ns

    // MTD133B initialization
    _outb(self, CTRL1, 0xc3); // setup 8285 IO chips
    _outb(self, CTRL2, 0xc1);

    // Configuring the MTD133B chip
    _outb(self, PIA2PB, 0x11 | self->com_mode); // 00010001 - ENABLE, P.in* go high

    return self->error;
}


int tdc_prepare_wait(struct tdc_device *self)
{
    unsigned int cfgH, cfgL;
    self->is_initialized = 0;
    self->error = 0;

    /* NOTE: It is necessary to send the cfgH and cfgL each time
     * we want to wait for a new COM signal apparently. Otherwise
     * new hits will not be detected after approx 3 seconds of acquring. */

    // setup max wait time 8ns + bits(4-15)*0.5 ns, and number of Hits bits 0-3 (0 = 16 hits)
    cfgH = self->t_max >> 8;
    cfgL = ((self->t_max % 0xff) & 0xf0) | self->max_num_hits_per_channel;

    // NOTE: The comments to the right are from RoentDek's documentation
    // but seems to be mixed up! It's probably vice versa.
    _outb(self, PIA1PA, cfgH);    // set number of Hits bits 0-3 (0 = 16 hits)
    _outb(self, PIA2PA, cfgL);    // setup max wait time 8ns + bits(4-15)*0.5 ns

    udelay(10); // probably not necessary

    // Initialize

    // Reset TDC chip
    _outb(self, PIA2PB, 0x19 | self->com_mode); // 00X11001 - enable goes high, reset goes high
    if (self->com_mode == COMMON_STOP)
        _outb(self, PIA2PB, 0x51); // 01010001 - common stop trig goes high, reset goes low
    _outb(self, PIA2PB, 0x10 | self->com_mode); // 00X10000 - reset goes low, enable goes low

    if (!self->error) {
        self->is_initialized = 1;
    } else {
        PDEBUG("in tdc_prepare_wait, self->error is: %d", self->error);
    }

    return self->error;
}

int tdc_check_for_events(struct tdc_device *self)
{
    int i;

    #if DEBUG
    if (!self->is_initialized) {
        PDEBUG("TDC_Device is not initialized in tdc_check_for_events");
        return -1;
    }
    #endif

    // MTD133B acquisition and readout

    // Set Enable* leave rest unchanged
    _outb(self, PIA2PB, 0x11 | self->com_mode); // 00X10001
    // 3 pulses on RCLK*
    for (i=0; i<3; ++i) {
        _outb(self, PIA2PB, 0x91 | self->com_mode); // 10X10001
        _outb(self, PIA2PB, 0x11 | self->com_mode); // 00X10001
    }
    // Disable p.in*
    _outb(self, PIA2PB, 0x01 | self->com_mode); // 00X00001

    // Get status and test for p.out*
    i = _inb(self, PIA1PB);
    PDEBUG_MAYBE("Testing for p.out* i is 0x%x", i);
    self->has_events = (i & P_OUT);
    if (!self->has_events)
        self->measurement.num_com_signals_without_hits += 1;

    return self->error;
}


// MTD133B acquisition and readout, continued...
int tdc_decode_events(struct tdc_device *self)
{
    struct event_cache *cache = &self->measurement.cache;
    unsigned short channel, delay, num_hits_sum=0;
    int status, retval = 0;

    #if DEBUG
    if (!self->is_initialized) {
        PDEBUG("TDC_Device is not initialized in tdc_decode_events");
        return -1;
    }
    #endif

    while (self->has_events) {
        PDEBUG_MAYBE("Will decode the hits for this event. Number of hits: %d", cache->num_hits_sum);

        _outb(self, PIA2PB, 0x81 | self->com_mode); // 10X00001
        _outb(self, PIA2PB, 0x01 | self->com_mode); // 00X00001

        // get TDC data
        delay = (_inb(self, PIA1PA) << 8) | _inb(self, PIA2PA);

        udelay(10);
        status = _inb(self, PIA1PB); // bit 4,3,2 talar om vilken kanal som eventet registrerats på
        channel = (status & 0x1C) >> 2; // ch 0 - 7 är möjliga kanaler. 0x1c = 00011100b

        // Test if more hits are available after these?
        self->has_events = status & P_OUT;

        PDEBUG_MAYBE("\tCH%d: %d * 0.5 ns", channel+1, (int)delay);

        self->measurement.num_hits_sum++;
        num_hits_sum++;

        if (likely(cache->ch[channel].num_hits < self->max_num_hits_per_channel))
        {
            if (likely(self->t_min <= delay && delay <= self->t_max)) {
                PDEBUG("Valid hit.");
                self->measurement.num_hits[channel]++;
                cache->ch[channel].hits[cache->ch[channel].num_hits] = delay;
            } else {
                PDEBUG("Hit not within valid t-range");
                self->measurement.num_invalid_hits[channel]++;
            }
            cache->ch[channel].num_hits++;
            cache->num_hits_sum++;
            self->measurement.num_valid_hits_sum++;

        } else {
            // This is not supposed to happen, but it WILL happen if
            // the module is loaded on a computer with no ISA bus...
            PDEBUG("Event overflow on channel %d: event #%d, (max_num_hits_per_channel is %d)",
                channel, cache->ch[channel].num_hits, self->max_num_hits_per_channel);
            if (unlikely(self->max_num_hits_per_channel * self->num_channels < num_hits_sum)) {
                PDEBUG("Way too many hits detected. Exiting decoding loop.");
                // This will happen on a computer that doesn't have a TDC card, since all ports
                // will always be high by default in that case...
                retval = E_TOO_MANY_HITS;
                self->measurement.error |= self->error |= retval;
                // Test if more data is available:
                self->has_events = 0;
                goto out;
            }

        }

    }

    retval = 0;

out:
    retval |= tdc_add_hits_to_fifo(self);
    return retval; // todo: bättre return value?
}

int tdc_reset(struct tdc_device *self)
{
    #if DEBUG
    if (!self->is_initialized) {
        PDEBUG("TDC_Device is not initialized in tdc_reset");
        return -1;
    }
    #endif
    // Reenable P.in*
    _outb(self, PIA2PB, 0x81 | self->com_mode); // 10X00001
    _outb(self, PIA2PB, 0x11 | self->com_mode); // 00X10001

    self->is_initialized = 0;
    return self->error;
}


int tdc_add_hits_to_fifo(struct tdc_device *self)
{
    struct event_cache *cache = &self->measurement.cache;
    unsigned short num, ch, hit, *delay;

    if (!self->fifo) {
        PDEBUG("No FIFO exists!");
        return -ENOMEM;
    }

    if (down_interruptible(&self->sem))
        return -ERESTARTSYS;

    if (tdc_fifo_spacefree(self->fifo) == 0)
        goto fail_bufsize;

    #if DEBUG_DETAILED
    PDEBUG("Will try to add hits to FIFO. (free space: %d)",
        tdc_fifo_spacefree(self->fifo));
    #endif

    num = cache->num_hits_sum; // How many hits belong to this event?

    // Is there enough space in the FIFO to store all bytes
    // needed for this many hits?
    if (tdc_fifo_spacefree(self->fifo) < 1 + (3 * num))
        goto fail_bufsize;

    /*
     * The first byte tells how many hits that was detected with this COM event
     */
    tdc_fifo_putbyte(self->fifo, GET_BYTE(0, num));

    for (ch = 0; ch < self->num_channels; ch++) {
        num = cache->ch[ch].num_hits;
        if (num <= 0)
            continue;
        self->measurement.num_hits_of_type[ch][num]++;
        for (hit = 0; hit < num; hit++) {
            /* Next byte represents the channel no (from 0-7)
               (Max 8 channels, so one byte is enough). */
            tdc_fifo_putbyte(self->fifo, GET_BYTE(0, ch));

            delay = &cache->ch[ch].hits[hit];
            /* Next two bytes (16 bits) gives the delay
               using Big Endian ordering. */
            tdc_fifo_putbyte(self->fifo, GET_BYTE(0, *delay));
            tdc_fifo_putbyte(self->fifo, GET_BYTE(1, *delay));
        }
    }
    memset(cache, 0, sizeof(*cache));
    up(&self->sem);
    wake_up_interruptible(&self->bufq);  /* awake buffer readers */
    return 0;

fail_bufsize:
    up(&self->sem);
    #if DEBUG_DETAILED
    PDEBUG("FIFO is too full in tdc_write_events_to_fifo");
    PDEBUG("tdc_fifo_spacefree : %d", tdc_fifo_spacefree(self->fifo));
    PDEBUG("tdc_fifo_len : %d", tdc_fifo_len(self->fifo));
    #endif
    self->measurement.buf_overflow++;
    self->measurement.buf_overflow_events++;
    self->measurement.buf_overflow_hits += cache->num_hits_sum;
    return -ENOSPC;
}

void _outb(struct tdc_device *self, enum port _port, unsigned int val)
{
    #if DEBUG_IO
    PDEBUG("Writing 0x%x to 0x%x.", val, _port + self->baseport);
    #endif
    outb_p(val, _port + self->baseport);
    //outb(val, _port + self->baseport); // this one actually works as well, faster    
}

unsigned int _inb(struct tdc_device *self, enum port _port)
{
    unsigned int retval;
    retval = inb(_port + self->baseport);
    #if DEBUG_IO
    PDEBUG("Reading from 0x%x, value is: 0x%x", (_port + self->baseport), retval);
    #endif
    return retval;
}

unsigned int _get_bit(struct tdc_device *self, volatile enum port _port, short bit)
{
    unsigned int retval = (_inb(self, _port) & (1 << bit));
    #if DEBUG_IO
    PDEBUG("Getting bit %d, value is: %d", bit, retval);
    #endif
    return retval;
}
