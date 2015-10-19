#pragma once

#include <libubox/list.h>
#include <libubus.h>

struct script_object {
	struct ubus_object ubus_object; 
	struct ubus_object *ubus_object_mem; 
	struct ubus_method *ubus_methods; 
	struct ubus_context *ubus_ctx; 
	char *stdout_buf; 
	size_t stdout_buf_size; 
	struct list_head list; 
}; 

void script_object_init(struct script_object *self); 
int script_object_load(struct script_object *self, const char *objname, const char *path); 
void script_object_destroy(struct script_object *self); 
int script_object_register_on_ubus(struct script_object *self, struct ubus_context *ctx); 

