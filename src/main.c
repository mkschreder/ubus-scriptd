/* 
	LICENSE: GPL
	AUTHOR: Martin K. Schröder <mkschreder.uk@gmail.com>
	SOURCE: https://github.com/mkschreder/etc-ubus-daemon.git
	COPYRIGHT: (c) 2015
*/	
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED 
#define _BSD_SOURCE

#include <libubus.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <libubox/blobmsg_json.h>
#include <libubox/avl-cmp.h>
#include <libubox/list.h>
#include <libubus.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <pthread.h>

#include "config.h"
#include "script_object.h"
#include "lua_object.h"

struct app {
	struct ubus_context *ctx; 
	struct list_head scripts;  
}; 

void on_ubus_connection_lost(struct ubus_context *ctx){
	printf("ubus connection lost!\n"); 
}

static int _for_each_file(struct app *self, const char *path, const char *base_path, int (*on_file_cb)(struct app *self, const char *fname, const char *base_path)){
	int rv = 0; 
	if(!base_path) base_path = path; 
	DIR *dir = opendir(path); 
	if(!dir){
		fprintf(stderr, "%s: error: could not open directory %s\n", __FUNCTION__, path); 
		return -ENOENT; 
	}
	printf("%s: reading %s\n", __FUNCTION__, path); 
	struct dirent *ent = 0; 
	char fname[255]; 
	while((ent = readdir(dir))){
		if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue; 

		snprintf(fname, sizeof(fname), "%s/%s", path, ent->d_name); 
		
		printf("%s: found %s, type: %d\n", __FUNCTION__, fname, ent->d_type); 
		
		if(ent->d_type == DT_DIR) {
			rv |= _for_each_file(self, fname, base_path, on_file_cb);  
		} else  if(ent->d_type == DT_REG || ent->d_type == DT_LNK){
			rv |= on_file_cb(self, fname, base_path); 
		}
	}
	closedir(dir); 
	return rv; 
}


void app_init(struct app *self){
	uloop_init(); 
}

int app_connect_to_ubus(struct app *self, const char *path){
	self->ctx = ubus_connect(path); 
	if(!self->ctx) {
		perror("could not connect to ubus!\n"); 
		return -EIO; 
	}

	printf("connected as %x\n", self->ctx->local_id); 
	
	self->ctx->connection_lost = on_ubus_connection_lost; 	
	return 0; 
}

static int _load_script(struct app *self, const char *fname, const char *base_path){ 	
	int rv = 0; 
	struct script_object *script = calloc(1, sizeof(struct script_object)); 
	script_object_init(script);
	
	if(script_object_load(script, fname + strlen(base_path), fname) != 0){
		fprintf(stderr, "%s: error loading script script %s!\n", __FUNCTION__, fname);
		rv |= -EINVAL; 
	}
	if(script_object_register_on_ubus(script, self->ctx) != 0){ 
		script_object_destroy(script); 
		free(script); 
		rv |= -EINVAL;
	}
	return rv; 
}

int app_load_scripts(struct app *self, const char *root){
	return _for_each_file(self, root, NULL, _load_script); 
}

static void *_service_thread(void *pdata){
	const char *fname = (const char *)pdata; 
	struct lua_object *obj = calloc(1, sizeof(struct lua_object)); 
	printf("%s: loading lua service %s..\n", __FUNCTION__, fname); 
	lua_object_init(obj); 
	lua_object_load(obj, fname);
	printf("%s: lua object done %s\n", __FUNCTION__, fname); 
	return 0; 	
}

static int _load_service(struct app *self, const char *fname, const char *base_path){
	// TODO: this is just a test. Make it pretty and non-leaky
	pthread_t *thread = malloc(sizeof(pthread_t)); 
	pthread_create(thread, 0, _service_thread, strdup(fname)); 

	return 0; 
}

int app_load_services(struct app *self, const char *root){
	return _for_each_file(self, root, NULL, _load_service); 
}

void app_run(struct app *self){
	ubus_add_uloop(self->ctx); 
	uloop_run(); 
}

void app_destroy(struct app *self){
	ubus_free(self->ctx); 
}

int main(int argc, char **argv){
	static struct app app; 

	app_init(&app); 
	if(app_connect_to_ubus(&app, NULL) != 0){
		printf("failed to connect to ubus!\n"); 
		return -1; 
	}
	
	if(app_load_scripts(&app, UBUS_ROOT) != 0){ 
		printf("***** ERROR ******* could not load ubus scripts\n"); 
		app_destroy(&app); 
		return -1; 
	}
	
	if(app_load_services(&app, UBUS_SERVICE_ROOT) != 0){
		fprintf(stderr, "***** ERROR ***** could not load services!\n"); 
		app_destroy(&app); 
		return -1; 
	}

	app_run(&app); 
	
	app_destroy(&app); 

	return 0; 
}
