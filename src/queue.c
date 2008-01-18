/**
 *  (c) Copyright Freescale 2007, All Rights Reserved
 *
 *  @file queue.c
 *
 *	
 */

#include  "uverrors.h"
#include  "libos/libos.h"
#include  "stdint.h"
#include  "string.h"
#include  "queue.h"


int queue_create(int size, queue_t** queue)
{

	if(queue == NULL) {
		return NULL_POINTER;
	}

	if(size == 0) {
		return INVALID_PARAM;
	}
	

	*queue = (queue_t*)alloc(sizeof(queue_t),sizeof(int));
	if(*queue == NULL) {
		return MEM_NOT_ENOUGH;
	}

	(*queue)->buff = alloc(sizeof(char) * size, sizeof(int));

	if((*queue)->buff == NULL) {
		return MEM_NOT_ENOUGH;		
	}

	(*queue)->used	  = 0;
	(*queue)->low_indx  = 0;
	(*queue)->high_indx = 0;
	(*queue)->size = size;

	return 1;

};

int queue_get(queue_t* queue, char* c)
{	

	if(queue == NULL) {
		return NULL_POINTER;
	}

	if(c == 0) {
		return INVALID_PARAM;
	}
	
	if (queue->used == 0) {
		/* queue is empty */
		return -1;
	}
 
	*c = queue->buff[queue->low_indx++];
	/* optimize: queue->low_indx %= queue->size; */
	if (queue->low_indx == queue->size) {
		queue->low_indx = 0;
	}
	
	queue->used--;
	
	return 1;
 	
}



int queue_add(queue_t* queue, char c)
{	
	
	if (queue->used == queue->size) {
		/* queue is full */
		return -1;
	}
	
	queue->buff[queue->high_indx++] = c;
	
	/* optimize: queue->high_indx %= queue->size; */
	if (queue->high_indx == queue->size) {
		queue->high_indx = 0;
	}
	
	queue->used++;	
	return 1;
 	
}

int queue_get_room(queue_t* queue)
{
	return queue->size - queue->used;
}

