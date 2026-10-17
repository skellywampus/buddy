"""App-only diagnostic connections. Never deliberately change DTR or RTS."""
import os
import time


def open_diagnostic(port):
    if os.name == "nt":
        return WindowsDiagnostic(port)
    import serial
    import termios

    class NoResetSerial(serial.Serial):
        def _update_dtr_state(self):
            pass

        def _update_rts_state(self):
            pass

        def _reconfigure_port(self, force_update=False):
            attrs = termios.tcgetattr(self.fd)
            attrs[2] &= ~termios.HUPCL
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            super()._reconfigure_port(force_update=force_update)

    connection = NoResetSerial()
    connection.port = port
    connection.baudrate = 115200
    connection.timeout = .5
    connection.write_timeout = 2
    connection.open()
    return connection


def configure_windows_dcb(dcb):
    # Keep the current DTR/RTS control settings. pySerial's ordinary Windows
    # reconfiguration writes those fields, unlike its POSIX implementation.
    dcb.BaudRate = 115200
    dcb.ByteSize = 8
    dcb.Parity = 0
    dcb.StopBits = 0
    dcb.fBinary = 1
    dcb.fParity = 0
    dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = 0
    dcb.fOutX = dcb.fInX = 0
    dcb.fNull = dcb.fErrorChar = dcb.fAbortOnError = 0


class WindowsDiagnostic:
    """Synchronous Win32 serial I/O preserving DTR/RTS from GetCommState."""
    def __init__(self, port):
        import ctypes
        from serial import win32
        self.ctypes, self.api = ctypes, win32
        self.handle = None
        handle = win32.CreateFile("\\\\.\\" + port, win32.GENERIC_READ | win32.GENERIC_WRITE,
                                  0, None, win32.OPEN_EXISTING, 0, None)
        if handle == win32.INVALID_HANDLE_VALUE:
            raise OSError("Cannot open USB port. Close serial monitors and reconnect Buddy.")
        self.handle = handle
        try:
            dcb = win32.DCB()
            dcb.DCBlength = ctypes.sizeof(dcb)
            if not win32.GetCommState(handle, ctypes.byref(dcb)):
                raise OSError("Cannot read USB serial settings")
            configure_windows_dcb(dcb)
            if not win32.SetCommState(handle, ctypes.byref(dcb)):
                raise OSError("Cannot configure USB serial settings")
            timeouts = win32.COMMTIMEOUTS()
            timeouts.ReadIntervalTimeout = win32.MAXDWORD
            timeouts.ReadTotalTimeoutConstant = 200
            timeouts.WriteTotalTimeoutConstant = 2000
            if not win32.SetCommTimeouts(handle, ctypes.byref(timeouts)):
                raise OSError("Cannot configure USB timeouts")
        except BaseException:
            self.close()
            raise

    def write(self, data):
        count = self.api.DWORD()
        buffer = self.ctypes.create_string_buffer(data)
        if not self.api.WriteFile(self.handle, buffer, len(data), self.ctypes.byref(count), None):
            raise OSError("USB diagnostic write failed")
        if count.value != len(data):
            raise OSError("USB diagnostic write was incomplete")

    def readline(self):
        result = bytearray()
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline and len(result) < 2048:
            buffer = self.ctypes.create_string_buffer(1)
            count = self.api.DWORD()
            if not self.api.ReadFile(self.handle, buffer, 1, self.ctypes.byref(count), None):
                raise OSError("USB diagnostic read failed")
            if not count.value:
                break
            result.extend(buffer.raw[:1])
            if result.endswith(b"\n"):
                break
        return bytes(result)

    def close(self):
        if self.handle is not None:
            self.api.CloseHandle(self.handle)
            self.handle = None
