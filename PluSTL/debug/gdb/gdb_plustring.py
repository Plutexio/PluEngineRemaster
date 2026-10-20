import gdb
import gdb.printing


class BasicStringPrinter:
    """Pretty printer dla Plu::BasicString<CharT, Allocator>"""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            # Pobierz typ znaku
            char_type = self.val.type.template_argument(0)
            char_size = char_type.sizeof
            
            # Bezpieczny odczyt pól - użyj cast do odpowiednich typów
            length_field = self.val['mLength']
            capacity_field = self.val['mCapacity']
            
            # Konwertuj na Python int przez cast
            size_t = gdb.lookup_type('size_t')
            length = int(length_field.cast(size_t))
            capacity = int(capacity_field.cast(size_t))
            
            # Oblicz SSO capacity
            if char_size > 0:
                sso_capacity = (24 // char_size) - 1
            else:
                sso_capacity = 23  # domyślnie dla char
            
            # Sprawdź czy używa heap
            is_heap = capacity > sso_capacity
            
            # Pobierz dane
            if is_heap:
                data_ptr = self.val['mHeapData']
            else:
                # Dla SSO używamy tablicy
                data_ptr = self.val['mSsoBuffer'].address
            
            # Sprawdź wskaźnik
            if not data_ptr or int(data_ptr.cast(gdb.lookup_type('uintptr_t'))) == 0:
                return '""'
            
            # Odczytaj string
            if char_type.name == 'char':
                try:
                    # Użyj inferior do odczytu pamięci
                    inferior = gdb.selected_inferior()
                    data_addr = int(data_ptr.cast(gdb.lookup_type('uintptr_t')))
                    memory = inferior.read_memory(data_addr, length)
                    string_value = memory.tobytes().decode('utf-8', errors='replace')
                    
                    storage = "heap" if is_heap else "SSO"
                    return f'"{string_value}" [len={length}, cap={capacity}, {storage}]'
                except:
                    # Fallback - spróbujstring()
                    try:
                        string_value = data_ptr.string()
                        storage = "heap" if is_heap else "SSO"
                        return f'"{string_value}" [len={length}, cap={capacity}, {storage}]'
                    except:
                        return f'"<błąd odczytu>" [len={length}, cap={capacity}]'
            
            elif char_type.name == 'wchar_t':
                try:
                    inferior = gdb.selected_inferior()
                    data_addr = int(data_ptr.cast(gdb.lookup_type('uintptr_t')))
                    wchar_size = char_size
                    memory = inferior.read_memory(data_addr, length * wchar_size)
                    
                    # Konwertuj wchar_t do stringa
                    chars = []
                    for i in range(length):
                        offset = i * wchar_size
                        if wchar_size == 4:
                            wc = int.from_bytes(memory[offset:offset+4].tobytes(), 'little')
                        else:  # wchar_size == 2
                            wc = int.from_bytes(memory[offset:offset+2].tobytes(), 'little')
                        
                        if wc < 128 and wc > 0:
                            chars.append(chr(wc))
                        else:
                            chars.append(f'\\u{wc:04x}')
                    
                    string_value = ''.join(chars)
                    storage = "heap" if is_heap else "SSO"
                    return f'L"{string_value}" [len={length}, cap={capacity}, {storage}]'
                except Exception as e:
                    return f'L"<błąd odczytu: {e}>" [len={length}, cap={capacity}]'
            
            else:
                return f'"<nieobsługiwany typ: {char_type.name}>"'
        
        except gdb.error as e:
            return f'"<błąd GDB: {e}>"'
        except Exception as e:
            return f'"<błąd: {e}>"'

    def display_hint(self):
        return 'string'


def build_pretty_printer():
    """Buduje obiekt pretty printera dla rejestracji w GDB"""
    pp = gdb.printing.RegexpCollectionPrettyPrinter("Plu")
    pp.add_printer('BasicString', '^Plu::BasicString<.*>$', BasicStringPrinter)
    return pp


# Rejestracja pretty printera
gdb.printing.register_pretty_printer(
    gdb.current_objfile(),
    build_pretty_printer(),
    replace=True
)

print("GDB Pretty Printer dla Plu::BasicString załadowany!")
