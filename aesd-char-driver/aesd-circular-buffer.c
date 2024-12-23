/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
                                                                          size_t char_offset, size_t *entry_offset_byte_rtn)
{
    if (!buffer->full && buffer->in_offs == buffer->out_offs)
    {
        return NULL; // Buffer is empty
    }

    uint8_t index;
    struct aesd_buffer_entry *entry;
    size_t current_offset = 0;

    bool include_in_offs = buffer->full;

    for (index = buffer->out_offs; include_in_offs || index != buffer->in_offs; index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        include_in_offs = false;

        entry = &buffer->entry[index];

        current_offset += entry->size;

        if (char_offset < current_offset)
        {
            *entry_offset_byte_rtn = char_offset - (current_offset - entry->size);
            return entry;
        }
    }

    return NULL; // char_offset exceeds total data in buffer
}

/**
 * Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
 * If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
 * new start location.
 * Any necessary locking must be handled by the caller
 * Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
 */
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    buffer->entry[buffer->in_offs] = *add_entry;

    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    if (buffer->full)
    {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    buffer->full = buffer->in_offs == buffer->out_offs;
}

struct aesd_buffer_entry aesd_circular_buffer_pop_entry(struct aesd_circular_buffer *buffer)
{
    struct aesd_buffer_entry res_entry = {.buffptr = NULL, .size = 0};

    if (buffer->entry[buffer->out_offs].buffptr)
    {
        res_entry.buffptr = buffer->entry[buffer->out_offs].buffptr;
        res_entry.size = buffer->entry[buffer->out_offs].size;

        buffer->entry[buffer->out_offs].buffptr = NULL;
        buffer->entry[buffer->out_offs].size = 0;

        uint8_t new_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        if (new_offs != buffer->in_offs)
        {
            buffer->out_offs = new_offs;
        }

        buffer->full = false;
    }

    return res_entry;
}

size_t aesd_buffer_size(struct aesd_circular_buffer *buffer)
{
    uint8_t index;
    struct aesd_buffer_entry *entry;
    size_t total_size = 0;

    bool include_in_offs = buffer->full;

    for (index = buffer->out_offs; include_in_offs || index != buffer->in_offs; index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        include_in_offs = false;

        entry = &buffer->entry[index];

        total_size += entry->size;
    }

    return total_size;
}

/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}
