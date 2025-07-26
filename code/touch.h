//
// Created by fuqiuluo on 25-2-9.
//

#ifndef OVO_TOUCH_H
#define OVO_TOUCH_H

#include <linux/input.h>

struct ovo_touch_event {
	unsigned int type;
	unsigned int code;
	int value;
};

#define MAX_EVENTS 1024
#define RING_MASK (MAX_EVENTS - 1)

struct event_pool {
	struct ovo_touch_event events[MAX_EVENTS];
	unsigned long size;
	spinlock_t event_lock;
};

int init_input_dev(void);

void exit_input_dev(void);

struct input_dev* find_touch_device(void);

struct event_pool * get_event_pool(void);

int get_last_driver_slot(struct input_dev* dev);

int input_event_no_lock(struct input_dev *dev,
						unsigned int type, unsigned int code, int value);

int input_event_cache(unsigned int type, unsigned int code, int value, int lock);

int input_mt_report_slot_state_cache(unsigned int tool_type, bool active, int lock);
bool input_mt_report_slot_state_with_id_cache(unsigned int tool_type, bool active, int id, int lock);

#endif //OVO_TOUCH_H
