#include "tdc_fifo.h"

struct tdc_fifo *tdc_fifo_new(unsigned int size)
{
    struct tdc_fifo *fifo;

    fifo = kzalloc(sizeof(*fifo), GFP_KERNEL);
    if (!fifo)
        return NULL;

    fifo->buffer = kmalloc(size, GFP_KERNEL);
    if (!fifo->buffer) {
        kfree(fifo);
        return NULL;
    }

    fifo->size = size;
    fifo->lock = SPIN_LOCK_UNLOCKED;

    return fifo;
}

void tdc_fifo_destroy(struct tdc_fifo *fifo)
{
    kfree(fifo->buffer);
    kfree(fifo);
}

void tdc_fifo_reset(struct tdc_fifo *fifo)
{
    unsigned long flags;
    spin_lock_irqsave(&fifo->lock, flags);
    fifo->in = fifo->out = 0;
    spin_unlock_irqrestore(&fifo->lock, flags);
}

/* Must be called with lock held: */
static inline unsigned int __tdc_fifo_len(struct tdc_fifo *fifo)
{
    if (fifo->in == fifo->out)
        return 0;
    return (fifo->size - fifo->out + fifo->in) % fifo->size;
}
/* Must be called with lock held: */
static inline unsigned int __tdc_fifo_spacefree(struct tdc_fifo *fifo)
{
    return fifo->size - 1 - __tdc_fifo_len(fifo);
}
inline unsigned int tdc_fifo_len(struct tdc_fifo *fifo)
{
    unsigned long flags;
    unsigned int result;

    spin_lock_irqsave(&fifo->lock, flags);
    result = __tdc_fifo_len(fifo);
    spin_unlock_irqrestore(&fifo->lock, flags);
    return result;
}
inline unsigned int tdc_fifo_spacefree(struct tdc_fifo *fifo)
{
    unsigned long flags;
    unsigned int result;

    spin_lock_irqsave(&fifo->lock, flags);
    result = __tdc_fifo_spacefree(fifo);
    spin_unlock_irqrestore(&fifo->lock, flags);
    return result;
}
/*
 * Returns 0 on success, -1 on error (FIFO full)
 */
int tdc_fifo_putbyte(struct tdc_fifo *fifo,
    unsigned char byte)
{
    unsigned long flags;
    int retval = -1;

    spin_lock_irqsave(&fifo->lock, flags);
    if (__tdc_fifo_spacefree(fifo) == 0)
        goto out;

    fifo->buffer[fifo->in] = byte;
    fifo->in = (fifo->in + 1) % fifo->size;
    retval = 0;
out:
    spin_unlock_irqrestore(&fifo->lock, flags);
    return retval;
}

/*
 * Returns 0 on success, -1 on error.
 */
int tdc_fifo_getbyte(struct tdc_fifo *fifo, unsigned char *byte)
{
    unsigned long flags;
    int retval = -1;

    spin_lock_irqsave(&fifo->lock, flags);
    if (fifo->out == fifo->in)
        goto out;

    *byte = fifo->buffer[fifo->out];
    fifo->out = (fifo->out + 1) % fifo->size;
    retval = 0;
out:
    spin_unlock_irqrestore(&fifo->lock, flags);
    return retval;
}
