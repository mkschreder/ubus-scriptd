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
#include <dlfcn.h>
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

#define UBUS_ROOT "/usr/lib/ubus"

static struct blob_buf buf; 

struct script_object {
	struct ubus_object *ubus_object; 
	struct ubus_method *ubus_methods; 
	struct ubus_context *ubus_ctx; 
	struct list_head list; 
	uint8_t free_ubus_object; 
}; 

struct app {
	struct ubus_context *ctx; 
	struct list_head scripts;  
}; 

void on_ubus_connection_lost(struct ubus_context *ctx){
	printf("ubus connection lost!\n"); 
}

static const char*
run_command(const char *pFmt, int *exit_code, ...)
{
	va_list ap;
	char cmd[256] = {0};
		
	va_start(ap, exit_code);
	vsnprintf(cmd, sizeof(cmd), pFmt, ap);
	va_end(ap);

	FILE *pipe = 0;
	static char buffer[16384] = {0};
	memset(buffer, 0, sizeof(buffer)); 
	
	if ((pipe = popen(cmd, "r"))){
		char *ptr = buffer; 
		while(fgets(ptr, sizeof(buffer) - (ptr - buffer), pipe)){
			ptr+=strlen(ptr); 
			//*ptr = '\0'; 
		}
		
		*exit_code = WEXITSTATUS(pclose(pipe));

		if(strcmp(buffer + strlen(buffer) - 1, "\n") == 0) buffer[strlen(buffer)-1] = 0; 
		
		if (ptr != buffer)
			return (const char*)buffer;
		else
			return "{}";
	} else {
		return "{}"; 
	}
}

static int rpc_shell_script(struct ubus_context *ctx, struct ubus_object *obj,
		  struct ubus_request_data *req, const char *method,
		  struct blob_attr *msg)
{
	blob_buf_init(&buf, 0);
	
	struct stat st; 
	char fname[255]; 

	// all objects are preceded with slash so no need for slash in the path between root and objname
	snprintf(fname, sizeof(fname), "%s/%s", UBUS_ROOT, obj->name); 
	
	printf("%s: run %s\n", __FUNCTION__, fname); 
	
	if(stat(fname, &st) == 0){
		int exit_code = 0; 
		const char *resp = run_command("%s %s '%s'", &exit_code, fname, method, blobmsg_format_json(msg, true)); 
		if(!blobmsg_add_json_from_string(&buf, resp)){
			blobmsg_add_string(&buf, "error", "could not add json"); 
			blobmsg_add_string(&buf, "json", resp); 
			return ubus_send_reply(ctx, req, buf.head); 
		}
	}
	
	return ubus_send_reply(ctx, req, buf.head);
}

static int _parse_methods_json(struct script_object *self, struct ubus_object *obj, const char *str){
	struct blob_buf buf; 
	
	// BUG in blobbuf
	memset(&buf, 0, sizeof(buf)); 

	blob_buf_init(&buf,0); 
	
	printf("parsing %s\n", str); 
	if(!blobmsg_add_json_from_string(&buf, str)) {
		printf("%s: warning: could not parse json!\n", __FUNCTION__); 
		blob_buf_free(&buf); 
		return -EINVAL;  
	}

	int memsize = 0, nmethods = 0, nparams = 0; 
	struct blob_attr *attr = 0; 
	int l = blob_len(buf.head); 
	__blob_for_each_attr(attr, blob_data(buf.head), l){
		memsize += sizeof(struct ubus_method); 
		memsize += strlen(blobmsg_name(attr)) + 1; // include last \0
		nmethods ++; 
		switch(blob_id(attr)){
			case BLOBMSG_TYPE_TABLE: 
			case BLOBMSG_TYPE_ARRAY: {
				struct blob_attr *a = 0; 
				int len = blobmsg_data_len(attr); 
				__blob_for_each_attr(a, blobmsg_data(attr), len){
					memsize += sizeof(struct blobmsg_policy); 
					memsize += strlen(blobmsg_name(a)) + 1; // extra for name 
					nparams ++; 
				}
			} break; 
		}
	}
	
	char *memory = malloc(memsize); 
	memset(memory, 0, memsize); 

	printf("%s: allocating %d bytes for methods\n", __FUNCTION__, memsize); 
	
	struct ubus_method *method = (struct ubus_method*)memory; 
	self->ubus_methods = method; 
	struct blobmsg_policy *policy = (struct blobmsg_policy*)(((char*)method) + sizeof(struct ubus_method) * nmethods); 
	char *strings = (char*)(((char*)policy) + sizeof(struct blobmsg_policy) * nparams); 

	obj->methods = method; 

	l = blob_len(buf.head); 
	__blob_for_each_attr(attr, blob_data(buf.head), l){
		struct blobmsg_hdr *hdr = blob_data(attr); 
		printf(" - found %s\n", hdr->name); 
		switch(blob_id(attr)){
			case BLOBMSG_TYPE_TABLE: 
			case BLOBMSG_TYPE_ARRAY: {
				method->policy = policy; 
				
				struct blob_attr *a = 0; 
				int len = blobmsg_data_len(attr); 
				__blob_for_each_attr(a, blobmsg_data(attr), len){
					if(blob_id(a) == BLOBMSG_TYPE_STRING){
						const char *type_string = "string"; 
						printf(" - blobattr: %s %s %s\n", blobmsg_name(attr), blobmsg_name(a), (char*)blobmsg_data(a)); 
						const char *name = ""; 
						if(strlen(blobmsg_name(a)) > 0){
							name = blobmsg_name(a); 
							type_string = blobmsg_data(a); 
						} else {
							name = blobmsg_data(a); 
						}
						
						// store policy name after the policys array and increment the pointer
						policy->name = strings; 
						strcpy(strings, name); // this copy is not dangerous as long as we allocated memory above. 
						strings += strlen(strings) + 1; 

						if(strcmp(type_string, "string") == 0)
							policy->type = BLOBMSG_TYPE_STRING; 
						else if(strcmp(type_string, "bool") == 0)
							policy->type = BLOBMSG_TYPE_INT8; 
						else if(strcmp(type_string, "int") == 0 || strcmp(type_string, "int32") == 0)
							policy->type = BLOBMSG_TYPE_INT32; 
						else 
							policy->type = BLOBMSG_TYPE_STRING; 
						
						policy++; 
						method->n_policy++; 
					} 
				}
				printf("%s: method %s with %d params\n", __FUNCTION__, hdr->name, nparams); 
				
				strcpy(strings, (char*)hdr->name); 
				method->name = strings; 
				strings += strlen(strings) + 1; 
				
				method->handler = rpc_shell_script; 

				method++;
				obj->n_methods++; 
			} break; 
		}
	}
	blob_buf_free(&buf); 
	return 0; 
}

static int _parse_methods_comma_list(struct script_object *self, struct ubus_object *obj, const char *str){
	// calculate necessary memory and allocate chunk
	int nmethods = 1, memsize = sizeof(struct ubus_method); 
	for(const char *name = str; *name; name++){
		if(*name == ','){
			name++; 
			memsize += sizeof(struct ubus_method); 
			nmethods++; 
		}
	}
	// reserve space for method names. Note that commas will become string endings and we add one byte for last string ending. 
	memsize += strlen(str) + 1; 

	struct ubus_method *method = malloc(memsize); 
	self->ubus_methods = method; 
	memset(method, 0, sizeof(memsize)); 
	char *strings = (char*)(method + nmethods); 
	
	obj->methods = method; 

	for(const char *name = str; *name; name++) { 
		method->name = strings; 
		while(*name && *name != ',') {
			*strings++ = *name++; 
		}
		*strings++ = 0; 
		printf(" - parsed method %s\n", method->name); 
		method->handler = rpc_shell_script; 
		method++; 
	}
	obj->n_methods = nmethods; 
	return 0; 
}

void script_object_init(struct script_object *self){
	INIT_LIST_HEAD(&self->list); 
	self->ubus_object = 0; 
	self->free_ubus_object = 0; 
	self->ubus_ctx = 0; 
}

int script_object_load(struct script_object *self, const char *path){	
	const char *objname = path + strlen(path); 
	for(; objname != path; objname--){
		if(*(objname - 1) == '/'){
			break;
		}
	}
	
	int memsize = sizeof(struct ubus_object) + sizeof(struct ubus_object_type) + (strlen(objname) + 1) * 2;  
	struct ubus_object *obj = malloc(memsize); 
	memset(obj, 0, memsize); 
	struct ubus_object_type *type = (struct ubus_object_type*)(((char*)obj) + sizeof(struct ubus_object)); 
	char *strings = ((char*)type) + sizeof(struct ubus_object_type); 
	
	strcpy(strings, objname); 
	obj->name = strings; strings += strlen(objname) + 1; 
	strcpy(strings, objname); 
	for(char *ch = strings; *ch; ch++) if(*ch == '/') *ch = '-'; 
	type->name = strings; strings += strlen(objname) + 1; 
		
	if(strcmp(path + strlen(path) - 3, ".so") == 0){
		// try to load the so and see if it is a valid plugin
		void *dp = dlopen(path, RTLD_NOW); 
		obj->methods = dlsym(dp, "ubus_methods"); 
		if(!obj->methods){
			fprintf(stderr, "error: could not find ubus_methods symbol in %s\n", path); 
			free(obj); 
			return -EINVAL;  
		}
	} else {
		int exit_code = 0; 
		const char *mstr = run_command("%s .methods", &exit_code, path); 
		
		// extract methods into an array 
		if(_parse_methods_json(self, obj, mstr) != 0){
			printf("%s: unable to parse json, tryging comma list...\n", __FUNCTION__); 
			if(_parse_methods_comma_list(self, obj, mstr) != 0){
				fprintf(stderr, "%s: unable to parse comma list.. bailing out!\n", __FUNCTION__); 
				free(obj); 
				return -EINVAL; 
			}
		}
		
		printf(" - %d methods for %s\n", obj->n_methods, path); 
	}
	
	if(!obj->methods) {
		fprintf(stderr, "%s: unable to load ubus methods for %s\n", __FUNCTION__, obj->name); 
		free(obj); 
		return -EINVAL; 
	}

	type->id = 0; 
	type->n_methods = obj->n_methods; 
	type->methods = obj->methods;

	obj->type = type;
	
	self->ubus_object = obj; 

	return 0; 
}

void script_object_destroy(struct script_object *self){
	if(self->ubus_object){
		if(self->ubus_ctx) ubus_remove_object(self->ubus_ctx, self->ubus_object); 	
		if(self->ubus_methods) free(self->ubus_methods); 
		free(self->ubus_object); 
		self->ubus_object = 0; 
		self->ubus_methods = 0; 
		self->ubus_ctx = 0; 
	}
}

int script_object_register_on_ubus(struct script_object *self, const char *name, struct ubus_context *ctx){
	if(!self->ubus_object) return -EINVAL;
	if(self->ubus_ctx) return -EEXIST; 

	printf("Registering ubus object %s (%s)\n", self->ubus_object->name, self->ubus_object->type->name); 
	
	if(ubus_add_object(ctx, self->ubus_object) != 0){
		fprintf(stderr, "%s: error: could not add ubus object (%s)!\n", __FUNCTION__, name); 
		return -EIO; 
	}

	self->ubus_ctx = ctx; 

	return 0; 
}

static int _load_ubus_plugins(struct app *self, const char *path, const char *base_path){
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
			rv |= _load_ubus_plugins(self, fname, base_path);  
		} else  if(ent->d_type == DT_REG || ent->d_type == DT_LNK){
			struct script_object *script = calloc(1, sizeof(struct script_object)); 
			script_object_init(script); 
			if(script_object_load(script, fname) != 0){
				fprintf(stderr, "%s: error loading script script %s!\n", __FUNCTION__, fname);
				rv |= -EINVAL; 
			}
			const char *basename = fname + strlen(fname); 
			for(; basename != fname; basename--){
				if(*(basename - 1) == '/'){
					break;
				}
			}
			if(script_object_register_on_ubus(script, fname + strlen(base_path), self->ctx) != 0){ 
				script_object_destroy(script); 
				free(script); 
				rv |= -EINVAL;
			}
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

int app_load_scripts(struct app *self, const char *root){
	return _load_ubus_plugins(self, root, NULL); 
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

	app_run(&app); 
	
	app_destroy(&app); 

	return 0; 
}
