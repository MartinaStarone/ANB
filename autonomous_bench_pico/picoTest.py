import gdb
from time import *



OPENOCD_CMD = "openocd"
GDB_CMD = "/usr/local/bin/riscv32-unknown-elf-gdb"
GDB_PORT = 3333


class testAll(gdb.Command):
    def __init__(self):
        super(testAll, self).__init__("avvia_picoTest", gdb.COMMAND_USER)
    def invoke(self,arg,from_tty):
        gdb.execute("set pagination off")
        gdb.execute("set confirm off")
        gdb.execute("target extended-remote localhost:3333")
        gdb.execute("monitor reset halt")
        gdb.execute("load")

        funzioni_aes = ['Cipher', 'AddRoundKey', 'SubBytes', 'MixColumns', 'ShiftRows', 'KeyExpansion']
        indirizzi = []

        for func in funzioni_aes:
            try:
                gdb.execute("monitor reset halt")
                gdb.execute(f"tbreak {func}")
                gdb.execute("continue")

                frame = gdb.newest_frame()
                blocco = frame.block()
                arch= frame.architecture()
                istruzioni = arch.disassemble(blocco.start, blocco.end)
                for istr in istruzioni:
                    if istr['addr'] not in indirizzi:
                        indirizzi.append(istr['addr'])
            except Exception as e:
                print(f"Errore caricando {func}: {e}")

        for addr in indirizzi:
            gdb.execute("monitor reset halt")
            gdb.execute(f"tbreak *{hex(addr)}")
            gdb.execute("continue")
            pc = int(gdb.parse_and_eval("*(unsigned short *)$pc"))
            if (pc & 0x3) == 0x3:
                instruction_length = 4 # Istruzione 32-bit
            else:
                instruction_length = 2 # Istruzione compressa 16-bit
            gdb.execute(f"set $pc = $pc + {instruction_length}")

            gdb.execute("tbreak test_completed")

            try:
                gdb.execute("continue")
            except gdb.error:
                pass

            status = int(gdb.parse_and_eval("test_status"))

            if status==1:
                print(f"RESULT: Benign fault at {hex(addr)}")

            elif status == 2:
                print(f"RESULT SDC at {hex(addr)}")
            else:
                print(f"Fault at {hex(addr)}")

testAll()





