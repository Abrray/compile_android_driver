//
// Created by fuqiuluo on 25-2-9.
//
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

#include "touch.h"
#include <linux/mutex.h>
#include <linux/input/mt.h>
#include <linux/kprobes.h>
#include <linux/version.h>
#include <linux/input-event-codes.h>
#include "kkit.h"

static inline int is_event_supported(unsigned int code,
									 unsigned long *bm, unsigned int max)
{
	return code <= max && test_bit(code, bm);
}

int get_last_driver_slot(struct input_dev* dev) {
	int slot;
	int new_slot;
	struct input_mt *mt;
	int is_new_slot;

	if(!dev) {
		pr_err("[ovo] wtf? dev is null\n");
		return -114;
	}

	is_new_slot = 0;
	mt = dev->mt;
	if (mt)
		new_slot = mt->slot;
	else
		new_slot = -999;

	if (dev->absinfo != NULL)
		slot = dev->absinfo[ABS_MT_SLOT].value;
	else
		slot = -999;

	if(new_slot == -999 && slot == -999) {
		return -114;
	}

	if(slot == -999) {
		return new_slot;
	}

	if(new_slot == -999) {
		return slot;
	}

	is_new_slot = new_slot != slot;
	return is_new_slot ? new_slot : slot;
}

static void (*my_input_handle_event)(struct input_dev *dev,
							   unsigned int type, unsigned int code, int value) = NULL;

int input_event_no_lock(struct input_dev *dev,
				 unsigned int type, unsigned int code, int value)
{
	if(my_input_handle_event == NULL) {
		my_input_handle_event = (void (*)(struct input_dev *, unsigned int, unsigned int, int))ovo_kallsyms_lookup_name("input_handle_event");
	}

	if (!my_input_handle_event) {
		pr_err("[ovo] Holy fuck!Failed to find input_handle_event\n");
		return -1;
	}

	if (is_event_supported(type, dev->evbit, EV_MAX)) {
		my_input_handle_event(dev, type, code, value);
	}

	return 0;
}

struct input_dev* find_touch_device(void) {
	static struct input_dev* CACHE = NULL;

	if (CACHE != NULL) {
		return CACHE;
	}

	struct input_dev *dev;
	struct list_head *input_dev_list;
	struct mutex *input_mutex;

	input_dev_list = (struct list_head *)ovo_kallsyms_lookup_name("input_dev_list");
	input_mutex = (struct mutex *)ovo_kallsyms_lookup_name("input_mutex");
	if (!input_dev_list || !input_mutex) {
		printk(KERN_ERR "Failed to find symbols!\n");
		return NULL;
	}

	// /*
	// * input_mutex protects access to both input_dev_list and input_handler_list.
	// * This also causes input_[un]register_device and input_[un]register_handler
	// * be mutually exclusive which simplifies locking in drivers implementing
	// * input handlers.
	// */
	//static DEFINE_MUTEX(input_mutex);
	mutex_lock(input_mutex);

	list_for_each_entry(dev, input_dev_list, node) {
		if (test_bit(EV_ABS, dev->evbit) &&
			(test_bit(ABS_MT_POSITION_X, dev->absbit) || test_bit(ABS_X, dev->absbit))) {\
            pr_info("[ovo] Name: %s, Bus: %d Vendor: %d Product: %d Version: %d\n",
					dev->name,
					dev->id.bustype, dev->id.vendor,
					dev->id.product, dev->id.version);
			mutex_unlock(input_mutex);
			CACHE = dev;
			return dev;
		}
	}

	mutex_unlock(input_mutex);
	return NULL;
}

static struct event_pool *pool = NULL;

struct event_pool * get_event_pool(void) {
	return pool;
}

int input_event_cache(unsigned int type, unsigned int code, int value, int lock) {
	if (!my_input_handle_event) {
		pr_err("[ovo] Failed to find input_handle_event\n");
		return -EINVAL;
	}

	unsigned long flags;
	if (lock)
		spin_lock_irqsave(&pool->event_lock, flags);
	if (pool->size >= MAX_EVENTS) {
		if (lock)
			spin_unlock_irqrestore(&pool->event_lock, flags);
		return -EFAULT;
	}
	struct ovo_touch_event* event = &pool->events[pool->size++];
	event->type = type;
	event->code = code;
	event->value = value;
	if (lock)
		spin_unlock_irqrestore(&pool->event_lock, flags);

	return 0;
}

int input_mt_report_slot_state_cache(unsigned int tool_type, bool active, int lock)
{
	if (!active) {
		input_event_cache(EV_ABS, ABS_MT_TRACKING_ID, -1, lock);
		return 0;
	}

	struct input_dev* dev = find_touch_device();
	struct input_mt *mt = dev->mt;
	struct input_mt_slot *slot;
	int id;

	if (!mt)
		return -1;

	if (mt->slot < 0 || mt->slot > mt->num_slots) {
		return -1;
	}
	slot = &mt->slots[mt->slot];

	id = input_mt_get_value(slot, ABS_MT_TRACKING_ID);
	if (id < 0)
		id = input_mt_new_trkid(mt);

	input_event_cache(EV_ABS, ABS_MT_TRACKING_ID, id, lock);
	input_event_cache(EV_ABS, ABS_MT_TOOL_TYPE, tool_type, lock);

	return id;
}

bool input_mt_report_slot_state_with_id_cache(unsigned int tool_type, bool active, int id, int lock)
{
	if (!active) {
		input_event_cache(EV_ABS, ABS_MT_TRACKING_ID, -1, lock);
		return false;
	}

	input_event_cache(EV_ABS, ABS_MT_TRACKING_ID, id, lock);
	input_event_cache(EV_ABS, ABS_MT_TOOL_TYPE, tool_type, lock);

	return true;
}

static void handle_cache_events(struct input_dev* dev) {
	struct input_mt *mt = dev->mt;
	struct input_mt_slot *slot;
	unsigned long flags, flags2;
	int id;

	if (!mt)
		return;

	if (mt->slot < 0 || mt->slot > mt->num_slots) {
		return;
	}
	slot = &mt->slots[mt->slot];

	spin_lock_irqsave(&pool->event_lock, flags2);
	if (pool->size == 0) {
		spin_unlock_irqrestore(&pool->event_lock, flags2);
		return;
	}
	spin_lock_irqsave(&dev->event_lock, flags);

	for (int i = 0; i < pool->size; ++i) {
		struct ovo_touch_event event = pool->events[i];

		if (event.type == EV_ABS &&
			event.code == ABS_MT_TRACKING_ID &&
			event.value == -114514) {
			id = input_mt_get_value(slot, ABS_MT_TRACKING_ID);
			if (id < 0)
				id = input_mt_new_trkid(mt);
			event.value = id;
		}

		input_event_no_lock(dev, event.type, event.code, event.value);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
	pool->size = 0;
	spin_unlock_irqrestore(&pool->event_lock, flags2);
}

static int input_handle_event_handler_pre(struct kprobe *p,
										  struct pt_regs *regs)
{
	unsigned int type = (unsigned int)regs->regs[1];
//	unsigned int code = (unsigned int)regs->regs[2];
//	int value = (int)regs->regs[3];

	struct input_dev* dev = (struct input_dev*)regs->regs[0];
	if(!dev) {
		return 0;
	}

//	if (type == EV_ABS) {
//		pr_info("[ovo] input_event(%u, %u, %d)", type, code, value);
//	}

	if (type != EV_SYN) {
		return 0;
	}

	handle_cache_events(dev);
	return 0;
}

static struct kprobe input_event_kp = {
	.symbol_name = "input_event",
	.pre_handler = input_handle_event_handler_pre,
};

// void input_mt_sync_frame(struct input_dev *dev)
static int input_mt_sync_frame_pre(struct kprobe *p, struct pt_regs *regs) {
	struct input_dev* dev = (struct input_dev*)regs->regs[0];
	if(!dev) {
		return 0;
	}

	//handle_cache_events(dev);
	return 0;
}

static struct kprobe input_mt_sync_frame_kp = {
	.symbol_name = "input_mt_sync_frame",
	.pre_handler = input_mt_sync_frame_pre,
};

int init_input_dev(void) {
	int ret;
	ret = register_kprobe(&input_event_kp);
	pr_info("[ovo] input_event_kp: %d\n", ret);
	if (ret) {
		return ret;
	}

	ret = register_kprobe(&input_mt_sync_frame_kp);
	if(ret) {
		unregister_kprobe(&input_event_kp);
		return ret;
	}

	if(my_input_handle_event == NULL) {
		my_input_handle_event = (void (*)(struct input_dev *, unsigned int, unsigned int, int))ovo_kallsyms_lookup_name("input_handle_event");
	}

	if (!my_input_handle_event) {
		pr_err("[ovo] failed to find my_input_handle_event\n");
		return -1;
	}

	pool = kvmalloc(sizeof(struct event_pool), GFP_KERNEL);
	if (!pool) {
		unregister_kprobe(&input_event_kp);
		unregister_kprobe(&input_mt_sync_frame_kp);
		return -ENOMEM;
	}
	pool->size = 0;
	spin_lock_init(&pool->event_lock);

	return ret;
}

void exit_input_dev(void) {
	unregister_kprobe(&input_event_kp);
	unregister_kprobe(&input_mt_sync_frame_kp);
	if (pool)
		kfree(pool);
}