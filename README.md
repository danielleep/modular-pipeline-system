
# Modular Pipeline System

A modular, multithreaded string analyzer pipeline in C, featuring dynamic plugin loading, thread-safe bounded queues, and graceful shutdown.
Developed as part of an Operating Systems course and maintained as a personal portfolio project.

---


## Features

- Modular plugin-based architecture with dynamic loading of `.so` files.
- Multithreading: each plugin runs in its own thread using POSIX threads.
- Thread-safe bounded producer-consumer queues for inter-thread communication.
- Graceful shutdown on `<END>` input: queues are drained, and all threads terminate cleanly.
- Built-in plugins:
  - **logger** – prints the string to STDOUT.
  - **typewriter** – prints each character with a 100ms delay.
  - **uppercaser** – converts all letters to uppercase.
  - **rotator** – rotates characters to the right by one position.
  - **flipper** – reverses the string.
  - **expander** – inserts spaces between characters.

---

## Project Structure

```
.
├── main.c                 # main application
├── build.sh               # build script
├── test.sh                # test orchestrator
├── README.md              # project documentation
├── plugins/               # plugin implementations
│   ├── *.c / *.h          # plugin source files
│   └── sync/              # synchronization primitives (monitor, queues)
├── script_tests/          # integration/system test scripts (bash)
├── tests/                 # C unit tests
│   ├── consumer_producer/
│   ├── monitor/
│   ├── plugin_common/
│   └── plugin_tests/
└── output/                # build outputs (ignored by git)
```

---

## Build Instructions

From the project root, run:

```bash
./build.sh
```

---

## Run Example

From the project root, run:

```bash
echo "hello" | ./output/analyzer 20 uppercaser rotator logger flipper typewriter
echo "<END>" | ./output/analyzer 20 uppercaser rotator logger flipper typewriter
```

Expected output:

```bash
[logger] OHELL
[typewriter] LLEHO
```

---

## Testing

Run the full automated test suite:

```bash
./test.sh
```


Test categories:
- happy path – normal pipeline flows
- edge cases – empty strings, long strings, multiple chained plugins
- negative tests – invalid arguments, missing plugins, incorrect usage
- stress & robustness – concurrency and performance testing
- output hygiene – clean STDOUT/STDERR handling
- insiders tests – internal modules (monitor, queue, plugin_common)

---


## Technologies

- C (GCC 13, Ubuntu 24.04)
- POSIX Threads (pthread)
- Dynamic linking (dlopen, dlsym)
- Custom synchronization mechanisms (monitor, bounded queue)

---

## Future Improvements

- Add additional plugins (e.g., encryption, compression).
- Support configuration files for pipeline definition instead of CLI only.
- Add performance metrics and monitoring.
- Integrate memory checks with Valgrind and sanitizers.
