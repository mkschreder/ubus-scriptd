#pragma once

struct lua_object {
	struct lua_State *lua; 
	struct ubus_context *ubus_ctx; 
}; 

void lua_object_init(struct lua_object *self); 
void lua_object_destroy(struct lua_object *self); 
int lua_object_load(struct lua_object *self, const char *path); 
int lua_object_register_on_ubus(struct lua_object *self, struct ubus_context *ctx); 
