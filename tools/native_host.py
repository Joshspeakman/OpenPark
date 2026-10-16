"""ctypes interface to compiled ARM64 blocks. No disassembler or x86 oracle."""
import ctypes
from dos import REGS, SEGMENTS, MEMORY, Stop


class Native:
    names = REGS + ('eflags', 'eip') + SEGMENTS

    def __init__(self, library):
        self.lib = ctypes.CDLL(str(library.resolve()))
        self.lib.tp_memory.restype = ctypes.c_void_p
        self.lib.tp_error.restype = ctypes.c_char_p
        self.memory = self.lib.tp_memory()
        self.lib.tp_reset()

    def get(self, name):
        return self.lib.tp_get(self.names.index(name)) & 0xffffffff

    def set(self, name, value):
        self.lib.tp_set(self.names.index(name), ctypes.c_uint32(value))

    def read(self, address, size):
        if address < 0 or address + size > MEMORY:
            raise Stop('native host read outside memory')
        return ctypes.string_at(self.memory + address, size)

    def write(self, address, data):
        if address < 0 or address + len(data) > MEMORY:
            raise Stop('native host write outside memory')
        ctypes.memmove(self.memory + address, data, len(data))

    def segments(self, values):
        for name, value in values.items():
            self.set(name, value)
