import os
import sys
import gdb

class ComandoTest(gdb.Command):
    def __init__(self):
        super().__init__("avvia_test",gdb.COMMAND_USER)

    def invoke(self,arg,from_tty):
        nome_funzione = arg.strip()
        if not arg:
            return


        gdb.execute(f"set confirm off")
        gdb.execute(f"set pagination off")
        gdb.execute(f"tbreak {nome_funzione}")
        gdb.execute(f"run > /dev/null 2>&1")

        indirizzi = []
        frame = gdb.newest_frame()
        blocco = frame.block()
        inizio = blocco.start
        fine = blocco.end
        arch = frame.architecture()
        istruzioni = arch.disassemble(inizio,fine)
        for istr in istruzioni:
            indirizzi.append(istr['addr'])

        rilevati_anb = 0
        rilevati_os = 0
        silent_errors = 0
        benign_faults = 0

        for addr in indirizzi:
            bp = SkipbreakPoint(addr)
            try:
                # Eseguiamo redirigendo su file
                gdb.execute(f"run > /tmp/anb_test_out.txt 2>&1", to_string=True)
                
                inf = gdb.selected_inferior()
                
                # corrispondente all'indirizzo
                sal = gdb.find_pc_line(addr)
                if sal.symtab:
                    line_info = f"({sal.symtab.filename}:{sal.line})"
                else:
                    line_info = "(linea sconosciuta)"

                if inf.pid > 0:
                    # Il programma è crashato con un segnale (SIGSEGV, SIGABRT, SIGILL)
                    gdb.execute("kill", to_string=True)
                    rilevati_os += 1
                    print(f"Indirizzo {hex(addr)} {line_info}: RILEVATO (Crash/Segnale OS)")
                else:
                    exitcode = int(gdb.parse_and_eval("$_exitcode"))
                    
                    # Leggiamo l'output
                    try:
                        with open("/tmp/anb_test_out.txt", "r") as f:
                            out = f.read()
                    except:
                        out = ""

                    if exitcode != 0:
                        # Verifichiamo se è un errore ANB o un silent error
                        if "[FAIL]" in out:
                            silent_errors += 1
                            print(f"Indirizzo {hex(addr)} {line_info}: SILENT ERROR (Cifratura sbagliata!)")
                        else:
                            rilevati_anb += 1
                            print(f"Indirizzo {hex(addr)} {line_info}: RILEVATO (Mismatch ANB / Exit {exitcode})")
                    else:
                        benign_faults += 1
                        print(f"Indirizzo {hex(addr)} {line_info}: BENIGN (Sopravvissuto e Cifratura Corretta)")
                        
            except gdb.error:
                rilevati_os += 1
                print(f"Indirizzo {hex(addr)} {line_info}: RILEVATO (GDB Error)")
            finally:
                bp.delete() # Eliminiamo qui in modo sicuro!

        print("\n--- RISULTATI FINALI ---")
        print(f"Istruzioni testate: {len(indirizzi)}")
        print(f"Rilevati da OS (Crash): {rilevati_os}")
        print(f"Rilevati da ANB: {rilevati_anb}")
        print(f"Silent Errors (Sbagliato, non rilevato): {silent_errors}")
        print(f"Benign Faults (Sopravvissuto, cifratura OK): {benign_faults}")

#salto istruzioni

class SkipbreakPoint(gdb.Breakpoint):
    def __init__(self, addr):
        super().__init__(f"*{addr}", internal=True)
        self.silent = True # Evita stampe inutili a schermo
    def stop(self): # quando vede il breakpoint si blocca
        frame = gdb.newest_frame() #salva la funzione
        arch  = frame.architecture()
        pc = int(gdb.parse_and_eval("$pc"))

        #ottieni lunghezza dell'istruzione
        instruction = arch.disassemble(pc, count=1)[0]
        length = instruction['length']

        #aggiorna il pc sommando length a pc
        gdb.execute(f"set $pc = $pc + {length}")
        self.enabled = False # Evita il crash disabilitando invece di cancellare
        return False







ComandoTest()



