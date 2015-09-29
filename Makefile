BUILD_DIR=build_dir
TARGET=ubus-scriptd
SOURCE=src/main.c
OBJECTS=$(patsubst %.c,%.o,$(SOURCE))
CFLAGS+=-Wall -Werror -std=c99

all: $(BUILD_DIR) $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR): 
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(TARGET): $(addprefix $(BUILD_DIR)/,$(OBJECTS)) 
	$(CC) -o $@ $< -lubus -lubox -lblobmsg_json -ldl

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
