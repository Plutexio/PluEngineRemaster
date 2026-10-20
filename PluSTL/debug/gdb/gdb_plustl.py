"""
GDB Pretty Printers dla kontenerow i smart pointerow PluSTL.

Obsluguje:
  - Plu::DynamicArray<T, Allocator>         (Array/Array.h)
  - Plu::HashMap<TKey, TValue, ...>         (HashMap/HashMap.h)
  - Plu::HashSet<T, ...>                    (HashSet/HashSet.h)
  - Plu::BasicPath<CharT, ...>  (Path/PathW) (Path/Path.h)
  - Plu::TOwningPointer<T>                  (Pointers/TOwningPointer.h)
  - Plu::TUsePointer<T>                     (Pointers/TUsePointer.h)

BasicString / String / StringW maja wlasny printer w gdb_plustring.py.

Uzycie: `source gdb_plustl.py` w GDB albo wpis w .gdbinit (patrz debug/gdb/README.md).
"""

import gdb
import gdb.printing


def _atomic_value(val):
    """Odczyt std::atomic<int> z libstdc++ (_M_i); fallback na bezposredni int()."""
    try:
        return int(val['_M_i'])
    except (gdb.error, KeyError):
        try:
            return int(val)
        except gdb.error:
            return -1


# =============================================================================
# Plu::DynamicArray<T, Allocator>  -- mData / mSize / mCapacity
# =============================================================================
class DynamicArrayPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(self.val['mSize'])
        capacity = int(self.val['mCapacity'])
        return f"DynamicArray [size={size}, cap={capacity}]"

    def children(self):
        size = int(self.val['mSize'])
        data = self.val['mData']
        if int(data) == 0:
            return
        for i in range(size):
            yield f'[{i}]', data[i]

    def display_hint(self):
        return 'array'


# =============================================================================
# Plu::HashMap<TKey, TValue, ...>  -- separate chaining (HashMap/HashMap.h)
#   mBuckets (Node**), mBucketCount, mElementCount
#   Node{ Storage: alignas(pair) unsigned char[sizeof(pair)], Next }
#   Storage jest surowym buforem (wezly z listy wolnych nie maja zywego elementu),
#   wiec reinterpretujemy go jako std::pair<TKey, TValue> tak samo jak w HashSet.
# =============================================================================
class HashMapPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        count = int(self.val['mElementCount'])
        buckets = int(self.val['mBucketCount'])
        return f"HashMap [elements={count}, buckets={buckets}]"

    def _pair_ptr_type(self):
        key_type = self.val.type.template_argument(0)
        value_type = self.val.type.template_argument(1)
        return gdb.lookup_type(f'std::pair<{key_type.name}, {value_type.name}>').pointer()

    def children(self):
        buckets = self.val['mBuckets']
        if int(buckets) == 0:
            return
        bucket_count = int(self.val['mBucketCount'])
        pair_ptr_type = self._pair_ptr_type()
        idx = 0
        for i in range(bucket_count):
            node = buckets[i]
            while int(node) != 0:
                data = node['Storage'].address.cast(pair_ptr_type).dereference()
                yield f'[{idx}] key', data['first']
                yield f'[{idx}] value', data['second']
                idx += 1
                node = node['Next']

    def display_hint(self):
        return 'map'


# =============================================================================
# Plu::HashSet<T, ...>  -- open addressing, linear probing
#   mSlots (Slot*), mCapacity, mSize, mDeletedCount
#   Slot{ Storage: alignas(T) unsigned char[sizeof(T)], State }
#   State: enum class { Empty=0, Occupied=1, Deleted=2 }
# =============================================================================
class HashSetPrinter:
    OCCUPIED = 1

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(self.val['mSize'])
        capacity = int(self.val['mCapacity'])
        deleted = int(self.val['mDeletedCount'])
        return f"HashSet [size={size}, cap={capacity}, deleted={deleted}]"

    def children(self):
        slots = self.val['mSlots']
        if int(slots) == 0:
            return
        capacity = int(self.val['mCapacity'])
        elem_type = self.val.type.template_argument(0)
        elem_ptr_type = elem_type.pointer()
        idx = 0
        for i in range(capacity):
            slot = slots[i]
            if int(slot['State']) == self.OCCUPIED:
                # Storage to wyrownany bufor bajtow; reinterpretuj jako T.
                value = slot['Storage'].address.cast(elem_ptr_type).dereference()
                yield f'[{idx}]', value
                idx += 1

    def display_hint(self):
        return 'array'


# =============================================================================
# Plu::BasicPath<CharT, ...>  (Path / PathW)  -- pojedyncze pole mPath (BasicString)
# =============================================================================
class BasicPathPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        # Zwracamy gdb.Value typu BasicString -> GDB zastosuje jego wlasny printer.
        return self.val['mPath']

    def display_hint(self):
        return 'string'


# =============================================================================
# Plu::TOwningPointer<T> / Plu::TUsePointer<T>
#   pole control (ControlBlockBase*); ControlBlockBase{ ptr (void*),
#   strongCount (atomic<int>), weakCount (atomic<int>), externallyOwned, owningThread }
#   Liczba zywych TUsePointerow = weakCount - 1 (kolektywna ref wlascicieli) gdy strong>0.
# =============================================================================
class _ControlPointerPrinter:
    KIND = "TPointer"

    def __init__(self, val):
        self.val = val

    def _control(self):
        return self.val['control']

    def to_string(self):
        control = self._control()
        if int(control) == 0:
            return f"{self.KIND} = nullptr"

        base = control.dereference()
        ptr = base['ptr']
        strong = _atomic_value(base['strongCount'])
        weak = _atomic_value(base['weakCount'])
        uses = (weak - 1) if strong > 0 else weak

        if int(ptr) == 0:
            return f"{self.KIND} <expired> [strong={strong}, uses={uses}]"
        return f"{self.KIND} @ {ptr} [strong={strong}, uses={uses}]"

    def children(self):
        control = self._control()
        if int(control) == 0:
            return
        base = control.dereference()
        ptr = base['ptr']
        if int(ptr) == 0:
            return
        elem_type = self.val.type.template_argument(0)
        yield 'pointee', ptr.cast(elem_type.pointer()).dereference()


class TOwningPointerPrinter(_ControlPointerPrinter):
    KIND = "TOwningPointer"


class TUsePointerPrinter(_ControlPointerPrinter):
    KIND = "TUsePointer"


# =============================================================================
# Rejestracja
# =============================================================================
def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("PluSTL")
    pp.add_printer('DynamicArray',   '^Plu::DynamicArray<.*>$',     DynamicArrayPrinter)
    pp.add_printer('HashMap',        '^Plu::HashMap<.*>$',          HashMapPrinter)
    pp.add_printer('HashSet',        '^Plu::HashSet<.*>$',          HashSetPrinter)
    pp.add_printer('BasicPath',      '^Plu::BasicPath<.*>$',        BasicPathPrinter)
    pp.add_printer('TOwningPointer', '^Plu::TOwningPointer<.*>$',   TOwningPointerPrinter)
    pp.add_printer('TUsePointer',    '^Plu::TUsePointer<.*>$',      TUsePointerPrinter)
    return pp


gdb.printing.register_pretty_printer(
    gdb.current_objfile(),
    build_pretty_printer(),
    replace=True
)

print("PluSTL pretty printers zaladowane: "
      "DynamicArray, HashMap, HashSet, Path/PathW, TOwningPointer, TUsePointer")
