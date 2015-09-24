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
	int exit_code = UBUS_STATUS_NO_DATA; 
	char fname[255]; 

	// all objects are preceded with slash so no need for slash in the path between root and objname
	snprintf(fname, sizeof(fname), "%s/%s", UBUS_ROOT, obj->name); 
	
	printf("%s: run %s\n", __FUNCTION__, fname); 
	
	if(stat(fname, &st) == 0){
		const char *resp = run_command("%s %s '%s'", &exit_code, fname, method, blobmsg_format_json(msg, true)); 
		if(!blobmsg_add_json_from_string(&buf, resp))
			return UBUS_STATUS_NO_DATA; 
	}
	
	ubus_send_reply(ctx, req, buf.head);

	return exit_code;
}

static struct ubus_method *parse_methods_json(const char *str, int *mcount){
	struct blob_buf buf; 
	
	// BUG in blobbuf
	memset(&buf, 0, sizeof(buf)); 

	blob_buf_init(&buf,0); 
	
	printf("parsing %s\n", str); 
	if(!blobmsg_add_json_from_string(&buf, str)) {
		printf("%s: warning: could not parse json!\n", __FUNCTION__); 
		return 0;  
	}

	struct blob_attr *attr = 0; 
	struct blob_attr *head = blob_data(buf.head); 
	int len = blob_len(buf.head); 
	int nmethods = 0; 

	__blob_for_each_attr(attr, head, len){
		nmethods++; 
	}

	printf("%s: allocating %d slots for methods\n", __FUNCTION__, nmethods); 

	struct ubus_method *methods = calloc(nmethods, sizeof(struct ubus_method)); 
	
	nmethods = 0; 
	head = blob_data(buf.head); 
	len = blob_len(buf.head); 
	
	__blob_for_each_attr(attr, head, len){
		struct blobmsg_hdr *hdr = blob_data(attr); 
		printf(" - found %s\n", hdr->name); 
		switch(blob_id(attr)){
			case BLOBMSG_TYPE_TABLE: 
			case BLOBMSG_TYPE_ARRAY: {
				struct blob_attr *a = 0; 
				int l = blobmsg_data_len(attr); 
				int nparams = 0; 
					
				__blob_for_each_attr(a, blobmsg_data(attr), l){
					nparams++; 
				}
					
				struct blobmsg_policy *policy = calloc(nparams, sizeof(struct blobmsg_policy)); 

				l = blob_len(attr); 
				nparams = 0; 
				__blob_for_each_attr(a, blobmsg_data(attr), l){
					if(blob_id(a) == BLOBMSG_TYPE_STRING){
						const char *type_string = "string"; 
						printf(" - blobattr: %s %s %s\n", blobmsg_name(attr), blobmsg_name(a), (char*)blobmsg_data(a)); 
						if(strlen(blobmsg_name(a)) > 0){
							policy[nparams].name = strdup(blobmsg_name(a)); 
							type_string = blobmsg_data(a); 
						} else {
							policy[nparams].name = strdup(blobmsg_data(a));
						}
						if(strcmp(type_string, "string") == 0)
							policy[nparams].type = BLOBMSG_TYPE_STRING; 
						else if(strcmp(type_string, "bool") == 0)
							policy[nparams].type = BLOBMSG_TYPE_INT8; 
						else if(strcmp(type_string, "int") == 0 || strcmp(type_string, "int32") == 0)
							policy[nparams].type = BLOBMSG_TYPE_INT32; 
						else 
							policy[nparams].type = BLOBMSG_TYPE_STRING; 
						nparams++;
					} 
				}
				printf("%s: method %s with %d params\n", __FUNCTION__, hdr->name, nparams); 
				methods[nmethods].name = strdup((char*)hdr->name); 
				methods[nmethods].policy = policy; 
				methods[nmethods].n_policy = nparams; 
				methods[nmethods].handler = rpc_shell_script; 
				nmethods++;
			} break; 
		}
	}
	blob_buf_free(&buf); 

	*mcount = nmethods; 
	return methods; 
}

static struct ubus_method* parse_methods_comma_list(const char *str, int *mcount){
	char mstr[512]; 
	strncpy(mstr, str, sizeof(mstr)); 
	const char *mnames[64] = {0}; 
	int nmethods = 1; 
	mnames[0] = mstr; 
	int len = strlen(mstr); 
	for(int c = 0; c < len; c++) { 
		if(mstr[c] == ',') {
			mstr[c] = 0; 
			mnames[nmethods] = mstr + c + 1; 
			nmethods++; 
			if(nmethods == sizeof(mnames)/sizeof(mnames[0]) - 1){
				printf("Error: maximum number of methods!\n"); 
				break; 
			}
		} else if(mstr[c] == '\n'){
			break; 
		}
	}
	
	struct ubus_method *methods = calloc(nmethods, sizeof(struct ubus_method));

	for(size_t c = 0; c < nmethods; c++){
		printf(" - registering %s\n", mnames[c]); 
		methods[c].name = strdup(mnames[c]); 
		methods[c].handler = rpc_shell_script;  
	}

	*mcount = nmethods; 

	return methods; 
}

void script_object_init(struct script_object *self){
	INIT_LIST_HEAD(&self->list); 
	self->ubus_object = 0; 
	self->free_ubus_object = 0; 
	self->ubus_ctx = 0; 
}

int script_object_load(struct script_object *self, const char *path){
	struct ubus_method *methods = 0; 
	int nmethods = 0;
	if(strcmp(path + strlen(path) - 3, ".so") == 0){
		// try to load the so and see if it is a valid plugin
		void *dp = dlopen(path, RTLD_NOW); 
		methods = dlsym(dp, "ubus_methods"); 
		if(!methods){
			fprintf(stderr, "error: could not find ubus_methods symbol in %s\n", path); 
			return -EINVAL;  
		}
	} else {
		int exit_code = 0; 
		const char *mstr = run_command("%s .methods", &exit_code, path); 
		
		// extract methods into an array 
		if(!(methods = parse_methods_json(mstr, &nmethods))){
			printf("%s: unable to parse json, tryging comma list...\n", __FUNCTION__); 
			methods = parse_methods_comma_list(mstr, &nmethods); 
		}
		
		if(!methods) {
			fprintf(stderr, "%s: error loading methods from %s\n", __FUNCTION__, path); 
			return -EINVAL;  
		}

		printf(" - %d methods for %s\n", nmethods, path); 
	}
	
	struct ubus_object *obj = calloc(1, sizeof(struct ubus_object)); 
	struct ubus_object_type *obj_type = calloc(1, sizeof(struct ubus_object_type)); 
	
	obj_type->name = 0; 
	obj_type->id = 0; 
	obj_type->n_methods = nmethods; 
	obj_type->methods = methods;
	
	obj->name = 0;
	obj->type = obj_type;
	obj->methods = methods;
	obj->n_methods = nmethods; 
	
	self->ubus_object = obj; 
	self->free_ubus_object = 1; 

	return 0; 
}

void script_object_destroy(struct script_object *self){
	if(!self->free_ubus_object) return; 
	struct ubus_object *obj = self->ubus_object;
	if(self->ubus_ctx) ubus_remove_object(self->ubus_ctx, self->ubus_object); 	
	if(obj->name) free((char*)obj->name); 
	if(obj->type->name) free((char*)obj->type->name); 
	/*if(obj->methods){
		for(int c = 0; c < obj->n_methods; c++){
			const struct ubus_method *m = &obj->methods[c];
			if(m->name) free(m->name); 
			for(int j = 0; j < m->n_policy; j++){
				const struct ubus_policy *p = &m->policy[j]; 
				if(p->name) free((char*)p->name);  
			}
			free(m->policy); 
		}
		free(obj->methods); 
	}*/
	free(self->ubus_object); 
}

int script_object_register_on_ubus(struct script_object *self, const char *name, struct ubus_context *ctx){
	if(!self->ubus_object) return -EINVAL;

	char obj_type_name[64]; 
	
	strncpy(obj_type_name, name, sizeof(obj_type_name)); 
	for(size_t c = 0; c < strlen(name); c++) if(obj_type_name[c] == '/') obj_type_name[c] = '-'; 
	
	self->ubus_object->name = strdup(name); 
	self->ubus_object->type->name = strdup(obj_type_name); 

	printf("Registering ubus object %s (%s)\n", name, obj_type_name); 
	
	if(ubus_add_object(ctx, self->ubus_object) != 0){
		//free(self->ubus_object->name); 
		//free(self->ubus_object->type->name); 
		fprintf(stderr, "%s: error: could not add ubus object (%s)!\n", __FUNCTION__, name); 
		self->ubus_object->name = self->ubus_object->type->name = 0; 
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

void app_free(struct app *self){
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
		app_free(&app); 
		return -1; 
	}
	
	app_run(&app); 
	
	app_free(&app); 

	return 0; 
}
