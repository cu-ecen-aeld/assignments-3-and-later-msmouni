/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>      // file_operations
#include <linux/uaccess.h> // Required for the copy_to_user and copy_from_user functions
#include <linux/slab.h>    // For kmalloc and kfree
#include <linux/string.h>  // Required for strchr()
#include "aesdchar.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("msmouni");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

// Function prototypes for file operations
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos);

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle read
     */

    size_t total_buff_size = aesd_buffer_size(&aesd_device.circular_buff);

    if (*f_pos >= (total_buff_size + aesd_device.input_size))
    {
        return 0; // EOF
    }
    else if (*f_pos >= total_buff_size)
    {
        if (copy_to_user(buf, &aesd_device.input_buffer[*f_pos - total_buff_size], count))
        {
            printk(KERN_ALERT "Failed to send message to user\n");
            return -EFAULT;
        }
    }
    else
    {
        size_t entry_offset_byte_rtn;

        struct aesd_buffer_entry *entry_at_pos = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_device.circular_buff,
                                                                                                 *f_pos, &entry_offset_byte_rtn);

        if (entry_at_pos)
        {
            size_t bytes_to_read = min(count, entry_at_pos->size - entry_offset_byte_rtn);

            if (copy_to_user(buf, &entry_at_pos->buffptr[entry_offset_byte_rtn], bytes_to_read))
            {
                printk(KERN_ALERT "Failed to send message to user\n");
                return -EFAULT;
            }

            retval = bytes_to_read;
            *f_pos += bytes_to_read;
        }
    }

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle write
     */

    // Allocate (or reallocate) memory using krealloc
    aesd_device.input_buffer = krealloc(aesd_device.input_buffer, aesd_device.input_size + count, GFP_KERNEL);
    if (!aesd_device.input_buffer)
    {
        printk(KERN_ALERT "Memory allocation failed\n");
        return -ENOMEM;
    }

    // Copy the input data from user space to the kernel buffer
    if (copy_from_user(aesd_device.input_buffer + aesd_device.input_size, buf, count))
    {
        printk(KERN_ALERT "Failed to receive data from user\n");
        return -EFAULT;
    }

    retval = count;
    aesd_device.input_size += count;

    // Check if the input buffer contains a newline character
    char *newline = strchr(aesd_device.input_buffer, '\n');
    if (newline)
    {
        struct aesd_buffer_entry *new_entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
        new_entry->size = aesd_device.input_size;
        new_entry->buffptr = aesd_device.input_buffer;

        if (aesd_device.circular_buff.full)
        {
            struct aesd_buffer_entry pop_entry = aesd_circular_buffer_pop_entry(&aesd_device.circular_buff);
            kfree(pop_entry.buffptr);
        }

        aesd_circular_buffer_add_entry(&aesd_device.circular_buff, new_entry);

        aesd_device.input_buffer = NULL;
        aesd_device.input_size = 0;
    }

    return retval;
}
struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

// Init function prototype
int aesd_init_module(void);

// Cleanup module prototype
void aesd_cleanup_module(void);

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
                                 "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    result = aesd_setup_cdev(&aesd_device);

    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
