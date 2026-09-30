import gdb

class ComandoTestAll(gdb.Command):
    def __init__(self):
        super().__init__("avvia_test_all", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        gdb.execute("set confirm off")
        gdb.execute("set pagination off")
        
        funzioni_aes = ['Cipher', 'AddRoundKey', 'SubBytes', 'MixColumns', 'ShiftRows', 'KeyExpansion']
        indirizzi = []
        
        for func in funzioni_aes:
            try:
                gdb.execute("start")
                gdb.execute(f"tbreak {func}")
                gdb.execute("continue")
                
                frame = gdb.newest_frame()
                if frame.name() != func:
                    # In case of inline or mismatch
                    continue
                    
                blocco = frame.block()
                arch = frame.architecture()
                istruzioni = arch.disassemble(blocco.start, blocco.end)
                for istr in istruzioni:
                    if istr['addr'] not in indirizzi:
                        indirizzi.append(istr['addr'])
            except Exception as e:
                print(f"Errore caricando {func}: {e}")
                
        print(f"Trovate {len(indirizzi)} istruzioni totali in tutto l'AES...")

        rilevati_anb = 0
        rilevati_os = 0
        silent_errors = 0
        benign_faults = 0

        for addr in indirizzi:
            bp = SkipbreakPoint(addr)
            try:
                gdb.execute(f"run > /tmp/anb_test_out.txt 2>&1", to_string=True)
                
                inf = gdb.selected_inferior()
                
                if inf.pid > 0:
                    # Program crashed (received a signal like SIGSEGV, SIGABRT, SIGILL)
                    try:
                        # Check if this was ANB's branchless memory trap (which loads from 0xffffffffffffffff / -1)
                        fault_addr = int(gdb.parse_and_eval("$_siginfo._sifields._sigfault.si_addr"))
                        # In 64-bit, -1 is 0xffffffffffffffff
                        is_anb_trap = (fault_addr == -1 or fault_addr == 0xffffffffffffffff)
                    except:
                        is_anb_trap = False

                    gdb.execute("kill", to_string=True)

                    if is_anb_trap:
                        print(f"Indirizzo {hex(addr)}: RILEVATO (ANB Memory Trap)")
                        rilevati_anb += 1
                    else:
                        print(f"Indirizzo {hex(addr)}: RILEVATO (Crash/Segnale OS)")
                        rilevati_os += 1
                else:
                    # Program exited normally
                    try:
                        with open("/tmp/anb_test_out.txt", "r", errors="ignore") as f:
                            output = f.read()
                    except:
                        output = ""

                    if "[FAIL]" in output:
                        print(f"Indirizzo {hex(addr)}: SILENT ERROR (Cifratura sbagliata!)")
                        silent_errors += 1
                        sal = gdb.find_pc_line(addr)
                        linea_c = f"{sal.symtab.filename}:{sal.line}" if sal.symtab else "Sconosciuta"
                        arch = gdb.selected_inferior().architecture()
                        istr_asm = arch.disassemble(addr,count=1)[0]['asm']

                        with open("analisi_silent.txt", "a") as log_file:
                            log_file.write(f"ADDR: {hex(addr)} | C_LINE: {linea_c} | ASM: {istr_asm}\n")
                    elif "[OK]" in output:
                        print(f"Indirizzo {hex(addr)}: BENIGN (Sopravvissuto e Cifratura Corretta)")
                        benign_faults += 1
                    else:
                        # Fallback if output doesn't match expected strings
                        print(f"Indirizzo {hex(addr)}: SILENT ERROR (Output inaspettato)")
                        silent_errors += 1
                    
            except gdb.error as e:
                # GDB error (e.g. timeout or execution issue)
                print(f"Indirizzo {hex(addr)}: RILEVATO (Crash/GDB Error)")
                rilevati_os += 1
            finally:
                bp.delete()

        print("\n--- RISULTATI FINALI GLOBALI AES ---")
        print(f"Istruzioni testate: {len(indirizzi)}")
        print(f"Rilevati da OS (Crash): {rilevati_os}")
        print(f"Rilevati da ANB: {rilevati_anb}")
        print(f"Silent Errors (Sbagliato, non rilevato): {silent_errors}")
        print(f"Benign Faults (Sopravvissuto, cifratura OK): {benign_faults}")

class SkipbreakPoint(gdb.Breakpoint):
    def __init__(self, addr):
        super().__init__(f"*{addr}", gdb.BP_BREAKPOINT, internal=True)
    def stop(self):
        arch = gdb.newest_frame().architecture()
        pc = int(gdb.parse_and_eval("$pc"))
        istruzione = arch.disassemble(pc, pc + 15)[0]
        lunghezza = istruzione['length']
        gdb.execute(f"set $pc = $pc + {lunghezza}", to_string=True)
        return False

ComandoTestAll()
