#ifndef _TDC_COMMON_H_
#define _TDC_COMMON_H_

#include <linux/init.h>
#include <linux/module.h>

#include <linux/cdev.h>
#include <linux/sched.h>

#include "tdc_fifo.h"

#define DEBUG_DETAILED 0        /* if detailed info about TDC is wanted */

/* Debug alias, this method is copied from LDD3 code: */
#undef PDEBUG             /* undef it, just in case */
#ifdef TDC_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_ALERT "tdc: " fmt "\n", ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt "\n", ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif
#undef PDEBUGG
#define PDEBUGG(fmt, args...) /* nothing: it's a placeholder */


#ifndef DEBUG
#define DEBUG 1 /* make this 0 if debug info is not wanted */
#endif


#undef PDEBUG_MAYBE
#ifdef DEBUG_DETAILED
  #define PDEBUG_MAYBE(fmt, args...) PDEBUG( fmt , ## args)
#else
  #define PDEBUG_MAYBE(fmt, args...) /* nothing: it's a placeholder */
#endif



#define TDC_BASEPORT 0x320  /* default baseport of TDC card */
#define TDC_NUM_PORTS 8     /* number of subsequent ports used by TDC card */




#define TDC_MAX_DELAY 0xffff // 65535 * 0.5 ns
#define TDC_MAX_NUM_HITS_PER_CHANNEL 16
#define TDC_MAX_NUM_CHANNELS          8

/* Error codes used by TDC_Device */
#define E_NO_TDC_CARD 1
#define E_NO_COM 2
#define E_TOO_MANY_HITS 4
#define E_TOO_MANY_EVENTS 8

/*
 * Limit the callback rate to safe intervals so the user does not
 * accidentally make the timer callback execute on each clockcycle.
 * These values are empirically determined estimates.
 */
#define TDC_MAX_TRIGGER_RATE_HZ 100000
#define TDC_MIN_TRIGGER_RATE_HZ 1
#define TDC_MAX_TRIGGER_PERIOD_NS 1e9/TDC_MIN_TRIGGER_RATE_HZ
#define TDC_MIN_TRIGGER_PERIOD_NS 1e9/TDC_MAX_TRIGGER_RATE_HZ

//#define MAX_WAIT_CYCLES 100000 // TODO: recode module so the loop using this value disappears

/* Returns the value of the nr:th byte in var of bigger data type like word */
#define GET_BYTE(nr, var) ( ((var) & (0xff << (8*(nr)))) >> (8*(nr)) )

#define TDC_DEFAULT_COM_PERIOD_NS 100000 /* 100 µs ~ 10kHz */

#ifndef TDC_BASE_ADDRESS
#define TDC_BASE_ADDRESS TDC_BASEPORT
#endif

#ifndef TDC_BUFFER_SIZE
#define TDC_BUFFER_SIZE 0xfffff // 1 MB
#endif

#ifndef TDC_EVENT_TIMEOUT
#define TDC_EVENT_TIMEOUT 10000
#endif

#ifndef TDC_MAJOR
#define TDC_MAJOR 0   /* dynamic major by default */
#endif

/* The number of TDC devices that can be used at the same time in a PC. */
#ifndef TDC_NR_DEVS
#define TDC_NR_DEVS 1
#endif

enum com_mode {
    COMMON_STOP=0,
    COMMON_START=0x20
};

typedef enum {
    M_NEW = 0,
    M_STARTED,
    M_PAUSED,
    M_STOPPED
} tdc_measurement_state;

/* Card "registers"; actually port adresses. */
enum port {
       PIA1PA = 0, /* + baseport... */
       PIA1PB = 1,
       PIA2PA = 4,
       PIA2PB = 5,
       CTRL1 = 3,
       CTRL2 = 7 };

/* Bitmasks for the different TDC ports:
 * This info is taken from RoentDek TDC8 spec. */
enum bitmasks {
    /* PIA1PB: */
    P_OUT =         0x02,   // bit 1, p.out*
    CH_BIT_0 =      0x04,   // bit 2, channel number bit 0
    CH_BIT_1 =      0x08,   // bit 3, channel number bit 1
    CH_BIT_2 =      0x10,   // bit 4, channel number bit 2
    COM_DISABLED =  0x80,   // bit 7, Com Disabled (Data collection finished)
    /* PIA2PB: */
    ENABLE =        0x02,   // bit 0, Enable*
    RESET =         0x08,   // bit 3, RESET
    P_IN =          0x10,   // bit 4, p.in*
    COM_MODE =      0x20,   // bit 5, Com-operation Mode (HI = common start)
    CSTP_TRIG =     0x40,   // bit 6, Common Stop trigger
    RCLK =          0x80    // bit 7, RCLK
};

/**
 * struct channel_info - information about hits for a channel
 *                       Used for temporary storage of data from TDC-card
 *                       before writing it to the FIFO-buffer.
 * @hits:       Array with the delays (unit 0.5 ns) of each detected hit.
 * @num_hits:   The total number of hits on this channel.
 */
struct channel_info
{
    unsigned short hits[TDC_MAX_NUM_HITS_PER_CHANNEL];
    short num_hits;
};

/**
 * struct event_cache - a temp struct with all the hits for a COM signal. *
 * @ch: Struct with list of the hits on each channel.
 * @num_hits_sum: Total number of hits detected in response to this COM signal.
 *
 * Used to store data temporarily before writing to FIFO.
 */
struct event_cache
{
    struct channel_info ch[TDC_MAX_NUM_CHANNELS]; // list of the hits on each channel
    unsigned short num_hits_sum;
};

/**
 * struct tdc_measurement - information about current TDC-card measurement
 * @time_started:   When was the measurement started
 * @duration:       How long did the measurement run (excluding pauses)
 * @error:          Error flags. See defined TDC error codes above.
 * @cache:          Temporary storage of structured data from TDC-card
 * @buf_overflow:   How many times the buffer size has been too small
 *                  to store all the data
 * @buf_overflow_hits:   How many hits could not be saved due to full buffer
 * @buf_overflow_events: How many events could not be saved due to full buffer
 * @com_rate:       How many times per second we receive a com signal
 * @hit_rate:       How many times per second we receive a hit.
 * @state:          The current state of the measurement.
 * @max_num_com_signals:    Measurement automatically stops if num_com_signals
 *                  reaches this limit, unless it is 0.
 * @num_com_signals: Mainly used to calculate the COM rate or for the limit.
 * @num_com_signals_without_hits: How many COM signals that had no related hit.
 * @num_hits[TDC_MAX_NUM_CHANNELS]: Count the number of hits for each channel.
 * @num_hits_sum: The total number of hits on all channels, also invalid hits.
 * @num_valid_hits_sum: Total number valid hits on all channels.
 * @num_invalid_hits: number of hits not within the time range t_min..t_max,
 *                  for each channel.
 * @num_hits_of_type: count how many singles, doubles, triples, etc,
 *                  got detected per channel.
 */
struct tdc_measurement
{
    ktime_t time_started, duration;
    int error;
    struct event_cache cache;
    unsigned long buf_overflow;
    unsigned long buf_overflow_hits;
    unsigned long buf_overflow_events;
    unsigned long com_rate;
    unsigned long hit_rate;
    tdc_measurement_state state;
    unsigned int max_num_com_signals;
    unsigned long num_com_signals;
    unsigned long num_com_signals_without_hits;
    unsigned long num_hits[TDC_MAX_NUM_CHANNELS];
    unsigned long num_hits_sum;
    unsigned long num_valid_hits_sum;
    unsigned long num_invalid_hits[TDC_MAX_NUM_CHANNELS];
    unsigned long num_hits_of_type[TDC_MAX_NUM_CHANNELS][TDC_MAX_NUM_HITS_PER_CHANNEL+1];
};

/**
 * struct tdc_timer - high resolution timer
 * @hrtimer:            Timer used to periodically poll TDC card for
 *                      received COM signals
 * @callback_interval:  The desired interval between the callbacks to the
 *                      hrtimer callback function. This interval should be a
 *                      multiple of the interval between the COM signal pulses,
 *                      measured in nanoseconds.
 * @callback_rate:      How many times per second the hrtimer callback
 *                      function will be called. Should be "1e9/interval".
 * @lock:               Used to make sure only one timer callback is running
 *                      at the same time.
 */
struct tdc_timer
{
    struct hrtimer hrtimer;
    ktime_t callback_interval;
    unsigned long callback_rate;
    struct semaphore lock;
};

/**
 * struct tdc_device - basic struct representing the TDC card
 * @is_initialized: tells whether the TDC card has been initialized or not
 * @bufq:       A queue for waiting until the FIFO buffer has data to read
 * @stopq:      A queue for waiting until the timer callback function
 *              is not running, so that the measurement can safely be stopped.
 * @error:      Error flags. See defined TDC error codes above.
 * @has_events: 1 if any channel has detected hits with the COM pulse, else 0.
 * @com_mode:   Should card use common start or common stop trigger mode?
 *              Note: Only COMMON_START is tested!
 * @max_num_hits_per_channel: How many hits per channel shall we allow?
 * @t_min:      Min allowed flight-time (in units of 0.5 ns) for valid hits.
 * @t_max:      Max allowed flight-time (in units of 0.5 ns) for valid hits.
 * @num_channels: The number of channels the TDC card has, usually 8.
 * @baseport:   The baseport of the TDC card (ISA), usually: 0x320
 * @measurement: tdc_measurement struct
 * @timer:      tdc_timer struct
 * @fifo:       FIFO buffer from tdc_fifo.h
 * @nreaders:   Number of processes having open read connections to module.
 * @nwriters:   Number of processes having open write connections to module.
 * @sem:        Mutual exclusion semaphore
 * @cdev:       Char device structure
 */
struct tdc_device
{
    unsigned short is_initialized;
    wait_queue_head_t bufq, stopq;
    int error;
    volatile int has_events;
    enum com_mode com_mode;
    unsigned short max_num_hits_per_channel;
    unsigned short t_min, t_max;
    unsigned short num_channels;
    int baseport;
    struct tdc_measurement measurement;
    struct tdc_timer timer;
    struct tdc_fifo *fifo;
    unsigned int nreaders, nwriters;
    struct semaphore sem;
    struct cdev cdev;
};

#endif /* _TDC_COMMON_H_ */
