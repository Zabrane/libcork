/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2011, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the COPYING file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <stdlib.h>

#include "libcork/core/types.h"
#include "libcork/ds/ring-buffer.h"


int
cork_ring_buffer_init(struct cork_ring_buffer *self, size_t size)
{
    self->elements = calloc(size, sizeof(void *));
    if (self->elements == NULL) {
        return -1;
    }

    self->allocated_size = size;
    self->size = 0;
    self->read_index = 0;
    self->write_index = 0;
    return 0;
}

void
cork_ring_buffer_done(struct cork_ring_buffer *self)
{
    free(self->elements);
}

int
cork_ring_buffer_add(struct cork_ring_buffer *self, void *element)
{
    if (cork_ring_buffer_is_full(self)) {
        return -1;
    }

    self->elements[self->write_index++] = element;
    self->size++;
    if (self->write_index == self->allocated_size) {
        self->write_index = 0;
    }
    return 0;
}

void *
cork_ring_buffer_pop(struct cork_ring_buffer *self)
{
    if (cork_ring_buffer_is_empty(self)) {
        return NULL;
    } else {
        void  *result = self->elements[self->read_index++];
        self->size--;
        if (self->read_index == self->allocated_size) {
            self->read_index = 0;
        }
        return result;
    }
}

void *
cork_ring_buffer_peek(struct cork_ring_buffer *self)
{
    if (cork_ring_buffer_is_empty(self)) {
        return NULL;
    } else {
        return self->elements[self->read_index];
    }
}
