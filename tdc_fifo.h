#ifndef _TDC_FIFO_H_
#define _TDC_FIFO_H_

#include <linux/init.h>
#include <linux/module.h>

/*
 * A custom FIFO, inspired by <linux/kfifo.h>
 * and http://lwn.net/Articles/101808/
 * It should be replaced with the standard kfifo if this
 * driver is to be more standardized. (kfifo was quite
 * new when this code was developed, and seemed to
 * be causing memory leaks when it was tested)
 */
struct tdc_fifo {
    unsigned char *buffer;  /* the buffer holding the data */
    unsigned int size;      /* the size of the allocated buffer */
    unsigned int in;        /* data is added at offset (in % size) */
    unsigned int out;       /* data is extracted from off. (out % size) */
    spinlock_t lock;        /* protects concurrent modifications */
};

struct tdc_fifo *tdc_fifo_new(unsigned int size);
void tdc_fifo_destroy(struct tdc_fifo *fifo);
void tdc_fifo_reset(struct tdc_fifo *fifo);


unsigned int tdc_fifo_len(struct tdc_fifo *fifo);
unsigned int tdc_fifo_spacefree(struct tdc_fifo *fifo);
int tdc_fifo_putbyte(struct tdc_fifo *fifo, unsigned char byte);
int tdc_fifo_getbyte(struct tdc_fifo *fifo, unsigned char *byte);

#endif /* _TDC_FIFO_H_ */
