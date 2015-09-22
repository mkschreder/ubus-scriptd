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
#include <libubus.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>

#define UBUS_ROOT "/etc/ubus"

static struct blob_buf buf; 

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
	snprintf(fname, sizeof(fname), "%s%s", UBUS_ROOT, obj->name); 
	
	printf("%s: run %s\n", __FUNCTION__, fname); 
	
	if(stat(fname, &st) == 0){
		const char *resp = run_command("%s %s '%s'", &exit_code, fname, method, blobmsg_format_json(msg, true)); 
		if(!blobmsg_add_json_from_string(&buf, resp))
			return UBUS_STATUS_NO_DATA; 
	}
	
	ubus_send_reply(ctx, req, buf.head);

	return exit_code;
}


int _load_ubus_plugins(struct ubus_context *ctx, const char *path, const char *base_path){
	int rv = 0; 
	int exit_code = 0; 
	if(!base_path) base_path = path; 
	DIR *dir = opendir(path); 
	if(!dir){
		fprintf(stderr, "could not open directory %s\n", path); 
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
			_load_ubus_plugins(ctx, fname, base_path);  
		} else  if(ent->d_type == DT_REG || ent->d_type == DT_LNK){
			struct ubus_method *methods = 0; 
			size_t nmethods = 0; 
			if(strcmp(ent->d_name + strlen(ent->d_name) - 3, ".so") == 0){
				// try to load the so and see if it is a valid plugin
				void *dp = dlopen(fname, RTLD_NOW); 
				methods = dlsym(dp, "ubus_methods"); 
				if(!methods){
					fprintf(stderr, "error: could not find ubus_methods symbol in %s\n", fname); 
					continue; 
				}
			} else {
				char mstr[255]; 
				// check if the file is executable 
				strncpy(mstr, run_command("%s .methods", &exit_code, fname), sizeof(mstr)); 
				
				// extract methods into an array 
				nmethods = 1; 
				const char *mnames[64] = {0}; 
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
				
				printf(" - %d methods for %s\n", nmethods, fname); 
				
				methods = calloc(nmethods, sizeof(struct ubus_method));
				
				for(size_t c = 0; c < nmethods; c++){
					printf(" - registering %s\n", mnames[c]); 
					methods[c].name = strdup(mnames[c]); 
					methods[c].handler = rpc_shell_script;  
				}
			}

			if(!methods) continue; 
			
			char *obj_name = strdup(fname + strlen(base_path)); 
			char obj_type_name[64]; 
			
			strncpy(obj_type_name, obj_name, sizeof(obj_type_name)); 
			for(size_t c = 0; c < strlen(obj_name); c++) if(obj_type_name[c] == '/') obj_type_name[c] = '-'; 
			
			printf("Registering ubus object %s (%s)\n", obj_name, obj_type_name); 
			
			struct ubus_object *obj = calloc(1, sizeof(struct ubus_object)); 
			struct ubus_object_type *obj_type = calloc(1, sizeof(struct ubus_object_type)); 
			
			obj_type->name = strdup(obj_type_name); 
			obj_type->id = 0; 
			obj_type->n_methods = nmethods; 
			obj_type->methods = methods;
			
			obj->name = obj_name;
			obj->type = obj_type;
			obj->methods = methods;
			obj->n_methods = nmethods; 
			
			rv |= ubus_add_object(ctx, obj); 
		}
	}
	return rv; 
}

int main(int argc, char **argv){
	static struct ubus_context *ctx = 0; 
	
	uloop_init(); 

	ctx = ubus_connect(NULL); 
	if(!ctx) {
		perror("could not connect to ubus!\n"); 
		return -EIO; 
	}

	printf("connected as %x\n", ctx->local_id); 
	
	ctx->connection_lost = on_ubus_connection_lost; 
	
	if(0 <= _load_ubus_plugins(ctx, UBUS_ROOT, NULL)){ 
		ubus_add_uloop(ctx); 
		uloop_run(); 
	}

	ubus_free(ctx); 

	return 0; 
}
