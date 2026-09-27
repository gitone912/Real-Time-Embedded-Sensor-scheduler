# The directory that contains FreeRTOS source code
FREERTOS_ROOT := ../FreeRTOS/FreeRTOS

# Demo code
DEMO_PROJECT := .
TESTS_REPORTS := ../test_reports

# FreeRTOS kernel
KERNEL_DIR := $(FREERTOS_ROOT)/Source
KERNEL_PORT_DIR := $(KERNEL_DIR)/portable/GCC/ARM_CM3

# Where to store all the generated files (objects, elf and map)
OUTPUT_DIR := ./Output
#default log directory docker
LOG_DIR ?= logs

DEMO_NAME := demo
ELF := $(OUTPUT_DIR)/$(DEMO_NAME).elf
MAP := $(OUTPUT_DIR)/$(DEMO_NAME).map

# Compiler toolchain
CC := arm-none-eabi-gcc
LD := arm-none-eabi-gcc
SIZE := arm-none-eabi-size

# Target embedded board and CPU
MACHINE := mps2-an385
CPU := cortex-m3

# QEMU FLAGS
QEMU_FLAGS += -nographic -serial file:/app/output/uart.log

# Use -s to connect to gdb port 1234 and -S to wait before executing
QEMU_FLAGS_DBG = -s -S

INCLUDE_DIRS = -I$(KERNEL_DIR)/include -I$(KERNEL_PORT_DIR)
INCLUDE_DIRS += -I$(DEMO_PROJECT)

VPATH += $(KERNEL_DIR) $(KERNEL_PORT_DIR) $(KERNEL_DIR)/portable/MemMang
VPATH += $(DEMO_PROJECT)

# Include paths. See INCLUDE_DIRS
CFLAGS = $(INCLUDE_DIRS)

# Don't include standard libraries
CFLAGS += -ffreestanding

# Target ARM cortex M3 processor
CFLAGS += -mcpu=$(CPU)
# Use the thumb 16 bits instruction set. Required for cortex-M
CFLAGS += -mthumb

# Print all the most common warnings
CFLAGS += -Wall

# Print extra warnings, not included in -Wall
CFLAGS += -Wextra

# Print warnings about variable shadowing (var name = outer scope var name)
CFLAGS += -Wshadow

# Include debug information (-g) with maximum level of detail (3)
CFLAGS += -g3

# Optimize (-O) the size (s) of the generated executable
CFLAGS += -Os

# Place each function in a dedicated section (see -gc-sections in LDFLAGS)
CFLAGS += -ffunction-sections

# Place each variable in a dedicated section (see -gc-sections in LDFLAGS)
CFLAGS += -fdata-sections

# Specify the linker script
LDFLAGS = -T ./mps2_m3.ld

# Don't use the standard start-up files
LDFLAGS += -nostartfiles

# Optimize compilation to reduce memory
LDFLAGS += -specs=nano.specs

# Optimize compilation targeting a system without any operating system
LDFLAGS += -specs=nosys.specs

# Generate map file with memory layout. Pass option to the linker (-Xlinker)
LDFLAGS += -Xlinker -Map=$(MAP)

# Remove unused sections. Used with -ffunction-sections and -fdata-sections
# optimized memory. Pass option to the linker (-Xlinker)
LDFLAGS += -Xlinker --gc-sections

# Link specifically for cortex M3 and thumb instruction set
LDFLAGS += -mcpu=$(CPU) -mthumb

# Kernel files
SOURCE_FILES += $(KERNEL_DIR)/list.c
SOURCE_FILES += $(KERNEL_DIR)/tasks.c
SOURCE_FILES += $(KERNEL_DIR)/queue.c
SOURCE_FILES += $(KERNEL_DIR)/portable/GCC/ARM_CM3/port.c
SOURCE_FILES += $(DEMO_PROJECT)/sensor.c
SOURCE_FILES += $(DEMO_PROJECT)/sensor_queue.c
SOURCE_FILES += $(DEMO_PROJECT)/sensor_task.c
# Demo files
SOURCE_FILES += $(DEMO_PROJECT)/main.c
SOURCE_FILES += $(DEMO_PROJECT)/uart.c
SOURCE_FILES += $(DEMO_PROJECT)/trace.c
SOURCE_FILES += $(DEMO_PROJECT)/trace_dumper.c
SOURCE_FILES += $(DEMO_PROJECT)/timeline.c
SOURCE_FILES += $(DEMO_PROJECT)/timeline_internal.c
SOURCE_FILES += $(DEMO_PROJECT)/tasks_generated.c
SOURCE_FILES += $(DEMO_PROJECT)/workloads.c


# Start-up code
SOURCE_FILES += ./startup.c

# Create list of object files with the same names of the sources
OBJS = $(SOURCE_FILES:%.c=%.o)

# Remove path from object filename
OBJS_NOPATH = $(notdir $(OBJS))

# Prepend output dir to object filenames
OBJS_OUTPUT = $(OBJS_NOPATH:%.o=$(OUTPUT_DIR)/%.o)

############################## Task generation ##############################


TASK_GEN_SCRIPT := Tools/workload_gen.py
# Make TASK_CONFIG overridable from command line (default: config/tasks.json)
TASK_CONFIG    ?= suite/00_test.json
TASK_GEN_C      := tasks_generated.c
TASK_GEN_H      := tasks_generated.h

$(TASK_GEN_C) $(TASK_GEN_H): $(TASK_CONFIG) $(TASK_GEN_SCRIPT)
	python3 $(TASK_GEN_SCRIPT) --input $(TASK_CONFIG)
all: $(TASK_GEN_C) $(ELF)



$(ELF): $(OBJS_OUTPUT) ./mps2_m3.ld Makefile
	echo "\n\n--- Final linking ---\n"
	$(LD) $(LDFLAGS) $(OBJS_OUTPUT) -o $(ELF)
	$(SIZE) $(ELF)

$(OUTPUT_DIR)/%.o : %.c  Makefile $(OUTPUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

cleanobj:
	rm -f $(OUTPUT_DIR)/*.o

clean:
	rm -rf $(ELF) $(MAP) $(OUTPUT_DIR)/*.o $(OUTPUT_DIR) logs/*.log $(TASK_GEN_C) $(TASK_GEN_H)
remove_reports: # Remove test reports
	rm -rf $(TESTS_REPORTS)/*

qemu_start:
	mkdir -p $(LOG_DIR)
	qemu-system-arm \
		-machine $(MACHINE) \
		-cpu $(CPU) \
		-kernel $(ELF) \
		-monitor none \
		-nographic \
		-semihosting \
		-serial stdio \
		-icount shift=6,align=off,sleep=on \
	| tee $(LOG_DIR)/uart.log

qemu_debug:
	qemu-system-arm -machine $(MACHINE) -cpu $(CPU) -kernel \
	$(ELF) -monitor none -nographic -serial stdio $(QEMU_FLAGS_DBG)
gdb_start:
	gdb-multiarch $(ELF)


############################## Docker Command ##############################
DOCKER_CMD := docker run  -it --rm -v "$(PWD)/..:/app" -w /app/Demo embedded-env make

docker_build: ## Build the project inside a Docker container
	$(DOCKER_CMD) TASK_CONFIG=$(TASK_CONFIG) all
docker_clean: ## Clean the project inside a Docker container
	$(DOCKER_CMD) clean
docker_qemu: ## Start QEMU inside a Docker container
	$(DOCKER_CMD) TASK_CONFIG=$(TASK_CONFIG) qemu_start
docker_debug: ## Start QEMU and GDB inside a Docker container
	$(DOCKER_CMD) qemu_debug
