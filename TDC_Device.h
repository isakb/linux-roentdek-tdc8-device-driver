#ifndef _TDC_DEVICE_H_
#define _TDC_DEVICE_H_

#include <linux/init.h>
#include <linux/module.h>

#include <linux/ioport.h>
#include <asm/io.h>

#include <linux/delay.h>

#include "tdc_fifo.h"
#include "tdc_common.h"

#define DEBUG_IO 0              /* if port i/o is to be watched */


/*
    PIA2PB bitmask:
    ---------------
    7   RCLK
    6   Common Stop trigger _/ ̄
    5   Com-operation Mode (HI = common start)
    4   P.in*
    3   RESET
    2   -
    1   ?
    0   ENABLE
*/

/* The procedure of reading events from TDC card must be done in this order:
 *  1. tdc_setup
 *  2. tdc_prepare_wait
 *  3. tdc_wait_for_com (DEPRECATED, instead using tdc_timer_callback
 *  4. tdc_check_for_events
 *  5. tdc_decode_events
 *  6. tdc_reset
 * (7. repeat steps 2-6 for next iteration)
 *
 * Note: tdc_get_com_event performs steps 2-6 in the correct order,
 * and could be used instead, for simplicity:
 *  1. tdc_setup
 *  2. tdc_get_com_event
 *  3. goto step 2 for next iteration
 */
int tdc_setup (struct tdc_device *self);
inline int tdc_prepare_wait(struct tdc_device *self);
inline int tdc_check_for_events(struct tdc_device *self);
inline int tdc_decode_events(struct tdc_device *self);
inline int tdc_reset(struct tdc_device *self);

inline int tdc_has_detected_com_event(struct tdc_device *self);

int tdc_clear_data(struct tdc_device *dev);

int tdc_reset_measurement(struct tdc_measurement *measurement);
int tdc_start_measurement(struct tdc_measurement *measurement);
int tdc_pause_measurement(struct tdc_measurement *measurement);
int tdc_stop_measurement(struct tdc_measurement *measurement);

int tdc_add_hits_to_fifo(struct tdc_device *self);

int tdc_timer_callback(struct hrtimer *hrtimer);

void tdc_set_com_mode(struct tdc_device *self, enum com_mode mode);

struct tdc_device *tdc_new(unsigned int _baseport);
void tdc_destroy(struct tdc_device *self);

inline void _outb(struct tdc_device *self, enum port _port, unsigned int val);
inline unsigned int _inb(struct tdc_device *self, enum port _port);

/**
 * Returns the value of a specific bit of a port. Bit 0 is the LSB.
 */
inline unsigned int _get_bit(struct tdc_device *self, volatile enum port _port, short bit);


#endif /* _TDC_DEVICE_H_ */

