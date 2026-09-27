Real-Time Embedded Sensor Scheduler

A FreeRTOS-based real-time embedded system for the ARM Cortex-M3 platform, extended with a simulated temperature-sensing pipeline, static inter-task communication, runtime telemetry, deadline monitoring, and QEMU-based validation.

The project combines a time-triggered scheduler with an application pipeline:

Sensor → Static Queue → Processing Task

The scheduler manages Hard Real-Time (HRT) and Soft Real-Time (SRT) tasks across repeated major frames while the application collects and processes simulated sensor data.

Project Overview

This project was developed to study and demonstrate practical embedded real-time concepts:

FreeRTOS task scheduling

Time-triggered execution

HRT and SRT task management

ARM Cortex-M3 execution

Static memory allocation

Inter-task communication using FreeRTOS queues

Sensor-data acquisition and processing

UART runtime telemetry

Deadline monitoring

Fault diagnosis and stack/context debugging

QEMU-based embedded-system validation

The scheduler framework is based on the existing EOSproject / FreeRTOS time-triggered scheduling framework and has been extended with application-level embedded functionality and debugging work.

Upstream project:
https://github.com/marco-pedron/EOSproject

Attribution: This repository is an extension of an existing FreeRTOS/time-triggered scheduler framework. Original source attribution and licensing are retained; the application-level sensor pipeline, telemetry, integration, debugging, and project-specific changes are the work added in this repository.

System Architecture

                    +----------------------+
                    | Time-Triggered       |
                    | FreeRTOS Scheduler   |
                    +----------+-----------+
                               |
                +--------------+--------------+
                |                             |
          HRT Tasks                       SRT Tasks
         HT1 / HT2 / HT3             ST1 / ST2 / ST3
                                          |
                                          v
                               +---------------------+
                               | Temperature Sensor  |
                               |   Simulation Task   |
                               +----------+----------+
                                          |
                                          | SensorData_t
                                          v
                               +---------------------+
                               | Static FreeRTOS     |
                               | Queue               |
                               +----------+----------+
                                          |
                                          v
                               +---------------------+
                               | Sensor Processing   |
                               | Task                |
                               +----------+----------+
                                          |
                                          v
                               +---------------------+
                               | UART Telemetry      |
                               | Statistics / Logs   |
                               +---------------------+

Application Pipeline

1. Sensor simulation

A lightweight simulated temperature sensor generates timestamped readings:

typedef struct {
    uint32_t timestamp;
    int32_t temperature_c10;
} SensorData_t;

Temperature is represented in tenths of a degree Celsius to avoid unnecessary floating-point state in the core data path.

Example telemetry:

SENSOR: t=304 temp=30.0C status=OK

2. Static queue

Sensor data is passed between tasks through a statically allocated FreeRTOS queue.

Sensor Task
     |
     v
Static Queue
     |
     v
Processing Task

The queue avoids dynamic allocation for this application and is compatible with the project's static-allocation configuration.

3. Processing task

The processing task consumes sensor samples and maintains runtime counters for:

samples received

samples processed

queue drops

high-temperature events

Example:

SENSOR STATS: samples=310 processed=310 drops=0 high_temp=0

4. Runtime diagnostics

UART output is used to observe:

major-frame transitions

sensor readings

task execution

HRT/SRT activity

deadline monitoring

queue statistics

scheduler overhead

fault diagnostics

Real-Time Scheduling

The project separates workload into Hard Real-Time (HRT) and Soft Real-Time (SRT) tasks.

Example generated schedule:

Task

Type

Start

End

HT1

HRT

12

17

HT2

HRT

23

30

HT3

HRT

37

40

ST1

SRT

0

0

ST2

SRT

0

0

ST3

SRT

0

0

The application maps the SRT tasks to the embedded pipeline:

ST1 → vSensorTask
ST2 → vSensorProcessingTask
ST3 → vGenericTaskWrapper

The system runs the schedule repeatedly over major frames.

Key Contributions

Embedded application layer

Added a simulated temperature sensor module

Added a reusable SensorData_t structure

Added static FreeRTOS queue infrastructure

Implemented Sensor → Queue → Processor communication

Added runtime counters and status reporting

Added UART telemetry for live system observation

Scheduler integration

Integrated the application tasks into the generated HRT/SRT schedule

Connected the scheduler workload generator with the embedded application

Configured task functions and task stack sizes

Preserved the original time-triggered scheduling workflow

Debugging and fault analysis

During development, task-context/stack corruption was investigated using:

ARM addr2line

ARM objdump

QEMU execution

FreeRTOS scheduler/port source inspection

HardFault register dumps

The HardFault handler was extended to report registers such as:

PC
LR
PSP
R0-R3
R12
xPSR

This made it possible to identify failures occurring during FreeRTOS PendSV context restoration and to stabilize the demo execution path.

Validation

The project was executed using an ARM Cortex-M3 target under QEMU.

A stable validation run demonstrated:

samples      = 310
processed    = 310
queue drops  = 0
high-temp    = 0

The run also showed:

repeated major-frame execution

HRT/SRT task activity

Sensor → Queue → Processor communication

scheduler overhead around 1.18% in the observed run

continued execution through many major frames

no HardFault during the stable demonstration run

The scheduler's deadline-monitoring output also identified occasional HT3 deadline misses during some frames, demonstrating that the monitoring and diagnostic path is active rather than assuming perfect execution.

Repository Structure

.
├── Demo/
│   ├── FreeRTOS/
│   ├── Headers/
│   ├── Tools/
│   ├── suite/
│   ├── main.c
│   ├── sensor.c
│   ├── sensor_task.c
│   ├── sensor_queue.c
│   ├── timeline.c
│   ├── timeline_internal.c
│   ├── trace.c
│   ├── trace_dumper.c
│   ├── workloads.c
│   ├── startup.c
│   ├── uart.c
│   ├── tasks_generated.c
│   ├── FreeRTOSConfig.h
│   └── Makefile
│
├── FreeRTOS/
│
├── test_reports/
│
├── project_1.pdf
├── project_1_presentation.pdf
├── TEST_README.md
├── REALREADME.md
├── Dockerfile
└── README.md

Important Source Files

File

Purpose

Demo/sensor.c

Simulated temperature sensor

Demo/Headers/sensor.h

Sensor interface and data structure

Demo/sensor_queue.c

Static FreeRTOS queue implementation

Demo/Headers/sensor_queue.h

Queue interface

Demo/sensor_task.c

Sensor and processing tasks

Demo/main.c

System initialization and scheduler startup

Demo/tasks_generated.c

Generated task configuration

Demo/timeline.c

Scheduler configuration/integration

Demo/timeline_internal.c

Major-frame timeline logic

Demo/startup.c

Startup code and HardFault diagnostics

Demo/Makefile

Build and QEMU execution

Demo/Tools/workload_gen.py

Workload/task configuration generation

Build and Run

Environment

The project can be built in WSL2 / Ubuntu using the ARM GCC toolchain and QEMU.

Required tools:

git
python3
make
gcc-arm-none-eabi
qemu-system-arm

Build

From the project:

cd Demo
make all

Run in QEMU

make qemu_start

The UART output can then be observed to verify:

major frames

HRT/SRT execution

sensor values

queue transfer

processing results

deadline monitoring

scheduler overhead

Generate workload configuration

The task configuration is generated from the test suite:

python3 Tools/workload_gen.py --input suite/00_test.json

Testing and Deliverables

This repository contains the complete project deliverables used during development and validation.

Source Code

The Demo/ directory contains:

scheduler integration

sensor simulation

queue communication

processing tasks

generated workloads

UART telemetry

tracing

startup/fault diagnostics

build configuration

Test Reports

test_reports/ contains generated testing/reporting material associated with scheduler validation.

Project Report

project_1.pdf

Contains the project documentation/report submitted for the project work.

Project Presentation

project_1_presentation.pdf

Contains the presentation used to explain the system, architecture, implementation, results, and observations.

Testing Documentation

TEST_README.md

Contains testing-related instructions and validation information.

Technologies Used

Category

Technology

Language

C

RTOS

FreeRTOS

Target

ARM Cortex-M3

Emulator

QEMU

Toolchain

GNU ARM Embedded GCC

Build

GNU Make

Scripting

Python

Development Environment

WSL2 / Ubuntu

Output / Debugging

UART, tracing, ARM register diagnostics

Engineering Concepts Demonstrated

This project provides hands-on implementation of:

Embedded C

modular driver-style code

typed sensor data structures

static memory

embedded-friendly integer representations

RTOS

task creation

task scheduling

HRT/SRT execution

task suspension/resumption

queues

task synchronization

Real-Time Systems

major-frame scheduling

deterministic task windows

deadline monitoring

scheduler overhead measurement

runtime telemetry

Embedded Debugging

HardFault analysis

program-counter mapping

stack-pointer inspection

context-switch debugging

QEMU-based reproduction

System Integration

generated task configuration

scheduler + application integration

automated build/run workflow

repeatable emulated validation

Example Runtime Output

MAJOR FRAME

SENSOR: t=304 temp=30.0C status=OK
PROCESSOR: t=304 temp=30.0C

SRT ST1
SRT ST2
SRT ST3

HRT HT1
HRT HT2
HRT HT3

SENSOR STATS: samples=310 processed=310 drops=0 high_temp=0

The scheduler also reports deadline-monitoring events when execution crosses a configured deadline.

Results Summary

Metric

Observed Result

Sensor samples

310

Processed samples

310

Queue drops

0

High-temperature events

0

Scheduler overhead

~1.18%

Major-frame execution

Stable across long QEMU runs

HardFault in stable validation run

Not observed

Deadline monitoring

Enabled; occasional HT3 misses observed

These numbers describe the observed demonstration run and are not presented as a universal benchmark for all hardware/configurations.

Future Extensions

Possible next steps include:

replacing the simulated sensor with real hardware

adding ADC/I2C/SPI sensor acquisition

adding interrupt-driven data capture

extending sensor processing with filtering

adding more realistic workload models

adding automated deadline statistics

adding CI-based build and regression tests

validating on a physical Cortex-M development board

Author

Ankit Kumar

B.Tech, Electronics & Communication Engineering
IIT Patna

Acknowledgement

This project builds upon the open-source EOSproject time-triggered FreeRTOS scheduling framework by Marco Pedron.

Upstream repository:

https://github.com/marco-pedron/EOSproject

The upstream framework is retained as the scheduler foundation, while this repository adds the project-specific embedded sensor pipeline, queue communication, telemetry, integration, validation, and debugging work.

License

Please refer to the original project's license and the license files included in this repository. Upstream attribution should be preserved when redistributing or modifying the scheduler framework.
