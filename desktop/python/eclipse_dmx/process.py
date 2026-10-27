"""Starting eclipse-dmx so that it cannot outlive whoever started it.

The show quits when its stdin closes, and every normal way out closes it. The
abnormal ways do not: a python process that aborts - a Tcl panic, a hard kill
- can leave its pipe handles to be reclaimed later than its child notices, and
on Windows a child of a process that is gone keeps rendering into nobody's
pipe forever. One of those is one too many on a desk; a test run that crashes
used to leave a handful, each holding the executable open so the next build
could not link it.

`spawn` is `subprocess.Popen` with the operating system told the truth: this
child belongs to this process and dies with it, however it dies.

- Windows: the child goes in a job object with KILL_ON_JOB_CLOSE. The job
  handle is held by the returned process object, so it lives exactly as long
  as the parent's view of the child does, and the kernel closes it - and kills
  the job - when the parent ends by any means at all.
- Linux: PR_SET_PDEATHSIG, so the kernel sends the child SIGTERM when the
  parent thread that spawned it exits.
- Anywhere else: plain Popen, and the stdin-closes rule is all there is.

Best effort throughout: a parent that is itself in a job that forbids
breakaway cannot assign children to another one, and a show started from such
a place still runs - it just is not protected. Nothing here raises for that.
"""

from __future__ import annotations

import os
import subprocess
import sys
from typing import Any


def spawn(args, **popen_kwargs: Any) -> subprocess.Popen:
    """`subprocess.Popen(args, **popen_kwargs)`, tied to this process's life."""
    if os.name == "nt":
        return _spawn_windows(args, popen_kwargs)
    if sys.platform.startswith("linux"):
        return _spawn_linux(args, popen_kwargs)
    return subprocess.Popen(args, **popen_kwargs)


# ---------------------------------------------------------------------------
# windows: a job object per child
# ---------------------------------------------------------------------------

def _spawn_windows(args, popen_kwargs) -> subprocess.Popen:
    process = subprocess.Popen(args, **popen_kwargs)
    try:
        process._eclipse_job = _JobObject.for_process(process)  # type: ignore[attr-defined]
    except Exception:
        process._eclipse_job = None  # type: ignore[attr-defined]
    return process


class _JobObject:
    """A kill-on-close job holding one process. Closing the handle kills it."""

    _JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000
    _JobObjectExtendedLimitInformation = 9

    def __init__(self, handle: int):
        self.handle = handle

    @classmethod
    def for_process(cls, process: subprocess.Popen) -> "_JobObject":
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

        class IO_COUNTERS(ctypes.Structure):
            _fields_ = [(name, ctypes.c_ulonglong) for name in (
                "ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
                "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]

        class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
            _fields_ = [
                ("PerProcessUserTimeLimit", ctypes.c_longlong),
                ("PerJobUserTimeLimit", ctypes.c_longlong),
                ("LimitFlags", wintypes.DWORD),
                ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t),
                ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t),
                ("PriorityClass", wintypes.DWORD),
                ("SchedulingClass", wintypes.DWORD),
            ]

        class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
            _fields_ = [
                ("BasicLimitInformation", JOBOBJECT_BASIC_LIMIT_INFORMATION),
                ("IoInfo", IO_COUNTERS),
                ("ProcessMemoryLimit", ctypes.c_size_t),
                ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t),
                ("PeakJobMemoryUsed", ctypes.c_size_t),
            ]

        kernel32.CreateJobObjectW.restype = wintypes.HANDLE
        kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        kernel32.SetInformationJobObject.restype = wintypes.BOOL
        kernel32.SetInformationJobObject.argtypes = [
            wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        kernel32.AssignProcessToJobObject.restype = wintypes.BOOL
        kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        kernel32.CloseHandle.restype = wintypes.BOOL
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

        job = kernel32.CreateJobObjectW(None, None)
        if not job:
            raise ctypes.WinError(ctypes.get_last_error())

        info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
        info.BasicLimitInformation.LimitFlags = cls._JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        ok = kernel32.SetInformationJobObject(
            job, cls._JobObjectExtendedLimitInformation, ctypes.byref(info), ctypes.sizeof(info))
        if ok and process._handle is not None:  # type: ignore[attr-defined]
            ok = kernel32.AssignProcessToJobObject(job, int(process._handle))  # type: ignore[attr-defined]
        if not ok:
            error = ctypes.get_last_error()
            kernel32.CloseHandle(job)
            raise ctypes.WinError(error)

        instance = cls(job)
        instance._close = kernel32.CloseHandle  # bound late so __del__ needs no import
        return instance

    def __del__(self) -> None:
        close = getattr(self, "_close", None)
        if close is not None and self.handle:
            try:
                close(self.handle)
            except Exception:
                pass
            self.handle = 0


# ---------------------------------------------------------------------------
# linux: die with the parent
# ---------------------------------------------------------------------------

def _spawn_linux(args, popen_kwargs) -> subprocess.Popen:
    import ctypes
    import signal
    import threading

    # The death signal watches the *thread* that forked, not the process: a
    # show started from a worker thread would be killed when that thread
    # ended. Only the main thread's children get it; the rest rely on the
    # stdin-closes rule, as before.
    if threading.current_thread() is not threading.main_thread():
        return subprocess.Popen(args, **popen_kwargs)

    PR_SET_PDEATHSIG = 1
    try:
        libc = ctypes.CDLL("libc.so.6", use_errno=True)
    except OSError:
        return subprocess.Popen(args, **popen_kwargs)

    outer = popen_kwargs.pop("preexec_fn", None)

    def preexec() -> None:
        libc.prctl(PR_SET_PDEATHSIG, signal.SIGTERM, 0, 0, 0)
        if outer is not None:
            outer()

    return subprocess.Popen(args, preexec_fn=preexec, **popen_kwargs)
