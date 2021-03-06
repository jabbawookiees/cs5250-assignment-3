#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#define ONEBYTE_IOC_MAGIC  'k'
#define ONEBYTE_HELLO _IO(ONEBYTE_IOC_MAGIC, 1)
#define ONEBYTE_SET_DEVMSG _IOWR(ONEBYTE_IOC_MAGIC, 2, char*)
#define ONEBYTE_COPY_DEVMSG _IOR(ONEBYTE_IOC_MAGIC, 3, char*)
#define ONEBYTE_IOC_MAXNR 3
#define MAJOR_NUMBER 61/* forward declaration */
#define ONEBYTE_SIZE 4000000
#define MESSAGE_SIZE 1000

int onebyte_open(struct inode *inode, struct file *filep);
int onebyte_release(struct inode *inode, struct file *filep);
ssize_t onebyte_read(struct file *filep, char *buf, size_t count, loff_t *f_pos);
ssize_t onebyte_write(struct file *filep, const char *buf, size_t count, loff_t *f_pos);
loff_t onebyte_seek(struct file *filep, loff_t offset, int whence);
long ioctl_example(struct file * filp, unsigned int cmd, unsigned long arg);
static void onebyte_exit(void);

/* definition of file_operation structure */
struct file_operations onebyte_fops = {
    read: onebyte_read,
    write: onebyte_write,
    open: onebyte_open,
    release: onebyte_release,
    llseek: onebyte_seek,
    unlocked_ioctl: ioctl_example
};

char *onebyte_data = NULL;
char *dev_msg;
char *user_msg;
loff_t onebyte_length = 0;

int onebyte_open(struct inode *inode, struct file *filep) {
    // We want our device to work like a normal file
    if (filep->f_flags & O_APPEND) {
        // When we append, we move the file position to the end upon opening
        filep->f_pos = onebyte_length;
    } else if ((filep->f_flags & O_ACCMODE) == O_WRONLY) {
        // When we write, we set the length to zero
        onebyte_length = 0;
    }
    return 0; // always successful
}

int onebyte_release(struct inode *inode, struct file *filep) {
    return 0; // always successful
}

ssize_t onebyte_read(struct file *filep, char *buf, size_t count, loff_t *f_pos) {
    // We can only read until `onebyte_length`
    int i;
    for (i=0; i<count && *f_pos < onebyte_length; i++) {
        buf[i] = onebyte_data[(*f_pos)++];
    }
    return i;
}

ssize_t onebyte_write(struct file *filep, const char *buf, size_t count, loff_t *f_pos) {
    int i;
    // Ensure that the onebyte_length is at least as big as f_pos
    // And that all values are initialized (otherwise we will get garbage when we read!)
    while(onebyte_length < *f_pos && onebyte_length < ONEBYTE_SIZE) {
        onebyte_data[onebyte_length++] = 0;
    }
    // Copy data over to the correct position
    for (i=0; i<count && *f_pos < ONEBYTE_SIZE; i++) {
        onebyte_data[(*f_pos)++] = buf[i];
    }
    // If the new position is longer than the current length, update the length
    if (*f_pos > onebyte_length) {
        onebyte_length = *f_pos;
    }

    // If we manage to copy everything over, there is no error. Otherwise we give a no space error.
    if (i == count) {
        return i;
    } else {
        return -ENOSPC;
    }
}

loff_t onebyte_seek(struct file *filep, loff_t offset, int whence) {
    loff_t newpos;
    if (whence == 0) {
        newpos = offset;
    } else if (whence == 1) {
        newpos = filep->f_pos + offset;
    } else {
        newpos = onebyte_length + offset;
    }
    printk(KERN_INFO "Seeking. offset: %2lld whence: %2d  old_f_pos: %2lld new_f_pos: %2lld", offset, whence, filep->f_pos, newpos);
    filep->f_pos = newpos;
    return newpos;
}


static int onebyte_init(void) {
    int result;
    // register the device
    result = register_chrdev(MAJOR_NUMBER, "onebyte", &onebyte_fops);
    if (result < 0) {
        return result;
    }
    // allocate ONEBYTE_SIZE bytes of memory for storage
    onebyte_data = kmalloc(sizeof(char) * ONEBYTE_SIZE, GFP_KERNEL);
    dev_msg = kmalloc(sizeof(char) * MESSAGE_SIZE, GFP_KERNEL);
    user_msg = kmalloc(sizeof(char) * MESSAGE_SIZE, GFP_KERNEL);
    if (!onebyte_data || !dev_msg || !user_msg) {
        onebyte_exit();
        // cannot allocate memory
        // return no memory error, negative signify a failure
        return -ENOMEM;
    }
    // initalize the length to zero, existing data is garbage
    onebyte_length = 0;

    printk(KERN_ALERT "This is a onebyte device module\n");
    return 0;

}

static void onebyte_exit(void)
{
    // if the pointer is pointing to something
    if (onebyte_data) {
        // free the memory and assign the pointer to NULL
        kfree(onebyte_data);
        onebyte_data = NULL;
    }
    if (dev_msg) {
        kfree(dev_msg);
        dev_msg = NULL;
    }
    if (user_msg) {
        kfree(user_msg);
        user_msg = NULL;
    }
    // unregister the device
    unregister_chrdev(MAJOR_NUMBER, "onebyte");
    printk(KERN_ALERT "Onebyte device module is unloaded\n");
}

long ioctl_example(struct file * filp, unsigned int cmd, unsigned long arg) {
    int err = 0;
    int retval = 0;
    int i;
    char* msg;
    /*
     * extract the type and number bitfields, and don't decode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
    if (_IOC_TYPE(cmd) != ONEBYTE_IOC_MAGIC) {
        return -ENOTTY;
    }
    if (_IOC_NR(cmd) > ONEBYTE_IOC_MAXNR) {
        return -ENOTTY;
    }
     /*
     * the direction is a bitmask, and VERIFY_WRITE catches R/W
     * transfers. `Type' is user‐oriented, while
     * access_ok is kernel‐oriented, so the concept of "read" and
     * "write" is reversed
     */
    if (_IOC_DIR(cmd) & _IOC_READ) {
       err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    } else if (_IOC_DIR(cmd) & _IOC_WRITE) {
       err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    }
    if (err) {
        return -EFAULT;
    }
    switch(cmd) {
    case ONEBYTE_HELLO:
        printk(KERN_WARNING "hello\n");
        break;
    case ONEBYTE_SET_DEVMSG:
        // Convert the arg to a char*
        msg = (char*)arg;
        printk(KERN_INFO "Called SET_DEVMSG with \"%s\"\n", msg);

        // Store the original value of dev_msg in user_msg
        for(i=0; dev_msg[i] != 0 && i < MESSAGE_SIZE - 1; i++) {
            user_msg[i] = dev_msg[i];
        }

        // Copy msg to dev_msg
        for(i=0; msg[i] != 0 && i < MESSAGE_SIZE - 1; i++) {
            dev_msg[i] = msg[i];
        }
        dev_msg[i] = 0;
        printk(KERN_INFO "New Dev Message: %s\n", dev_msg);
        break;
    case ONEBYTE_COPY_DEVMSG:
        printk(KERN_INFO "Called COPY_DEVMSG\n");
        // Copy the dev_msg to user_msg
        for(i=0; dev_msg[i] != 0 && i < MESSAGE_SIZE - 1; i++) {
            user_msg[i] = dev_msg[i];
        }
        user_msg[i] = 0;
        printk(KERN_INFO "New User Message: %s\n", user_msg);
        break;
    default:  /* redundant, as cmd was checked against MAXNR */
        return -ENOTTY;
    }
    return retval;
}


MODULE_LICENSE("GPL");
module_init(onebyte_init);
module_exit(onebyte_exit);
