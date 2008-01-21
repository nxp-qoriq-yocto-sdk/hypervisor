/**************************************************************************//*
    @file   queue.h

    @brief  This file contain the declarations of simple queue 
    
    

    $Id: $
 **************************************************************************/

#ifndef QUEUE_H_
#define QUEUE_H_


/*==================================================================================================
                          TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
==================================================================================================*/


/** queue_t - This is a generic byte_chan device description  */
typedef struct queue_s
{
	char* buff;
	int used;
	int low_indx;
	int high_indx;
	int size;
    
} queue_t;




/*==================================================================================================
                                       GLOBAL FUNCTIONS
==================================================================================================*/

int queue_create(int size, queue_t** queue);
int queue_get(queue_t* queue, char* c);
int queue_add(queue_t* queue, char c);
int queue_get_room(queue_t* queue);

/*================================================================================================*/

#endif

