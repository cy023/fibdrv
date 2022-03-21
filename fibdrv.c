#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 100

#define DEC_LOWDIGIT_BOUND 100000000000000000

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

typedef union {
    struct {
        unsigned long long bin_lower;
        unsigned long long bin_upper;
    };
    struct {
        unsigned long long dec_lower;
        unsigned long long dec_upper;
    };
} ui128_t;

static ssize_t cntdig(unsigned long long num)
{
    ssize_t cnt = 0;
    do {
        cnt++;
        num /= 10;
    } while (num);
    return cnt;
}

static inline void add_ui128_dec(ui128_t *output, ui128_t x, ui128_t y)
{
    output->dec_upper = x.dec_upper + y.dec_upper;
    if (y.dec_lower + x.dec_lower > DEC_LOWDIGIT_BOUND)
        output->dec_upper++;
    output->dec_lower = (x.dec_lower + y.dec_lower) % DEC_LOWDIGIT_BOUND;
}

static ui128_t fib_sequence(long long k)
{
    ui128_t fn = {.bin_upper = 0, .bin_lower = 0};
    ui128_t fn1 = {.bin_upper = 0, .bin_lower = 1};

    if (k == 0)
        return fn;
    if (k == 1)
        return fn1;

    long long i;
    for (i = 2; i <= k; i++) {
        ui128_t tmp = {.dec_upper = 0, .dec_lower = 0};
        add_ui128_dec(&tmp, fn, fn1);
        fn.dec_upper = fn1.dec_upper;
        fn.dec_lower = fn1.dec_lower;
        fn1.dec_upper = tmp.dec_upper;
        fn1.dec_lower = tmp.dec_lower;
    }
    return fn1;
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    ui128_t res = fib_sequence(*offset);
    ssize_t lenu = cntdig(res.dec_upper);
    ssize_t lenl = cntdig(res.dec_lower);
    char ns[40] = {0};
    snprintf(ns, lenu + 1, "%llu", res.dec_upper);
    if (res.dec_upper) {
        snprintf(ns, lenu + 1, "%llu", res.dec_upper);
        snprintf(ns + lenu, 18, "%017llu", res.dec_lower);
    } else {
        snprintf(ns, lenl + 1, "%llu", res.dec_lower);
    }
    if (copy_to_user(buf, ns, strlen(ns) + 1) != 0)
        return -1;
    return (ssize_t) strlen(ns);
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
