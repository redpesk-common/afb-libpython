# afb-libpython functions available from Python-land

## `loopstart` (main loop)

Returns: `int`, return value from the callback

| Argument   | Type        | Details                                                                                       |
|------------|-------------|-----------------------------------------------------------------------------------------------|
| `binder`   | `PyCapsule` | Hides a `GlueHandleT` of magic `GLUE_BINDER_MAGIC_TAG`                                        |
| `callback` | `Callable`  | Optional; see below                                                                           |
| `userdata` | `Any`       | Optional, `callback` must be present if used; any data the user wants to pass to the callback |

### `loopstart`'s callback

Returns: `int`, 0 for the main loop to continue indefinitely, <0 for KO, >0 for return value

| Argument   | Type        | Details                                                |
|------------|-------------|--------------------------------------------------------|
| `binder`   | `PyCapsule` | Hides a `GlueHandleT` of magic `GLUE_BINDER_MAGIC_TAG` |
| `userdata` | `Any`       | The same `userdata` which was given to `loopstart`     |

## Jobs

There are 3 ways of running a job. This table denotes the features of
each alternative, see below for typing and arguments documentation.

| Function   | Type         | Feature | Termination           | Behavior on callback return | Behavior on callback timeout                         |
|------------|--------------|---------|-----------------------|-----------------------------|------------------------------------------------------|
| `jobcall`  | Synchronous  | Timeout | Callback ends/returns | Terminates, value unused    | Initial call terminates, new call with `signum` != 0 |
| `jobenter` | Synchronous  | Timeout | `jobleave`            | Nothing happens             | Same as `jobcall` + throws exception                 |
| `jobpost`  | Asynchronous | Delay   | `jobabort`            |                             | Does not timeout                                     |

### `jobcall`

Returns: `int`, always 0

| Argument   | Type        | Details                                                   |
|------------|-------------|-----------------------------------------------------------|
| `binder`   | `PyCapsule` | Hides a `GlueHandleT` of magic `GLUE_BINDER_MAGIC_TAG`    |
| `callback` | `Callable`  | See below                                                 |
| `timeout`  | `int`       | Time in seconds after which execution flow is stopped     |
| `userdata` | `Any`       | Optional; any data the user wants to pass to the callback |

The timeout should be >=0, 0 meaning the callback never times out.

#### `jobcall`'s callback

Returns: nothing

| Argument   | Type        | Details                                                             |
|------------|-------------|---------------------------------------------------------------------|
| `job`      | `PyCapsule` | Hides a `GlueHandleT` of magic `GLUE_JOB_MAGIC_TAG`                 |
| `signum`   | `int`       | 0 for normal execution, signal number when execution is interrupted |
| `userdata` | `Any`       | The same `userdata` which was given to `jobcall`                    |

### `jobenter`

`jobenter` throws an exception in case of a timeout, so it should be
called in a `try` block.

Returns: `int`, status passed to `jobleave`

| Argument   | Type        | Details                                                   |
|------------|-------------|-----------------------------------------------------------|
| `binder`   | `PyCapsule` | Hides a `GlueHandleT` of magic `GLUE_BINDER_MAGIC_TAG`    |
| `callback` | `Callable`  | See below                                                 |
| `timeout`  | `int`       | Time in seconds after which execution flow is stopped     |
| `userdata` | `Any`       | Optional; any data the user wants to pass to the callback |

The timeout should be >=0, 0 meaning the callback never times out.

#### `jobenter`'s callback

Returns: nothing

| Argument   | Type        | Details                                                             |
|------------|-------------|---------------------------------------------------------------------|
| `job`      | `PyCapsule` | Hides a `GlueHandleT` of magic `GLUE_JOB_MAGIC_TAG`                 |
| `signum`   | `int`       | 0 for normal execution, signal number when execution is interrupted |
| `userdata` | `Any`       | The same `userdata` which was given to `jobenter`                   |

#### `jobleave`

Returns: nothing

| Argument | Type        | Details                                             |
|----------|-------------|-----------------------------------------------------|
| `job`    | `PyCapsule` | Hides a `GlueHandleT` of magic `GLUE_JOB_MAGIC_TAG` |
| `status` | `int`       | Return code                                         |

### `jobpost`

Broken since Python sub-interpreters removal?

Returns: `int`, job ID

| Argument   | Type        | Details                                                   |
|------------|-------------|-----------------------------------------------------------|
| `binder`   | `PyCapsule` | Hides a `GlueHandleT` of magic `GLUE_BINDER_MAGIC_TAG`    |
| `callback` | `Callable`  | See below                                                 |
| `delay`    | `int`       | Time in seconds after which execution starts              |
| `userdata` | `Any`       | Optional; any data the user wants to pass to the callback |

#### `jobpost`'s callback

Returns: nothing

| Argument   | Type        | Details                                                             |
|------------|-------------|---------------------------------------------------------------------|
| `job`      | `PyCapsule` | Hides a `GlueHandleT` of magic `GLUE_JOB_MAGIC_TAG`                 |
| `signum`   | `int`       | 0 for normal execution, signal number when execution is interrupted |
| `userdata` | `Any`       | The same `userdata` which was given to `jobpost`                    |

#### `jobabort`

Returns: nothing

| Argument | Type  | Details            |
|----------|-------|--------------------|
| `jobid`  | `int` | Given by `jobpost` |
