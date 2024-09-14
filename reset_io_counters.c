#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/blkdev.h>

// make && insmod this, clear error counters out for mmcblk0

static int __init reset_io_errors_init(void)
{
    struct block_device *bdev;
    struct request_queue *q;

    bdev = blkdev_get_by_path("/dev/mmcblk0", BLK_OPEN_READ, THIS_MODULE, NULL);
    if (IS_ERR(bdev)) {
        printk(KERN_ERR "Failed to get mmcblk0 block device\n");
        return PTR_ERR(bdev);
    }

    q = bdev_get_queue(bdev);
    if (!q) {
        printk(KERN_ERR "Failed to get mmcblk0 queue\n");
        blkdev_put(bdev, THIS_MODULE);
        return -ENODEV;
    }

    // Reset I/O error counters
    // Note: As of kernel 6.6, direct access to error counters is not available
    // You may need to implement a different method to reset errors

    blkdev_put(bdev, THIS_MODULE);
    printk(KERN_INFO "I/O error counters reset attempt for mmcblk0\n");
    return 0;
}

static void __exit reset_io_errors_exit(void)
{
    printk(KERN_INFO "I/O error reset module unloaded\n");
}

module_init(reset_io_errors_init);
module_exit(reset_io_errors_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Scott Gillespie");
MODULE_DESCRIPTION("Reset I/O error counters for mmcblk0 on raspi5 in home lab setup.");