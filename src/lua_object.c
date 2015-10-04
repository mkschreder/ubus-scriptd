#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua_object.h"

void lua_object_init(struct lua_object *self){
	self->lua = luaL_newstate(); 
	luaL_openlibs(self->lua); 
}

void lua_object_destroy(struct lua_object *self){
	if(self->lua) lua_close(self->lua); 
}

int lua_object_load(struct lua_object *self, const char *path){
	int error = luaL_loadfile(self->lua, path) || lua_pcall(self->lua, 0, 0, 0); 
	if(error){
		fprintf(stderr, "%s: error %s\n", __FUNCTION__, lua_tostring(self->lua, -1)); 
		lua_pop(self->lua, 1); 
	}
	return error; 
}

int lua_object_register_on_ubus(struct lua_object *self, struct ubus_context *ctx){
	return 0; 
}
