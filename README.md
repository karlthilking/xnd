XND: Transparent Checkpoint-Restart for macOS 
=============================================

XND is a transparent, user-level checkpoint-restart package for macOS.
Transparency means that an application does not need to be re-compiled or
re-linked to work with XND, and does not need to be aware of XND in order to 
be checkpointed. For example, a binary-only executable, such as an 
interpreter, can be checkpointed without any modification. 

The core functionality of XND is implemented by a dynamic library,
`libxnd.dylib`, that is preloaded when a program is executed and provides
checkpoint control. A checkpoint constitutes a binary 'checkpoint image'
encapsulating the state of a running process that is written to disk when a
checkpoint is requested. Another executable, `xnd_restart`, interprets a set
of checkpoint images in order to restore state of the program that was
checkpointed. Thereafter, it will appear that the program had been resumed
from the point where it was checkpointed.

XND Usage
=========

## Run
```sh
./xnd_launch a.out -a -b -c
```

In this example, `xnd_launch` will setup for `libxnd.dylib` to be preloaded
into `a.out` when it is executed, using the environment variable
`DYLD_INSERT_LIBRARIES` (analogous to `LD_PRELOAD` on Linux). Then, `a.out`
is executed with its specified arguments verbatim (`./a.out -a -b -c`).


## Checkpoint
```sh
./xnd_command --checkpoint
```

`xnd_command` will connect to XND's coordinator process and send a
checkpoint request. The coordinator broadcasts the checkpoint request to
every process participating in the checkpoint, and each process writes a
checkpoint image file.

## Restart
```sh
./xnd_restart /path/to/ckpt-directory
```

In this example, `path/to/ckpt-directory` is a directory containing a
checkpoint manifest file and one checkpoint image for each process that was
part of the checkpointed program. `xnd_restart` rebuilds the a process
hierarchy, and each process executes `xnd_restart_internal` to restore
process-local state.

XND Feature Status and Internals
================================

## Overview
| Category/Feature | Support/Status |
|------------------|----------------|
| PAC | Yes with one caveat, see PAC section below |
| Multi-threading using POSIX threads | Yes |
| Multi-threading using libdispatch/workqueues | WIP |
| Signal state | Yes |
| Regular files | Mostly, see Multi-process support section |
| Multi-process support | Partial, see Multi-process support |
| Process groups and sessions | Yes |
| Process hierachy, parent-child relationships | Yes |
| PIDs, PGIDs, SIDs | Yes, via PID virtualization |
| Sockets | No |
| Pipes | No |
| Shared memory | No |

The following excerpts provide additional details about XND's support for
various features and internals.

## Basic Information

XND targets macOS built for arm64. It has been tested for macOS versions
14 and 26 on an Apple M3 chip. XND runs entirely in userspace and does not
require any system privileges.

## ARM Pointer Authentication (PAC)

Apple's Darwin kernel built for ARM-based CPUs supports two ABIs, arm64 and
arm64e. arm64e improves control flow integrity by taking advantage of the
AArch64 architecture's PAC features.

XND supports checkpointing all arm64 executables and most arm64e
executables. arm64e executables using the `LC_DYLD_INFO` or
`LC_DYLD_INFO_ONLY` load commands are not compatible with XND. However,
recent versions of Apple's linker will use `LC_DYLD_CHAINED_FIXUPS` and
`LC_DYLD_EXPORTS_TRIE`, which replaces the purpose of the former load
command. Thus, only executables compiled for arm64e and linked with an
older version of Apple's linker are incompatible with XND.

`otool` can be used to check that an executable satisfies these
prerequisites:
```sh
otool -hv # Mach header
otool -lv # load commands
```

`otool -hv` will show the executable's Mach header, including its
cpusubtype field. If the cpusubtype is `ALL`, then it was compiled for
arm64, otherwise if the cpusubtype is `E`, it was compiled for arm64e.

If the executable was compiled for arm64e, `otool -lv` can be used to
inspect the executable's load commands. If the executable uses the load
commands `LC_DYLD_CHAINED_FIXUPS` and `LC_DYLD_EXPORTRS_TRIE`, then it is 
compatible with XND. Otherwise, an arm64e executable with `LC_DYLD_INFO`
or `LC_DYLD_INFO_ONLY` can not be checkpointed by XND. 

This is due to a specific technique used by `xnd_launch` for handling arm64e 
executables, which is not compatible with executables using the older load 
command.

## Multi-threaded Applications

### POSIX threads

XND can transparently checkpoint and restart multi-threaded applications
using POSIX threads. Each thread's stack, including its thread descriptor
and thread-local data, are included in the checkpoint image. Other
thread-specific information such a thread's signal state, TLS address,
register context, and cancellability state, are saved by each thread when
a checkpoint is taken.

### XNU workqueues

The XNU kernel offers an alternative API for multi-threading, supported by
the workqueue subsystem. The majority of this API is private and is only
intended for use by libdispatch, which provides higher-level APIs for
concurrency. Thus, programs using workqueue threads are mostly limited to
either programs using libdispatch APIs directly, or programs written in a language whose runtime uses libdispatch, such as Swift.

Support for programs using workqueue threads in XND is currently a work in
progress. Implementing support for workqueue threads also requires
supporting at least a subset of the `kqueue(2)` API, a kernel event
notification facility first introduced by FreeBSD. The workqueue subsystem
is closely tied to XNU's implementation of kernel event notification, so
both must be considered for supporting workqueue threads.

## Multi-process Support

Multi-process support in XND is partial. XND's coordinator implements the
facilities to coordinate checkpoint/restart between multiple processes.
`xnd_restart` is equipped to recreate process hierarchies and restore
process-group or session leadership. XND also implements process ID
virtualization and was carefully designed with fork safety in mind.
However, the aforementioned support only supplies the groundwork for
multi-process support; other aspects remain incomplete.

### Regular files

XND saves the state of open files at the time of checkpoint. After restart,
files are re-opened, and are set up to have the same desciptor number, file
offset, and access mode. However, XND does not properly handle file
descriptors inherited by a child process. At restart, the parent and child
will re-open the file independently, so the descriptor will no longer be
shared.

### Sockets and Pipes

Sockets and pipes are not yet supported by XND.

### Shared memory

MacOS supports POSIX shared memory, SysV shared memory, and using `mmap`
with `MAP_SHARED`. Mach VM APIs can also be used to create shared memory
regions. At the time of writing, these facilities are not yet supported in
XND.

## Unix Signals

XND restores all relevant state related to signals. This includes signal
dispostions, signal masks, alternate signal stacks, and pending signals.
At restart, each thread restores its signal mask and re-registers an 
alternate signal stack if it previously registered one. A global set of
pending signals is used to distinguish between a pending signal that was
thread-directed or a process-wide pending signal. Pending signals are 
re-raised either to a specific thread or the entire process, depending on
the verdict.
