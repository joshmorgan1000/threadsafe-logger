```text
_\"||\/||'|_[-|_()[,[,[-|2
```

A simple thread-safe C++17 logging library with compile-time log levels, stream-style macros, structured JSON output,
progress displays, and terminal rendering utilities.

## Build

Threadsafe Logger requires a C++17 compiler, CMake 3.16 or newer, and a platform threading library.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To build and run the unit tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DLOGGING_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The animated logo demo is built with the tests and can be run from a terminal:

```sh
./build/tests/demo_logo
```

To consume the library from another CMake project, add this repository and link its namespaced target:

```cmake
add_subdirectory(path/to/threadsafe-logger)
target_link_libraries(your_target PRIVATE logging::logging)
```

## Usage

Include `logging.hpp` for the core logger and macros:

```cpp
#include <logging.hpp>

int main() {
    threadsafe_logger::component() = "server";
    threadsafe_logger::instance_name() = "primary";
    LOG_INFO("Listening on port ", 8080);
    LOG_WARN_STREAM << "Queue depth is " << 42;
    LOG_ERROR("Request failed");
    return 0;
}
```

The argument macros concatenate string literals, standard streamable values, and objects exposing a
`toString()` method. Stream variants support normal `operator<<` formatting.

| Level | Argument macro | Stream macro | `LOGGING_LOG_LEVEL` |
| --- | --- | --- | ---: |
| Trace | `LOG_TRACE(...)` | `LOG_TRACE_STREAM` | `0` |
| Debug | `LOG_DEBUG(...)` | `LOG_DEBUG_STREAM` | `1` |
| Info | `LOG_INFO(...)` | `LOG_INFO_STREAM` | `2` |
| Warning | `LOG_WARN(...)` | `LOG_WARN_STREAM` | `3` |
| Error | `LOG_ERROR(...)` | `LOG_ERROR_STREAM` | `4` |
| Raw | `LOG_RAW(...)` | `LOG_RAW_STREAM` | `5` |

`LOG_STREAM` logs at the level returned by `threadsafe_logger::Logger::level()`. Raw logging writes the
message without a timestamp or level prefix.

Set the minimum compiled level when configuring the library. Lower-level macros become null operations:

```sh
cmake -S . -B build -DLOGGING_LOG_LEVEL=2
```

Enable newline-delimited JSON and optionally identify the emitting component and instance:

```cpp
threadsafe_logger::component() = "scheduler";
threadsafe_logger::instance_name() = "worker-1";
threadsafe_logger::log_format_json().store(true);
LOG_INFO("Job accepted");
```

Terminal progress callbacks accept values from zero through one and become no-ops when stdout is not a
TTY:

```cpp
std::function<void(float)> update = threadsafe_logger::Logger::progress("Indexing");
update(0.5f);
update(1.0f);
```

`loggingutils.hpp` provides the terminal compositor, sprites, table rendering, colors, UTF-8 helpers,
and geometry types. `THROW(message)` and `EXCEPTION_CLASS(name)` provide the library's exception helpers.

## License

Threadsafe Logger is available under the [MIT License](LICENSE).
