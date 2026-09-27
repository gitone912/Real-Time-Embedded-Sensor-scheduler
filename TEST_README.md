# README ----- test suite ----
This is a test suite in python.

## Run the test suite
To run the whole  test suite, use the following command in the terminal:
```bash
python3 integrated_test.py
```
It is also possibile to run a single configurations with
```bash
python3 integrated_test.py --config <config_name>
```
where `<config_name>` is the name of the configuration you want to run located in suite/ directory

If you want to list all the possible configurations, you can run
```bash
python3 integrated_test.py --list
```
for help on the command line arguments, you can run
```bash 
python3 integrated_test.py -h 
```

### Only Build
```bash
python3 integrated_test.py --action build
```

### Only Test
```bash
python3 integrated_test.py --action test
```
### Only Analyze
```bash
python3 integrated_test.py --action analyze
``` 
### Full Test Cycle
```bash
python3 integrated_test.py --action full
``` 
by default, if no action is specified, it will run the full test cycle (build → test → analyze).



## Runs' chain


## Build & Test Pipeline
P.S. from markdown preview, the mermaid diagram is not rendered, but it should be rendered correctly on GitHub.
you hae to install the mermaid extension for vscode to see it in the markdown preview, or you can just open this file on GitHub to see the rendered diagram.
```mermaid
flowchart TD
    A[integrated_test.py\nPython Test Runner]
    B[make / docker commands]
    C[Task Generation\nconfig/tasks.json\n→ gen_tasks.py]
    D[docker_build\nCross-compilation\n→ demo.elf]
    E[docker_qemu\nQEMU + FreeRTOS]
    F[Trace Dumper Task\nStops on SIGINT 2]
    G[UART Log\nlogs/uart.log]
    H[Python Analysis\nMetrics & Validation]

    A --> B
    B --> C
    C --> D
    D --> E
    E --> F
    F --> G
    G --> H