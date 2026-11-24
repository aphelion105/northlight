"""Run a Windows fixture on a desktop that is never switched into view."""

import _winapi
import ctypes
from ctypes import wintypes as w
import msvcrt
import os
import subprocess


class StartupInfo(ctypes.Structure):
    _fields_ = [('cb', w.DWORD), ('lpReserved', w.LPWSTR), ('lpDesktop', w.LPWSTR),
                ('lpTitle', w.LPWSTR), ('dwX', w.DWORD), ('dwY', w.DWORD),
                ('dwXSize', w.DWORD), ('dwYSize', w.DWORD), ('dwXCountChars', w.DWORD),
                ('dwYCountChars', w.DWORD), ('dwFillAttribute', w.DWORD), ('dwFlags', w.DWORD),
                ('wShowWindow', w.WORD), ('cbReserved2', w.WORD), ('lpReserved2', w.LPVOID),
                ('hStdInput', w.HANDLE), ('hStdOutput', w.HANDLE), ('hStdError', w.HANDLE)]


class ProcessInfo(ctypes.Structure):
    _fields_ = [('process', w.HANDLE), ('thread', w.HANDLE), ('pid', w.DWORD), ('tid', w.DWORD)]


class Process:
    def __init__(self, argv, cwd, env, log, desktop):
        command = ctypes.create_unicode_buffer(subprocess.list2cmdline(argv))
        block = ctypes.create_unicode_buffer('\0'.join(f'{key}={value}' for key, value in sorted(env.items())) + '\0\0')
        startup = StartupInfo(cb=ctypes.sizeof(StartupInfo), lpDesktop=desktop, dwFlags=0x100)
        info = ProcessInfo()
        create = ctypes.WinDLL('kernel32', use_last_error=True).CreateProcessW
        create.argtypes = (w.LPCWSTR, w.LPWSTR, w.LPVOID, w.LPVOID, w.BOOL, w.DWORD,
                           w.LPVOID, w.LPCWSTR, ctypes.POINTER(StartupInfo), ctypes.POINTER(ProcessInfo))
        create.restype = w.BOOL
        with log.open('xb') as output, open(os.devnull, 'rb') as stdin:
            startup.hStdInput = msvcrt.get_osfhandle(stdin.fileno())
            startup.hStdOutput = startup.hStdError = msvcrt.get_osfhandle(output.fileno())
            os.set_handle_inheritable(startup.hStdInput, True)
            os.set_handle_inheritable(startup.hStdOutput, True)
            if not create(argv[0], command, None, None, True, 0x400, block, str(cwd),
                          ctypes.byref(startup), ctypes.byref(info)):
                raise ctypes.WinError(ctypes.get_last_error())
        _winapi.CloseHandle(info.thread)
        self.handle, self.pid, self.args = info.process, info.pid, argv
        self.returncode = None

    def wait(self, timeout=None):
        wait_ms = _winapi.INFINITE if timeout is None else int(timeout * 1000)
        if _winapi.WaitForSingleObject(self.handle, wait_ms) == _winapi.WAIT_TIMEOUT:
            raise subprocess.TimeoutExpired(self.args, timeout)
        self.returncode = _winapi.GetExitCodeProcess(self.handle)
        return self.returncode

    def poll(self):
        try:
            return self.wait(0)
        except subprocess.TimeoutExpired:
            return None

    def kill(self):
        _winapi.TerminateProcess(self.handle, 1)

    def close(self):
        _winapi.CloseHandle(self.handle)


class Desktop:
    def __init__(self):
        self.user = user = ctypes.WinDLL('user32', use_last_error=True)
        user.CreateDesktopW.argtypes = (w.LPCWSTR, w.LPCWSTR, w.LPVOID, w.DWORD, w.DWORD, w.LPVOID)
        user.CreateDesktopW.restype = w.HANDLE
        user.SetThreadDesktop.argtypes = (w.HANDLE,)
        user.GetThreadDesktop.argtypes = (w.DWORD,)
        user.GetThreadDesktop.restype = w.HANDLE
        user.CloseDesktop.argtypes = (w.HANDLE,)
        user.OpenInputDesktop.argtypes = (w.DWORD, w.BOOL, w.DWORD)
        user.OpenInputDesktop.restype = w.HANDLE
        user.GetUserObjectInformationW.argtypes = (w.HANDLE, ctypes.c_int, w.LPVOID, w.DWORD, ctypes.POINTER(w.DWORD))
        self.name = f'NorthlightM2{os.getpid()}'
        self.samples = 0
        self.original = user.GetThreadDesktop(ctypes.windll.kernel32.GetCurrentThreadId())
        self.handle = user.CreateDesktopW(self.name, None, None, 0, 0x00cf, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            if not user.SetThreadDesktop(self.handle):
                raise ctypes.WinError(ctypes.get_last_error())
            self.confirm_inactive()
        except BaseException:
            self.close()
            raise

    def confirm_inactive(self):
        active = self.user.OpenInputDesktop(0, False, 1)
        if not active:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            name = ctypes.create_unicode_buffer(256)
            if not self.user.GetUserObjectInformationW(active, 2, name, ctypes.sizeof(name), None):
                raise ctypes.WinError(ctypes.get_last_error())
            if name.value == self.name:
                raise ValueError('The M2 benchmark desktop became the input desktop')
        finally:
            self.user.CloseDesktop(active)
        self.samples += 1

    def launch(self, argv, cwd, env, log):
        self.confirm_inactive()
        return Process(argv, cwd, env, log, 'WinSta0\\' + self.name)

    def close(self):
        self.user.SetThreadDesktop(self.original)
        self.user.CloseDesktop(self.handle)
