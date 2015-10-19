BUILD_DIR=build_dir
TARGET=ubus-scriptd
SOURCE=src/main.c src/lua_object.c src/script_object.c
OBJECTS=$(patsubst %.c,%.o,$(SOURCE))
CFLAGS+=-Wall -Werror -std=gnu99 

all: $(BUILD_DIR) $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR): 
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(TARGET): $(addprefix $(BUILD_DIR)/,$(OBJECTS)) 
	$(CC) -o $@ $^ -lpthread -lubus -lubox -lblobmsg_json -ldl -llua -lm

$(BUILD_DIR)/%.o: %.c $(wildcard src/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean: 
	rm -rf build_dir
