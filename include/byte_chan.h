/** @file
 * Byte channel interface.
 */

#ifndef BYTE_CHAN_H
#define BYTE_CHAN_H

#include <libos/queue.h>
#include <stdint.h>
#include <percpu.h>

typedef struct byte_chan_handle_t {
	queue_t *tx;      /**< queue for transmitting data */
	queue_t *rx;      /**< queue for receiving data */
	uint32_t tx_lock; /**< lock for transmitting data */
	uint32_t rx_lock; /**< lock for receiving data */
	int attached;     /**< non-zero if something has attached to this endpoint. */
	handle_t user;    /**< user handle */
} byte_chan_handle_t;

/** byte_chan_t - This is a generic byte_chan device description  */
typedef struct {
	queue_t q[2];  /**< queues */
	byte_chan_handle_t handles[2];
} byte_chan_t;

void byte_chan_global_init(void);
void byte_chan_partition_init(guest_t *guest);

byte_chan_t *byte_chan_alloc(void);

ssize_t byte_chan_send(byte_chan_handle_t *bc,
                       const uint8_t *buf, size_t length);
ssize_t byte_chan_receive(byte_chan_handle_t *bc,
                          uint8_t *buf, size_t length);

int byte_chan_claim(byte_chan_handle_t *handle);
int byte_chan_attach_chardev(byte_chan_handle_t *bc, chardev_t *cd);
int byte_chan_attach_guest(byte_chan_handle_t *bc, guest_t *guest, int rxirq, int txirq);

#endif
