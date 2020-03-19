#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>


MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 101
#define DECIMAL_MAX 100000000000000000

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

typedef struct BigN {
    unsigned long long lower, upper;
} BigN;


static int myclz(int input)
{
    // use binary search method to check
    int count = 0;

    if ((input & 0xFFFF0000) == 0) {
        input <<= 16;
        count += 16;
    }  // 1111 1111 1111 1111
    if ((input & 0xFF000000) == 0) {
        input <<= 8;
        count += 8;
    }  // 1111 1111
    if ((input & 0xF0000000) == 0) {
        input <<= 4;
        count += 4;
    }  // 1111
    if ((input & 0xC0000000) == 0) {
        input <<= 2;
        count += 2;
    }  // 1100
    if ((input & 0x80000000) == 0) {
        count += 1;
    }  // 1000
    return count;
}

static inline void addBigN_DECIMAL(struct BigN *output, struct BigN x, struct BigN y)
{
    output->upper = x.upper + y.upper;

    unsigned long long diff =  DECIMAL_MAX - x.lower;
	if(y.lower > diff){
		output->upper++;
		output->lower = x.lower+y.lower-DECIMAL_MAX;
	}else
		output->lower = x.lower + y.lower;
	
}

static inline void addBigN(struct BigN *output, struct BigN x, struct BigN y)
{
    output->upper = x.upper + y.upper;
    unsigned long long diff = ULLONG_MAX - x.lower;
	if (y.lower > diff){
        output->upper++;
	}
    output->lower = x.lower + y.lower;

}



static inline void subBigN(struct BigN *output, struct BigN x, struct BigN y)
{
    if (x.lower < y.lower) {
        output->lower = x.lower + ~y.lower + 1;
        output->upper = x.upper - y.upper - 1;
    } else {
        output->lower = x.lower - y.lower;
        output->upper = x.upper - y.upper;
    }
}

static inline void mulBigN(struct BigN *output, struct BigN x, struct BigN y)
{
    output->lower = 0;
    output->upper = 0;

    size_t width = 8 * sizeof(unsigned long long);



    for (size_t i = 0; i < width; i++) {
        if ((y.lower >> i) & 0x1) {
            BigN tmp;

            output->upper += x.upper << i;

            tmp.lower = (x.lower << i);
            tmp.upper = (i == 0) ? 0 : (x.lower >> (width - i));
            addBigN(output, *output, tmp);
        }
    }

    for (size_t i = 0; i < width; i++) {
        if ((y.upper >> i) & 0x1) {
            output->upper += (x.lower << i);
        }
    }
}


static inline void setBigN(struct BigN *output,
                           unsigned long long upper,
                           unsigned long long lower)
{
    output->upper = upper;
    output->lower = lower;
}

static BigN fib_sequence(long long k)
{
    long long start, end;
    start = ktime_get_ns();
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    BigN f[k + 2];

    setBigN(&f[0], (long long) 0, (long long) 0);  // f[0] = 0
    setBigN(&f[1], (long long) 0, (long long) 1);  // f[1] = 1


    for (int i = 2; i <= k; i++) {
        addBigN_DECIMAL(&f[i], f[i - 1], f[i - 2]);  // f[i] = f[i - 1] + f[i - 2]
    }

    end = ktime_get_ns();
    printk("%lld %lld\n", k, end - start);
    return f[k];
}

static BigN fast_doubling_fib_sequence(long long k)
{
    long long start, end;
    start = ktime_get_ns();

    unsigned int msb = myclz(k);
    unsigned int mask = (1 << (31 - msb - 1));
    struct BigN a = {.upper = 0, .lower = 1}, b = {.upper = 0, .lower = 1};
    struct BigN two = {.upper = 0, .lower = 2}, zero = {.upper = 0, .lower = 0};
    struct BigN one = {.upper = 0, .lower = 1};
    /* fast doubling formula
     * f(2k) = f(k)[2f(k + 1) - f(k)]
     * f(2k + 1) = f(k + 1)^2 + f(k)^2
     */

    if (k == 0) {
        end = ktime_get_ns();
        printk("%lld %lld\n", k, end - start);
        return zero;
    }
    if (k == 1 || k == 2) {
        end = ktime_get_ns();
        printk("%lld %lld\n", k, end - start);
        return one;
    }

    while (mask > 0) {
        BigN t1, t2, tmp, tmp2;
        /*
        t1 = a*(2*b - a);
        t2 = b^2 + a^2;
        a = t1; b = t2; // m *= 2
        if (current binary digit == 1)
            t1 = a + b; // m++
            a = b; b = t1;
        */

        mulBigN(&tmp, two, b);
        subBigN(&tmp2, tmp, a);
        mulBigN(&t1, a, tmp2);

        mulBigN(&tmp, b, b);
        mulBigN(&tmp2, a, a);
        addBigN(&t2, tmp, tmp2);
        a = t1;
        b = t2;
        if (mask & k) {
            addBigN(&t1, a, b);
            a = b;
            b = t1;
        }
        mask >>= 1;
    }
    end = ktime_get_ns();
    printk("%lld %lld\n", k, end - start);
    return a;
}

static unsigned long long normal_fib(long k){
	long long start, end;
    start = ktime_get_ns();
	long long f[k + 2];

    f[0] = 0;
    f[1] = 1;

    for (int i = 2; i <= k; i++) {
        f[i] = f[i - 1] + f[i - 2];
    }
	end = ktime_get_ns();
    printk("%lld %lld\n", k, end - start);

    return f[k];
}

static unsigned long long fast_fib(long k){
	long long start, end;
    start = ktime_get_ns();

	unsigned int msb = myclz(k);
	unsigned long long a = 0,b = 1;

	if(k==0){
		end = ktime_get_ns();
		printk("%lld %lld\n", k, end - start);
		return 0;
	}
	else if( k==1 || k ==2 ){
		end = ktime_get_ns();
		printk("%lld %lld\n", k, end - start);
		return 1;
	}
	for (int i = 31 - msb; i >= 0; i--) {
        unsigned long long t1, t2;
		t1 = a*(2*b - a);
        t2 = b*b + a*a;
        a = t1; 
		b = t2; // m *= 2
        if ((k & (1 << i)) > 0) {
			t1 = a + b;
			a = b;
			b = t1;
        }
    }
	end = ktime_get_ns();
    printk("%lld %lld\n", k, end - start);

	return a;
	
	
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
    char kbuf[128] = {0};
#ifdef VER_DP
    BigN tmp = fib_sequence(*offset);
#else
    BigN tmp = fast_doubling_fib_sequence(*offset);
#endif
	
    if (tmp.upper != 0)
        sprintf(kbuf, "%llu_%llu", tmp.upper, tmp.lower);
    else
        sprintf(kbuf, "%llu", tmp.lower);
	copy_to_user(buf, kbuf, 128);
    return 1;
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
    cdev_init(fib_cdev, &fib_fops);
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
