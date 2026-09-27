# REALREADME

## HOW TO? 
Command line in the Makefile

``` make```  It builds the project and generates the .elf file  
``` make qemu_start ```    It starts qemu and run the .elf file  
``` make qemu_debug```   It starts qemu with gdb attached  
``` make gdb_start```   It starts gdb  
``` make cleanobj```   It cleans the object files  
``` make clean```  It cleans all the build files
### DOCKER GUIDE
Run in terminal, located in the Dockerfile folder, 
``` docker build -t embedded-env . ```
Then you can use:
- ``` make docker_clean ``` : as the above, to clean all the build files
- ``` make docker_build ``` : it builds the project
- ``` make docker_qemu ``` : it runs the program


---

## STRUCTURE
The project is organized into the following directories and files:
- FreeRTOS/  -> FreeRTOS kernel files
- Src/  -> Source files
  - Config/  -> Configuration files
  - Scheduler/ → Scheduler-related files
  - Drivers/  -> Hardware driver files
  - main.c → Main application file   
  - trace.c → Trace functionality file
  - trace_dumper.c → Trace dumper functionality file
  - uart.c → UART communication file
  - timer.c → Timer functionality file
  - startup.c → Startup code for the microcontroller  
- Makefile → Makefile for building the project

- REALREADME.md → This file
## GENERAL INFO

### How subframes'ids are generated?

Subframes' ids are assigne based on the start time of each task. the id is calculated as follows:
id = task_start_time / subframe_duration
Our tasks must be encapsulated exactly in one subframe, so the start time and end time must fit within the subframe boundaries.
---
## HOW TO MANAGE MAKEFILE?
``` make help ```  It shows all the available targets
### If you want to add a new source file:
1) Add the source file to the appropriate directory in Src/
2) Add the source file to the appropriate variable in the Makefile (SOURCES_FILES)

### If you want to add a new directory:
1) Add the directory path to the INCLUDES variable in the Makefile if It contains header files
2) Add the directory path to the VPATH variable in the Makefile if It contains source files

### If you want to change the name of the .elf file:
1) Change the DEMO_NAME variable in the Makefile


