# Debug — GDB Pretty Printers dla PluSTL

Pretty-printery GDB, które pokazują kontenery i smart pointery PluSTL w czytelnej
formie zamiast surowych pól (`m_Data`, `mSlots`, `control`, …).

## Pliki

| Plik | Typy |
|---|---|
| `gdb_plustl.py` | `Plu::DynamicArray<T>`, `Plu::HashMap<K,V>`, `Plu::HashSet<T>`, `Plu::Path`/`PathW` (`BasicPath`), `Plu::TOwningPointer<T>`, `Plu::TUsePointer<T>` |
| `gdb_plustring.py` | `Plu::String`/`StringW` (`BasicString<CharT>`) — z detekcją SSO/heap |

## Co pokazują

```
DynamicArray [size=3, cap=4] = {10, 20, 30}
"krotki" [len=6, cap=23, SSO]
"/home/Plutex/test/plik.txt" [len=26, cap=26, heap]
HashSet [size=3, cap=16, deleted=0] = {2, 1, 3}
HashMap [elements=2, buckets=8] = {[200] = "dwiescie", [100] = "sto"}
TOwningPointer @ 0x55…e020 [strong=1, uses=1] = {pointee = {a = 7, b = 9}}
TUsePointer @ 0x55…e020 [strong=1, uses=1] = {pointee = {a = 7, b = 9}}
```

- `strong` = liczba `TOwningPointer` (strongCount).
- `uses` = liczba żywych `TUsePointer` (`weakCount - 1`, czyli bez kolektywnej
  referencji właścicieli). Wygasły obiekt: `<expired>`; pusty pointer: `= nullptr`.
- String pokazuje `len`, `cap` i czy siedzi w buforze SSO czy na stercie.

## Ładowanie

`.gdbinit` w katalogu głównym projektu sourcuje już oba pliki (ścieżki względne
`./PluSTL/debug/gdb/…`, więc GDB musi startować z katalogu projektu):

```
source ./PluSTL/debug/gdb/gdb_plustl.py
source ./PluSTL/debug/gdb/gdb_plustring.py
```

### Ręcznie w sesji GDB
```gdb
source /path/to/PluSTL/debug/gdb/gdb_plustl.py
source /path/to/PluSTL/debug/gdb/gdb_plustring.py
```

### CLion  ← najczęstszy problem: „widzę tylko raw"

CLion używa **własnego, dołączonego GDB** (`…/clion/bin/gdb/linux/x64/bin/gdb`) i
**nie** sourcuje projektowego `.gdbinit` — blokuje go `auto-load safe-path`, a do
tego ścieżki względne `./PluSTL/debug/gdb/…` nie rozwiążą się, gdy CWD debugera
nie jest katalogiem projektu.

Rozwiązanie (działa niezależnie od CWD) — wpis w **`~/.gdbinit`** z absolutnymi
ścieżkami; dołączony GDB CLiona czyta home-init przy starcie:

```gdb
add-auto-load-safe-path /home/Plutex/CLionProjects/PluEngine
source /path/to/PluSTL/debug/gdb/gdb_plustl.py
source /path/to/PluSTL/debug/gdb/gdb_plustring.py
```

Jeśli po tym dalej widać raw, sprawdź dwa przełączniki CLiona i zrestartuj sesję debug:
1. **Settings → Build, Execution, Deployment → Debugger → Data Views → C/C++** →
   włącz *„Enable Python renderers / GDB pretty printers"* (bez tego panel Variables
   nie używa printerów, nawet gdy są załadowane).
2. **Settings → Build, Execution, Deployment → Debugger** → opcja ładowania
   `.gdbinit` (jeśli w danej wersji istnieje) musi być włączona.

Weryfikacja: w sesji debug otwórz zakładkę konsoli GDB i wpisz `print twojaZmienna` —
na starcie powinien też mignąć komunikat „PluSTL pretty printers zaladowane: …".

### VS Code (cppdbg)
W `launch.json` w konfiguracji:
```json
"setupCommands": [
  { "text": "source ${workspaceFolder}/PluSTL/debug/gdb/gdb_plustl.py" },
  { "text": "source ${workspaceFolder}/PluSTL/debug/gdb/gdb_plustring.py" }
]
```

## Uwagi implementacyjne

- Printery odczytują pola po nazwach z nagłówków PluSTL. Po zmianie layoutu
  kontenera (nazwy pól w `Array.h`, `HashMap.h`, `HashSet.h`, `Path.h`,
  `ControlBlock.h`) trzeba zaktualizować odpowiedni printer.
- `strongCount`/`weakCount` to `std::atomic<int>` — czytane przez `_M_i`
  (libstdc++), z fallbackiem.
- Wymaga buildu z symbolami debug (presety `PluDebugLinux-*`).
