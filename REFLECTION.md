# Reflection System — PluEngine

Makra refleksji są no-opami w C++ (`#define PLU_CLASS(...)`) — działają tylko jako adnotacje dla `ReflectionGeneratorRegex.py`, który parsuje je przez libclang i generuje pliki `*.generated.h` do `ReflectionCache/`.

> **Zawsze uruchom generator po zmianie jakiegokolwiek makra refleksji.**
> ```bash
> cd PythonTools && python ReflectionGeneratorRegex.py
> # Tylko jeden plik (szybciej):
> python ReflectionGeneratorRegex.py --file path/to/MyClass.h
> ```

---

## PLU_CLASS(...)

Oznacza klasę do refleksji. Wymagane jest makro `REFLECTION_BODY_CLASSNAME()` wewnątrz definicji klasy.

| Specifier | Opis |
|---|---|
| `Abstract` | Klasa abstrakcyjna — nie jest rejestrowana jako bezpośrednio instancjonowalna |
| `PyExport` | Eksportuje klasę do Pythona przez pybind11 |
| `PyDerive` | Pozwala na tworzenie podklas tej klasy z Pythona (generuje trampoline) |
| `NoReflection` | Generuje tylko bindingi Pythona; pomija `.generated.h` i rejestrację w `TypeRegistry` |
| `UUID=FieldName` | Wskazuje pole `PluUUID` jako identyfikator klasy (np. `UUID=Uuid`) |

```cpp
// Klasa eksportowana do Pythona, możliwa do podklasowania
PLU_CLASS(PyExport, PyDerive)
class MyComponent : public GameObjectComponent {
    REFLECTION_BODY_MYCOMPONENT()
    ...
};

// Abstrakcyjna klasa bazowa
PLU_CLASS(Abstract)
class IManager : public EngineObject {
    REFLECTION_BODY_IMANAGER()
    ...
};

// Klasa z UUID i tylko bindingami Pythona (bez pełnej refleksji)
PLU_CLASS(NoReflection)
class InputHandler : public EngineObject { ... };
```

---

## PLU_STRUCT(...)

Jak `PLU_CLASS`, ale dla struktur (domyślny access to `public`).

| Specifier | Opis |
|---|---|
| `PyExport` | Eksportuje strukturę do Pythona |
| `UUID=FieldName` | Wskazuje pole `PluUUID` jako identyfikator struktury |
| `NoVirtualClass` | Wymusza niewirtualne `GetClass()` — struct zostaje POD-em bez vtable (patrz niżej) |

```cpp
PLU_STRUCT(PyExport, UUID=Uuid)
struct AssetInfo {
    REFLECTION_BODY_ASSETINFO()

    PLU_PROPERTY(PyExport)
    PluUUID Uuid;

    PLU_PROPERTY(PyExport)
    String Name;
};
```

### Wirtualne `GetClass()` w hierarchiach structów

Struct **uczestniczący w hierarchii zreflektowanych structów** dostaje **wirtualne**
`GetClass()`, dzięki czemu przez wskaźnik/referencję do bazy zwraca właściwy `TypeInfo*`:

- struct dziedziczący po nie-POD zreflektowanym structcie → `virtual GetClass() override;`
- struct będący bazą dla nie-POD zreflektowanego structa → wprowadza `virtual GetClass();`
- samodzielny struct (bez relacji) → zwykłe, niewirtualne `GetClass();`

`GetClass()` klas (`PLU_CLASS`) jest wirtualne zawsze (jak dotąd).

**`NoVirtualClass`** wyłącza to dla danego structa — zostaje bez vtable i przerywa
propagację wirtualności przez ten węzeł. Używaj dla POD-ów o ściśle kontrolowanym
układzie pamięci, np. wierzchołków wysyłanych surowo na GPU (`Vertex`, `SkeletalVertex`
w `StaticMesh.h`/`SkeletalMesh.h`), gdzie dodanie vtable rozjechałoby `offsetof`/`sizeof`
i wysyłkę bufora. Jeśli oznaczysz bazę jako `NoVirtualClass`, oznacz też pochodne structy
w tej gałęzi (nie da się `override` po niewirtualnej bazie).

---

## PLU_ENUM(...)

Oznacza enum do refleksji.

| Specifier | Opis |
|---|---|
| `PyNamespace=X` | **WYMAGANY.** Namespace w Pythonie (prawie zawsze `PyNamespace=Plu`) |
| `PyExport` | Eksportuje enum do Pythona |

> **Uwaga:** pominięcie `PyNamespace=Plu` powoduje błąd kompilacji.

```cpp
PLU_ENUM(PyNamespace=Plu)
enum class EPhysicsBodyType {
    Static,
    Dynamic,
    Kinematic,
};

// Enum eksportowany i dostępny z Pythona jako Plu.EInputKey
PLU_ENUM(PyExport, PyNamespace=Plu)
enum class EInputKey { ... };
```

---

## PLU_INTERFACE(...)

Wariant `PLU_CLASS` dla klas-interfejsów. Parametry analogiczne jak `PLU_CLASS`.

---

## PLU_PROPERTY(...)

Oznacza pole (member variable) do refleksji. Umieszcza się bezpośrednio nad deklaracją pola.

| Specifier | Opis |
|---|---|
| `PyExport` | Eksportuje pole do Pythona (getter + setter) |
| `PyReadOnly` | Pole dostępne z Pythona tylko do odczytu (tylko getter) |
| `UuidFor=ClassName` | Pole musi być typu `PluUUID`; informuje edytor, że UUID odnosi się do danej klasy (picker UI) |
| `Getter=Method` / `Setter=Method` | Access the field through these methods instead of `offsetof`: the generator emits `PropertyInfo::GetterPtr`/`SetterPtr` (used by the editor and serialization) and a pybind `def_property`. Either one may be given alone — the missing side falls back to reading/assigning the field directly (e.g. `PLU_PROPERTY(Setter=SetBodyType)` so an edit dispatches an event, while reads stay plain). |

```cpp
PLU_PROPERTY(PyExport)
Vec3 Location;

PLU_PROPERTY(PyExport, PyReadOnly)
float Mass;

PLU_PROPERTY(UuidFor=IShaderCode)
PluUUID VertexShaderUuid;
```

---

## PLU_FUNCTION(...)

Zachowanie różni się zależnie od tego, czy funkcja jest **metodą klasy** czy **funkcją globalną**.

### Metoda klasy

Umieszcza się nad deklaracją metody wewnątrz klasy/struktury.

| Specifier | Opis |
|---|---|
| `PyExport` | Eksportuje metodę do Pythona |
| `PyOverride` | Metoda może być nadpisana w Pythonie (wymaga `PyDerive` na klasie; generuje trampoline). Błąd kompilacji jeśli klasa nie ma `PyDerive` |
| `PyNotCallable` | Metoda zarejestrowana w refleksji, ale **nie** dostępna do wywołania z Pythona |

```cpp
// Metoda nadpisywalna z Pythona
PLU_FUNCTION(PyOverride)
virtual void OnBeginPlay();

// Metoda tylko po stronie C++ (np. wewnętrzny callback)
PLU_FUNCTION(PyNotCallable)
void InternalTick(float DeltaTime);

// Zwykła metoda eksportowana do Pythona
PLU_FUNCTION(PyExport)
void SetVelocity(Vec3 Velocity);
```

#### Parsowanie deklaracji, wartości domyślne i typy parametrów

- Generator parsuje deklarację z **jednej linii**. Ciało inline jest OK, o ile mieści się w tej samej
  linii i nie ma zagnieżdżonych klamr (`virtual void OnPossessed(...) {};`). Ciało rozbite na kilka
  linii → funkcja jest **cicho pomijana**; wtedy trzeba przenieść definicję do `.cpp`.
- **Wartości domyślne** trafiają do `py::arg("x") = ...`, ale tylko gdy pybind11 na pewno je
  skonwertuje przy rejestracji modułu: literały (`1000.0f`, `true`, `nullptr`, `"str"`), puste
  kontenery PluSTL (`DynamicArray<T>{}`) oraz konstrukcje/wartości typów i enumów wystawionych do
  Pythona (e.g. `CollisionResponse::Block`). Enumy są rejestrowane w module
  przed klasami właśnie po to, żeby mogły być domyślnymi argumentami. Typy matematyczne
  (`Vec3()`, `Matrix4{}`) idą jeszcze przed enumami — patrz niżej.
- Parametry, które w bindingach zmieniają typ (`TClassPointer<T>` → `py::object`,
  `std::function<>` → `py::function`), domyślnej wartości **nie dostają** — po stronie Pythona
  pozostają wymagane.
- Smart-pointer parameters have no pybind11 caster, so Python always sees raw pointers:
  - `TUsePointer<T>` for an asset `T` → `IAssetData*`, recovered with `GetAssetUserAsRaw`.
  - `TUsePointer<T>` for an `EngineObject` `T` (e.g. `GameObject::OnOverlapBegin`) → `T*`, recovered
    from the object's handle with `UserFromPython` (`PythonPointers.h`); null or a dead object gives
    a null pointer. PyOverride trampolines call the Python override with `.GetRaw()`.
  - Any other `TUsePointer<T>`, and `TOwningPointer<T>` for a non-asset `T`, still drop the whole
    function from the bindings — it works in C++ and reflection, but not in Python (also not as
    `PyOverride`). Take `T*` instead if it has to be reachable from Python.

### Funkcja globalna (poza klasą)

Funkcje globalne oznaczone `PLU_FUNCTION` są **automatycznie eksportowane do Pythona** — `PyExport` nie jest potrzebny ani rozpoznawany. Jedynym dostępnym specifierem jest `PyNotCallable`, który wyklucza funkcję z bindingów.

`PyOverride` nie ma zastosowania dla funkcji globalnych.

Funkcje globalne **nie generują `.generated.h`** — generator tworzy tylko wpis w module Python.

```cpp
// Automatycznie dostępna w Pythonie jako Plu.GetForwardVector(...)
PLU_FUNCTION()
PLU_API Vec3 GetForwardVector(Vec3 rot);

// Zarejestrowana w refleksji, ale wykluczona z Pythona
PLU_FUNCTION(PyNotCallable)
PLU_API void InternalHelper();
```

---

## Math types in Python

`Vec2/3/4`, `IVec2/3/4`, `Quaternion` and `Matrix4` are ordinary pybind11 classes, bound by hand in
`LibEngine/src/PluEngine/Python/PythonMath.cpp` (`RegisterMathTypes`). The generator treats them like
any other registered type: no converter lambda, no unpacking — a `Vec3` parameter is a `Vec3`
parameter, and a `Vec3` field becomes a plain `def_readwrite`.

The generated module calls `RegisterMathTypes(m)` as its **first** statement, before enums and
classes, because pybind11 resolves parameter types, return types and default arguments at `.def()`
time. Anything registered earlier could not use them.

What this means when writing engine code:

- Tuples and lists convert implicitly, so `SetObjectLocation((1, 2, 3))` still works, as does
  `location == (1, 2, 3)`. Strings deliberately do not convert, even though they are sequences.
- Vectors returned to Python are real objects: `.x/.y/.z`, indexing, iteration, `Length()`,
  `Normalized()`, `Dot()`, `Cross()` (Vec3), `Lerp()`. Unpacking (`x, y, z = location`) still works.
- `PLU_PROPERTY` on a math field becomes `def_readwrite`, whose getter is
  `return_value_policy::reference_internal` — `obj.Location.x = 5.0` mutates the C++ object in place.
  A property exposed through a **getter/setter pair** returns a copy instead, so the same line there
  changes a temporary and is silently lost. Prefer the plain field when a script should mutate it.
- Integer vectors have no division operator: a component-wise integer divide by zero would take the
  editor down with SIGFPE, and a script cannot guard against what it cannot see.
- Adding a type to `RegisterMathTypes` means updating `CPP_TO_PY_TYPE`, `MATH_TYPE_NAMES` and
  `MATH_VECTOR_STUBS` in `ReflectionGeneratorRegex.py` — the stub writer mirrors the bindings by hand.

---

## Reguły walidacji generatora

- `PyOverride` na metodzie bez `PyDerive` na klasie → `#error` w wygenerowanym pliku
- `UuidFor=X` na polu o typie innym niż `PluUUID` → ostrzeżenie w logu generatora
- `PLU_ENUM` bez `PyNamespace=` → błąd kompilacji (puste namespace w bindingu)
- `PyReadOnly` działa tylko razem z `PyExport` (samo `PyReadOnly` bez eksportu jest ignorowane)

---

## REFLECTION_BODY macro

Każda klasa/struct z `PLU_CLASS`/`PLU_STRUCT` musi mieć w ciele klasowym:

```cpp
REFLECTION_BODY_CLASSNAME()   // zamień CLASSNAME na nazwę klasy wielkimi literami
```

Makro to jest generowane do `ReflectionCache/ClassName.generated.h` i zawiera rejestrację `TypeInfo`, metadanych pól i metod.
