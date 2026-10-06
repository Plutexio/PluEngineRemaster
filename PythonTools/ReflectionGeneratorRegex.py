import argparse
import ast
import io
import json
import os
import re
import sys
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import List, Optional

# ─────────────────────────────────────────────
#  Kolorowanie wyjścia
# ─────────────────────────────────────────────

# Wyłączone gdy stdout nie jest terminalem (np. pre-build step CMake),
# żeby kody ANSI nie zaśmiecały logów builda.
_USE_COLOR = sys.stdout.isatty() and os.name != "nt"


def _c(Code: str, Text: str) -> str:
    return f"\033[{Code}m{Text}\033[0m" if _USE_COLOR else Text


# ─────────────────────────────────────────────
#  Typy
# ─────────────────────────────────────────────

class ClassType(Enum):
    CLASS     = 1
    STRUCT    = 2
    ENUM      = 3
    INTERFACE = 4
    UNKNOWN   = 5


@dataclass
class PropertyInfo:
    Name:         str
    Type:         str
    Params:       List[str]
    UuidForClass: str = ""
    GetterName:   str = ""   # z Getter=X w PLU_PROPERTY
    SetterName:   str = ""   # z Setter=X w PLU_PROPERTY


@dataclass
class ParamInfo:
    """Pojedynczy parametr funkcji."""
    Type: str
    Name: str
    Default: str = ""   # surowy tekst wartosci domyslnej z C++ (bez "="), pusty gdy brak


@dataclass
class FunctionInfo:
    """Funkcja/metoda oznaczona PLU_FUNCTION."""
    Name:        str
    ReturnType:  str
    Params:      List[ParamInfo]
    MacroParams: List[str]
    IsVirtual:   bool = False
    IsConst:     bool = False


@dataclass
class GlobalFunctionInfo:
    """Funkcja globalna (poza klasą) oznaczona PLU_FUNCTION."""
    Name:        str
    ReturnType:  str
    Params:      List[ParamInfo]
    MacroParams: List[str]
    FilePath:    Path


@dataclass
class TypeInfo:
    Name:             str
    Type:             ClassType
    FilePath:         Path
    Bases:            List[str]
    Project:          str
    ReflectionParams: List[str]
    Properties:       List[PropertyInfo]
    Functions:        List[FunctionInfo] = field(default_factory=list)
    UuidProperty:     Optional[PropertyInfo] = None


@dataclass
class FileData:
    FilePath:        Path
    Children:        List[TypeInfo]
    GlobalFunctions: List[GlobalFunctionInfo] = field(default_factory=list)
    Enums:           List["EnumInfo"]          = field(default_factory=list)


@dataclass
class EnumValueInfo:
    """Pojedyncza wartość enuma, opcjonalnie oznaczona PLU_ENUM_VALUE."""
    Name:        str
    MacroParams: List[str] = field(default_factory=list)


@dataclass
class EnumInfo:
    """Enum oznaczony PLU_ENUM."""
    Name:             str
    FilePath:         Path
    Project:          str
    ReflectionParams: List[str]
    Values:           List[EnumValueInfo] = field(default_factory=list)
    Namespace:        str = ""   # z PyNamespace=X, np. "Plu"
    BaseType:         str = ""   # np. "UInt16" z "enum class Key : UInt16"


@dataclass
class StructInfoInternal:
    Name:     str
    Bases:    List[str]
    FilePath: str


# ─────────────────────────────────────────────
#  Konfiguracja ścieżek i argumenty
# ─────────────────────────────────────────────

ScriptDir        = os.path.dirname(os.path.abspath(__file__))
ProjectToScanDir = os.path.dirname(ScriptDir)
ProjectDir       = ProjectToScanDir
OutputDir        = os.path.join(ProjectDir, "ReflectionCache")
QuietMode:    bool = False
ForceMode:    bool = False
BindingsMode: bool = False

ArgParser = argparse.ArgumentParser(
    prog="Reflection Generator",
    description="Generates reflection data for PluEngine (Regex-based)"
)
ArgParser.add_argument("-q", "--quiet",    action="store_true")
ArgParser.add_argument("-p", "--project",  type=str)
ArgParser.add_argument("-F", "--force",    action="store_true", help="Ignore ProcessedList.txt?")
ArgParser.add_argument("-b", "--bindings", action="store_true", help="Generate pybind11 bindings + .pyi stubs")
Args = ArgParser.parse_args()

QuietMode    = bool(Args.quiet)
ForceMode    = bool(Args.force)
BindingsMode = bool(Args.bindings)

if Args.project:
    if Args.project != "ALL":
        ProjectToScanDir = os.path.join(ProjectToScanDir, Args.project)
    else:
        print("ALL projects selected")
else:
    print("No project selected!")
    sys.exit(1)

if QuietMode:    print("QUIET")
if ForceMode:    print("FORCE")
if BindingsMode: print("BINDINGS (pybind11)")

# ─────────────────────────────────────────────
#  Wzorce regex
# ─────────────────────────────────────────────

# Makra refleksji nad typem
RE_PLU_CLASS     = re.compile(r"PLU_CLASS\s*\((.*?)\)")
RE_PLU_STRUCT    = re.compile(r"PLU_STRUCT\s*\((.*?)\)")
RE_PLU_INTERFACE = re.compile(r"PLU_INTERFACE\s*\((.*?)\)")
RE_PLU_ENUM      = re.compile(r"PLU_ENUM\s*\((.*?)\)")

# Makro nad polem
RE_PLU_PROPERTY  = re.compile(r"PLU_PROPERTY\s*\((.*?)\)")

# Makro nad funkcją
RE_PLU_FUNCTION  = re.compile(r"PLU_FUNCTION\s*\((.*?)\)")

# Makro nad wartością enuma (opcjonalne)
RE_PLU_ENUM_VALUE = re.compile(r"PLU_ENUM_VALUE\s*\((.*?)\)")

# Deklaracja enum class (tylko enum class – zwykłe enum nie są wspierane)
# Grupa (1) nazwa, opcjonalna grupa (2) typ bazowy po ':'
RE_ENUM_DECL = re.compile(r"^enum\s+class\s+(\w+)(?:\s*:\s*(\w+))?")

# Deklaracja funkcji – pełna wersja wyciągająca virtual, return type, nazwę, parametry, const
# Grupy: (1) "virtual " lub None, (2) typ zwracany, (3) "*" lub None (wskaźnik przy nazwie), (4) nazwa, (5) raw params, (6) " const" lub None
RE_FUNC_DECL_FULL = re.compile(
    r"^(virtual\s+)?"
    r"(?:\[\[\w+\]\]\s+)*"            # atrybuty [[nodiscard]] itp.
    r"((?:[\w:<>*&,\s]+?))"           # (2) typ zwracany (non-greedy)
    r"\s*(\*)?\s*"                    # (3) opcjonalny * przy nazwie (wskaźnik)
    r"(\w+)\s*"                       # (4) nazwa funkcji
    r"\(([^;]*?)\)"                   # (5) surowe parametry – pozwala na zagniezdzone () jak std::function<void()>
                                      #     oraz na domyslne wartosci z nawiasami klamrowymi (= {})
    r"(\s*const)?"                    # (6) const qualifier
    r"(?:\s*(?:override|final))*"     # override / final
    r"\s*(?:;|\{[^{}]*\}\s*;?)?\s*$", # opcjonalny ; albo jednolinijkowe ciało { ... } (z ; lub bez)
    re.MULTILINE
)

# Deklaracja klasy / struktury
RE_CLASS_DECL  = re.compile(r"^(?:class|struct)\s+(?:[A-Z_]+\s+)?(\w+)(?:\s+final)?\s*(?::\s*(.+))?$")
# Bazowe klasy – wyciągamy jednotkowe identyfikatory po pominięciu specyfikatorów dostępu
RE_BASE_ENTRY  = re.compile(r"(?:public|protected|private)?\s*(\w[\w:<>]*)")

# Access specifiers
RE_ACCESS_SPEC = re.compile(r"^(public|protected|private)\s*:\s*$")

# Deklaracja pola: np.  "float Speed;", "TArray<int32> Items;" lub "Vec3 C = Vec3(1,1,1);"
# Obsługuje szablony i wskaźniki. Treść inicjalizatora jest dowolna i tak czy siak
# odrzucana (łapiemy tylko typ i nazwę), więc dopuszczamy '(', '{' i ';' w literałach.
# Deklaracje funkcji nie przejdą: część typu nie może zawierać '(', a poza
# inicjalizatorem między nazwą a ';' dopuszczone są tylko białe znaki.
RE_FIELD_DECL  = re.compile(
    r"^([\w:<>*&\s,]+?)\s+(\w+)\s*(?:=\s*.+)?;$"
)

RE_LINE_COMMENT = re.compile(r'("[^"\\]*(?:\\.[^"\\]*)*"|\'(?:\\.|[^\'\\])*\')|//.*$|/\*.*?\*/')


def StripLineComment(Line: str) -> str:
    """Usuwa komentarze // i /* */ spoza literałów tekstowych/znakowych."""
    return RE_LINE_COMMENT.sub(lambda M: M.group(1) or "", Line).strip()


# ─────────────────────────────────────────────
#  Parsowanie parametrów makra
# ─────────────────────────────────────────────

def NormalizeAngleBrackets(Line: str) -> str:
    """
    Zamienia '(' i ')' wewnątrz nawiasów kątowych <> na placeholdery \x00/\x01.
    Pozwala RE_FUNC_DECL_FULL poprawnie matchować std::function<void()> i podobne.
    """
    Result = list(Line)
    Depth  = 0
    for I, C in enumerate(Result):
        if C == "<":   Depth += 1
        elif C == ">": Depth -= 1
        elif Depth > 0:
            if C == "(": Result[I] = "\x00"
            elif C == ")": Result[I] = "\x01"
    return "".join(Result)


def ParseMacroParams(RawParams: str) -> List[str]:
    """Dzieli surowy string parametrów po przecinku i czyści spacje."""
    if not RawParams.strip():
        return []
    return [P.strip() for P in RawParams.split(",") if P.strip()]


# Słowa, które same w sobie nigdy nie są kompletnym typem — jeśli to wszystko,
# co poprzedza ostatni token, ten token jest nazwą typu, nie nazwą parametru.
PARAM_TYPE_QUALIFIERS = {"const", "volatile", "struct", "class", "enum", "typename"}
# Wbudowane typy: segment złożony wyłącznie z nich to typ bez nazwy (np. "unsigned int").
PARAM_BUILTIN_KEYWORDS = PARAM_TYPE_QUALIFIERS | {
    "unsigned", "signed", "long", "short", "int", "char", "float", "double", "void", "bool",
}


def ParseFunctionParams(RawParams: str) -> List[ParamInfo]:
    """Parsuje listę parametrów funkcji C++ na listę ParamInfo."""
    RawParams = RawParams.strip()
    if not RawParams or RawParams == "void":
        return []
    Result: List[ParamInfo] = []
    # Dzielimy po przecinkach z uwzględnieniem szablonów (głębokość '<>')
    Depth, Cur = 0, []
    Segments: List[str] = []
    for Ch in RawParams:
        if Ch in "<({": Depth += 1
        elif Ch in ")>}": Depth -= 1
        if Ch == "," and Depth == 0:
            Segments.append("".join(Cur).strip()); Cur = []
        else:
            Cur.append(Ch)
    if Cur:
        Segments.append("".join(Cur).strip())

    for Seg in Segments:
        Seg = Seg.strip()
        if not Seg:
            continue
        # Odetnij default value (zapamiętujemy go – trafia do py::arg("x") = ... w bindingach)
        DefaultValue = ""
        if "=" in Seg:
            DefaultValue = Seg[Seg.index("=") + 1:].strip()
            Seg = Seg[:Seg.index("=")].strip()
        # Usuń const ref/ptr kwalifikatory (zostawiamy w typie)
        # Ostatni token to nazwa parametru, reszta to typ
        # Wyjątek: jeśli jeden token – to tylko typ bez nazwy
        Tokens = Seg.split()
        if len(Tokens) == 1:
            Result.append(ParamInfo(Type=Tokens[0], Name="", Default=DefaultValue))
            continue
        # Nazwa to ostatni token bez wiodących * / & (te należą do typu)
        ParamIdent = Tokens[-1].lstrip("*&")
        # Rozpoznaj parametry bez nazwy — ostatni token jest wtedy częścią typu:
        #  - sam kwalifikator, np. "const String &"
        #  - token z * / & / <> w środku, np. "const String&" (nie jest identyfikatorem)
        #  - przed nim stoją same kwalifikatory, np. "const String"
        #  - segment złożony z samych słów kluczowych, np. "unsigned int"
        IsUnnamed = (
            not ParamIdent
            or any(Ch in ParamIdent for Ch in "*&<>")
            or all(T in PARAM_TYPE_QUALIFIERS for T in Tokens[:-1])
            or all(T in PARAM_BUILTIN_KEYWORDS for T in Tokens)
        )
        if IsUnnamed:
            Result.append(ParamInfo(Type=Seg, Name="", Default=DefaultValue))
            continue
        ParamType = Seg[:Seg.rfind(ParamIdent)].strip()
        Result.append(ParamInfo(Type=ParamType, Name=ParamIdent, Default=DefaultValue))
    return Result


def ParseBases(BasesStr: Optional[str]) -> List[str]:
    """Wyciąga nazwy bazowych klas z fragmentu po ':' w deklaracji."""
    if not BasesStr:
        return []
    Result = []
    for Part in BasesStr.split(","):
        Part = Part.strip()
        # Usuń specyfikator dostępu
        Part = re.sub(r"^(public|protected|private)\s+", "", Part)
        Part = Part.strip()
        if Part:
            Result.append(Part)
    return Result


# ─────────────────────────────────────────────
#  Główny parser pliku
# ─────────────────────────────────────────────

def WriteIfChanged(Path: str, Content: str) -> bool:
    """
    Zapisuje Content do pliku tylko jeśli zawartość się zmieniła.
    Zwraca True jeśli plik został zapisany, False jeśli pominięto (bez zmian).
    Dzięki temu CMake nie widzi zmiany timestampu i nie rekompiluje pliku.
    """
    if os.path.exists(Path):
        try:
            with open(Path, "r", encoding="utf-8") as F:
                if F.read() == Content:
                    return False
        except OSError:
            pass
    os.makedirs(os.path.dirname(Path), exist_ok=True)
    with open(Path, "w", encoding="utf-8") as F:
        F.write(Content)
    return True


def ProcessFile(FilePath: Path, ProjectName: str) -> List[TypeInfo]:
    """
    Przechodzi plik linia po linii.
    Śledzi:
      - aktualny access specifier
      - makra PLU_CLASS / PLU_STRUCT / PLU_INTERFACE (oczekujemy deklaracji w następnej linii)
      - makro PLU_PROPERTY (oczekujemy deklaracji pola w następnej linii)
    """
    if not QuietMode:
        print(f"Processing {FilePath} ...")

    with open(FilePath, "r", encoding="utf-8", errors="ignore") as F:
        Lines = F.readlines()

    FoundTypes: List[TypeInfo]   = []
    CurrentType: Optional[TypeInfo] = None

    # Stan oczekiwania na następną linię
    PendingTypeMacro: Optional[str]      = None   # "CLASS" | "STRUCT" | "INTERFACE"
    PendingTypeMacroParams: List[str]    = []
    PendingPropertyMacro: bool           = False
    PendingPropertyParams: List[str]     = []
    PendingFunctionMacro: bool           = False
    PendingFunctionParams: List[str]     = []
    PendingEnumMacro: bool               = False
    PendingEnumMacroParams: List[str]    = []
    PendingEnumValueMacro: bool          = False
    PendingEnumValueParams: List[str]    = []

    # Funkcje globalne (poza klasą)
    GlobalFunctions: List[GlobalFunctionInfo] = []

    # Enumy
    FoundEnums:   List[EnumInfo]        = []
    CurrentEnum:  Optional[EnumInfo]    = None
    EnumStartDepth: int                 = -1

    # Aktualny access specifier (dla klas domyślnie private, dla struktur public)
    CurrentAccess: str = "private"

    # Prosta heurystyka głębokości nawiasów klamrowych
    BraceDepth:     int = 0
    TypeStartDepth: int = -1  # głębokość '{' na której zaczął się CurrentType

    for LineIdx, RawLine in enumerate(Lines):
        Line = RawLine.strip()

        # ── Liczenie nawiasów klamrowych ──────────────────────────────
        # Usuń komentarze inline i literały stringów żeby { } w nich nie psuły licznika
        LineForBraces = re.sub(r'//.*$', '', Line)              # // komentarz
        LineForBraces = re.sub(r'"[^"\\]*(?:\\.[^"\\]*)*"', '', LineForBraces)  # "string"
        BraceDepth += LineForBraces.count("{") - LineForBraces.count("}")

        # ── Zamknięcie bieżącego enuma ────────────────────────────────
        if CurrentEnum is not None and BraceDepth < EnumStartDepth:
            if not QuietMode:
                print(f"  [ENUM] {CurrentEnum.Name}  values={[V.Name for V in CurrentEnum.Values]}")
            FoundEnums.append(CurrentEnum)
            CurrentEnum    = None
            EnumStartDepth = -1

        # ── Zamknięcie bieżącego typu ─────────────────────────────────
        if CurrentType is not None and BraceDepth < TypeStartDepth:
            if not QuietMode:
                Print_TypeSummary(CurrentType)
            FoundTypes.append(CurrentType)
            CurrentType   = None
            TypeStartDepth = -1
            CurrentAccess  = "private"

        # ── Access specifier ──────────────────────────────────────────
        AccessMatch = RE_ACCESS_SPEC.match(Line)
        if AccessMatch:
            CurrentAccess = AccessMatch.group(1)
            PendingPropertyMacro = False  # makro musi być bezpośrednio nad polem
            continue

        # ── Oczekiwanie na deklarację TYPU ───────────────────────────
        if PendingTypeMacro is not None:
            DeclMatch = RE_CLASS_DECL.match(Line)
            if DeclMatch:
                TypeName  = DeclMatch.group(1)
                BasesStr  = DeclMatch.group(2)
                Bases     = ParseBases(BasesStr)

                ClassKind = ClassType.CLASS
                if PendingTypeMacro == "STRUCT":
                    ClassKind = ClassType.STRUCT
                elif PendingTypeMacro == "INTERFACE":
                    ClassKind = ClassType.INTERFACE

                CurrentType = TypeInfo(
                    Name             = TypeName,
                    Type             = ClassKind,
                    FilePath         = FilePath,
                    Bases            = Bases,
                    Project          = ProjectName,
                    ReflectionParams = PendingTypeMacroParams,
                    Properties       = [],
                    UuidProperty     = None,
                )
                # Głębokość przy otwierającym '{' – szukamy go w tej lub kolejnych liniach
                # Często '{' jest na tej samej linii co deklaracja
                if "{" in Line:
                    TypeStartDepth = BraceDepth
                else:
                    # '{' będzie w następnej linii – ustawimy po kolejnym kroku liczenia
                    TypeStartDepth = BraceDepth + 1

                # Dla struktury domyślny access to public
                CurrentAccess = "public" if ClassKind == ClassType.STRUCT else "private"

                if not QuietMode:
                    print(f"  [FOUND] {ClassKind.name}: {TypeName} -> {Bases}")

            PendingTypeMacro       = None
            PendingTypeMacroParams = []
            continue

        # ── Oczekiwanie na deklarację POLA ───────────────────────────
        if PendingPropertyMacro:
            FieldMatch = RE_FIELD_DECL.match(StripLineComment(Line))
            if FieldMatch and CurrentType is not None:
                FieldType = FieldMatch.group(1).strip()
                FieldName = FieldMatch.group(2).strip()
                NewProp = PropertyInfo(
                    Name         = FieldName,
                    Type         = FieldType,
                    Params       = PendingPropertyParams,
                    UuidForClass = "",
                    GetterName   = GetPyParamValue(PendingPropertyParams, "Getter") or "",
                    SetterName   = GetPyParamValue(PendingPropertyParams, "Setter") or "",
                )
                CurrentType.Properties.append(NewProp)
                if not QuietMode:
                    print(f"      [PROP] {FieldType} {FieldName}  params={PendingPropertyParams}  access={CurrentAccess}")
            else:
                # PLU_PROPERTY bez rozpoznanej deklaracji pola pod spodem.
                # Nigdy nie chowaj tego po cichu — pole wypadłoby z refleksji bez śladu.
                print(_c("33", f"  [WARN] {FilePath}:{LineIdx + 1}: PLU_PROPERTY with no "
                               f"recognized field declaration, property SKIPPED -> {Line!r}"))
            PendingPropertyMacro  = False
            PendingPropertyParams = []
            continue

        # ── Wykrycie makra PLU_CLASS ──────────────────────────────────
        # ── Zbieranie wartości enuma ──────────────────────────────────
        if CurrentEnum is not None:
            # PLU_ENUM_VALUE (opcjonalne makro nad wartością)
            MEV = RE_PLU_ENUM_VALUE.search(Line)
            if MEV:
                PendingEnumValueMacro  = True
                PendingEnumValueParams = ParseMacroParams(MEV.group(1))
                continue

            # Usuń komentarze inline i znaki klamrowe
            EnumLine = re.sub(r"//.*$", "", Line).strip()
            EnumLine = EnumLine.strip("{}")
            if not EnumLine:
                continue

            # Rozbij po przecinkach – obsługuje "A, B, C," i "Smth1 = 5, Smth2"
            MacroParams = PendingEnumValueParams if PendingEnumValueMacro else []
            for Token in EnumLine.split(","):
                # Usuń przypisanie wartości (np. Unknown = 0xFFFF)
                Token = re.sub(r"=\s*\S+", "", Token).strip()
                if Token and re.match(r"^\w+$", Token):
                    CurrentEnum.Values.append(EnumValueInfo(Name=Token, MacroParams=MacroParams))
                    if not QuietMode:
                        print(f"      [ENUM_VAL] {Token}  params={MacroParams}")
                    # PLU_ENUM_VALUE dotyczy tylko pierwszej wartości po makrze
                    MacroParams = []
            PendingEnumValueMacro  = False
            PendingEnumValueParams = []
            continue

        # ── Oczekiwanie na deklarację ENUMA ──────────────────────────
        if PendingEnumMacro:
            EDM = RE_ENUM_DECL.match(Line)
            if EDM:
                EnumName   = EDM.group(1)
                CurrentEnum = EnumInfo(
                    Name             = EnumName,
                    FilePath         = FilePath,
                    Project          = ProjectName,
                    ReflectionParams = PendingEnumMacroParams,
                    Namespace        = GetPyParamValue(PendingEnumMacroParams, "PyNamespace") or "",
                    BaseType         = EDM.group(2) or "",
                )
                EnumStartDepth = BraceDepth + (1 if "{" not in Line else 0)
                if not QuietMode:
                    print(f"  [FOUND] ENUM: {EnumName}")
            PendingEnumMacro       = False
            PendingEnumMacroParams = []
            continue

        # ── Wykrycie makra PLU_ENUM ───────────────────────────────────
        M = RE_PLU_ENUM.search(Line)
        if M:
            PendingEnumMacro       = True
            PendingEnumMacroParams = ParseMacroParams(M.group(1))
            continue

        M = RE_PLU_CLASS.search(Line)
        if M:
            PendingTypeMacro       = "CLASS"
            PendingTypeMacroParams = ParseMacroParams(M.group(1))
            continue

        # ── Wykrycie makra PLU_STRUCT ─────────────────────────────────
        M = RE_PLU_STRUCT.search(Line)
        if M:
            PendingTypeMacro       = "STRUCT"
            PendingTypeMacroParams = ParseMacroParams(M.group(1))
            continue

        # ── Wykrycie makra PLU_INTERFACE ──────────────────────────────
        M = RE_PLU_INTERFACE.search(Line)
        if M:
            PendingTypeMacro       = "INTERFACE"
            PendingTypeMacroParams = ParseMacroParams(M.group(1))
            continue

        # ── Wykrycie makra PLU_PROPERTY ───────────────────────────────
        M = RE_PLU_PROPERTY.search(Line)
        if M and CurrentType is not None:
            PendingPropertyMacro  = True
            PendingPropertyParams = ParseMacroParams(M.group(1))
            continue

        # ── Wykrycie makra PLU_FUNCTION ───────────────────────────────
        M = RE_PLU_FUNCTION.search(Line)
        if M:
            PendingFunctionMacro  = True
            PendingFunctionParams = ParseMacroParams(M.group(1))
            continue

        # ── Oczekiwanie na deklarację FUNKCJI ────────────────────────
        if PendingFunctionMacro:
            PendingFunctionMacro = False
            FM = RE_FUNC_DECL_FULL.match(Line)
            if FM:
                IsVirt     = bool(FM.group(1))
                ReturnType = FM.group(2).strip() + ("*" if FM.group(3) else "")
                # Usuń makra eksportu DLL i specyfikatory C++ (PLU_API, inline, static, itp.)
                ReturnType = re.sub(r"\bPLU_API\b|\b\w+_API\b|__declspec\s*\([^)]*\)|__cdecl|__stdcall|__fastcall|\binline\b|\bstatic\b|\bextern\b|\bexplicit\b|\bconstexpr\b|\bconsteval\b|\bconstinit\b|\bfriend\b", "", ReturnType).strip()
                FuncName   = FM.group(4).strip()
                RawParams  = FM.group(5).strip()
                IsConst    = bool(FM.group(6))
                Params     = ParseFunctionParams(RawParams)
                FuncInfo   = FunctionInfo(
                    Name        = FuncName,
                    ReturnType  = ReturnType,
                    Params      = Params,
                    MacroParams = PendingFunctionParams,
                    IsVirtual   = IsVirt,
                    IsConst     = IsConst,
                )
                if CurrentType is not None:
                    CurrentType.Functions.append(FuncInfo)
                    if not QuietMode:
                        print(f"      [FUNC] {ReturnType} {FuncName}({RawParams})  params={PendingFunctionParams}")
                else:
                    # Funkcja globalna
                    GlobalFunctions.append(GlobalFunctionInfo(
                        Name        = FuncName,
                        ReturnType  = ReturnType,
                        Params      = Params,
                        MacroParams = PendingFunctionParams,
                        FilePath    = FilePath,
                    ))
                    if not QuietMode:
                        print(f"  [GLOBAL FUNC] {ReturnType} {FuncName}({RawParams})  params={PendingFunctionParams}")
            PendingFunctionParams = []
            continue

    # Koniec pliku – jeśli typ nie został zamknięty (brak '}') dodajemy go
    if CurrentType is not None:
        if not QuietMode:
            Print_TypeSummary(CurrentType)
        FoundTypes.append(CurrentType)

    if CurrentEnum is not None:
        if not QuietMode:
            print(f"  [ENUM] {CurrentEnum.Name}  values={[V.Name for V in CurrentEnum.Values]}")
        FoundEnums.append(CurrentEnum)

    return FoundTypes, GlobalFunctions, FoundEnums


def Print_TypeSummary(T: TypeInfo):
    print(f"    -> {T.Type.name} '{T.Name}'  bases={T.Bases}  props={[P.Name for P in T.Properties]}")


# ─────────────────────────────────────────────
#  Walidacja parametrów (params_check)
# ─────────────────────────────────────────────

def ParamsCheck(Data: List[TypeInfo]):
    for Cls in Data:
        for Prop in Cls.Properties:
            if Prop.Type.endswith("*") or Prop.Type.endswith("&"):
                print(f"Warning: {Cls.Name}::{Prop.Name} jest wskaźnikiem/referencją ({Prop.Type}).")
            if any(C in Prop.Type for C in ("std::vector", "std::map", "std::unordered_map")):
                print(f"Warning: {Cls.Name}::{Prop.Name} używa kontenera STL. Użyj PluSTL.")
            if any(C in Prop.Type for C in ("std::shared_ptr", "std::unique_ptr")):
                print(f"Warning: {Cls.Name}::{Prop.Name} używa smart-pointer STL. Użyj PluSTL.")
            if "glm::" in Prop.Type:
                print(f"Warning: {Cls.Name}::{Prop.Name} używa typu GLM. Użyj PluTypes.h.")
            if Prop.Type == "auto":
                print(f"Error: {Cls.Name}::{Prop.Name} ma typ 'auto' – refleksja niemożliwa.")

            for Param in Prop.Params:
                if Param.startswith("UuidFor="):
                    UuidForClass = Param.split("=", 1)[1]
                    if Prop.Type not in ("PluUUID", "Plu::PluUUID"):
                        print(f"Error: {Cls.Name}::{Prop.Name} – UuidFor={UuidForClass} ale typ to '{Prop.Type}', oczekiwano 'PluUUID'.")
                    else:
                        print(f"Info: {Cls.Name}::{Prop.Name} – prawidłowy UUID prop dla '{UuidForClass}'.")
                        Prop.UuidForClass = UuidForClass

            # Walidacja Getter=/Setter= – oba albo żaden (lub tylko jeden z nich)
            # HasGetter = bool(Prop.GetterName)
            # HasSetter = bool(Prop.SetterName)
            # if HasGetter and not HasSetter:
            #     print(f"Error: {Cls.Name}::{Prop.Name} – podano Getter= bez Setter=. Prop zostanie pominięty.")
            #     Prop.GetterName = ""
            # elif HasSetter and not HasGetter:
            #     print(f"Error: {Cls.Name}::{Prop.Name} – podano Setter= bez Getter=. Prop zostanie pominięty.")
            #     Prop.SetterName = ""

        for Param in Cls.ReflectionParams:
            if Param.startswith("UUID="):
                UuidPropName = Param.split("=", 1)[1]
                MatchingProp = next((P for P in Cls.Properties if P.Name == UuidPropName), None)
                if MatchingProp is None:
                    print(f"Error: {Cls.Name} – UUID={UuidPropName} – brak takiego pola.")
                elif MatchingProp.Type not in ("PluUUID", "Plu::PluUUID"):
                    print(f"Error: {Cls.Name} – UUID prop '{UuidPropName}' ma typ '{MatchingProp.Type}', oczekiwano 'PluUUID'.")
                else:
                    print(f"Info: {Cls.Name} – prawidłowe UUID pole '{UuidPropName}'.")
                    Cls.UuidProperty = MatchingProp


# ─────────────────────────────────────────────
#  Platforma i filtrowanie folderów
# ─────────────────────────────────────────────

# Mapowanie nazwa folderu Platforms/<X> → prefiks sys.platform
PlatformFolderMap: dict = {
    "Windows": "win",
    "Linux":   "linux",
    "MacOS":   "darwin",
    "IOS":     "ios",
}

# Makra których obecność w pliku .h kwalifikuje go do przetwarzania przez generator.
# Wystarczy że jedno z nich wystąpi w pliku – dodaj tutaj nowe makra kiedy silnik je dostanie.
REFLECTION_TRIGGER_MACROS: List[str] = [
    "PLU_CLASS",
    "PLU_STRUCT",
    "PLU_INTERFACE",
    "PLU_FUNCTION",   # pliki z samymi funkcjami globalnymi
    "PLU_ENUM",
]


def IsExcludedPlatformDir(DirPath: str) -> bool:
    """
    Zwraca True jeśli folder jest podfolderem Platforms/<Platforma>
    która nie pasuje do bieżącej platformy.
    Działa zarówno dla ścieżek z separatorem / jak i \\.
    """
    NormDir = DirPath.replace("\\", "/")
    for FolderName, SysPlatformPrefix in PlatformFolderMap.items():
        if f"/Platforms/{FolderName}" in NormDir:
            return not sys.platform.startswith(SysPlatformPrefix)
    return False


# ─────────────────────────────────────────────
#  Skanowanie projektu
# ─────────────────────────────────────────────

def ScanProject() -> List[Path]:
    print(f"Scanning project at {ProjectToScanDir} ...")
    print(f"Platform: {sys.platform}")
    Found: List[Path] = []
    for DirPath, _, Files in os.walk(ProjectToScanDir):
        if any(Skip in DirPath for Skip in ("cmake-build", ".git", "ReflectionCache")):
            continue
        if IsExcludedPlatformDir(DirPath):
            if not QuietMode:
                print(f"  Skipping dir {DirPath} (other platform)")
            continue
        for FileName in Files:
            if not FileName.endswith(".h") or FileName.endswith("generated.h"):
                continue
            FullPath = Path(DirPath, FileName)
            try:
                Content = FullPath.read_text(errors="ignore")
            except OSError:
                continue
            if not any(Macro in Content for Macro in REFLECTION_TRIGGER_MACROS):
                continue
            Found.append(FullPath)
    print(f"Found {len(Found)} file(s) to process.")
    return Found


# ─────────────────────────────────────────────
#  Generowanie plików refleksji
# ─────────────────────────────────────────────

def IsStructAsset(Cls: StructInfoInternal, AllClasses: List[StructInfoInternal]) -> bool:
    for Base in Cls.Bases:
        for C in AllClasses:
            if C.Name == Base:
                if C.Name == "IAssetData":
                    return True
                return IsStructAsset(C, AllClasses)
    return False


def IsTypeDerivedFrom(BaseName: str, Derived: TypeInfo, AllTypes: List[TypeInfo]) -> bool:
    for B in Derived.Bases:
        if B == BaseName:
            return True
        for T in AllTypes:
            if T.Name == B and IsTypeDerivedFrom(BaseName, T, AllTypes):
                return True
    return False


def _IsPodStruct(Cls: TypeInfo) -> bool:
    """PLU_STRUCT(NoVirtualClass) — zostaje POD-em, GetClass() niewirtualne."""
    return "NoVirtualClass" in Cls.ReflectionParams


def StructHasVirtualStructBase(Cls: TypeInfo, AllTypes: List[TypeInfo]) -> bool:
    """True jeśli któraś bezpośrednia baza to zreflektowany struct, który NIE jest
    POD-em — wtedy on wprowadza wirtualne GetClass(), a nasze jest 'override'."""
    for B in Cls.Bases:
        for T in AllTypes:
            if T.Name == B and T.Type == ClassType.STRUCT and not _IsPodStruct(T):
                return True
    return False


def StructHasVirtualStructDerived(Cls: TypeInfo, AllTypes: List[TypeInfo]) -> bool:
    """True jeśli jakiś zreflektowany, nie-POD struct dziedziczy bezpośrednio po Cls
    — wtedy Cls musi wprowadzić wirtualne GetClass() dla dyspozycji przez wskaźnik bazy."""
    for T in AllTypes:
        if T.Type == ClassType.STRUCT and not _IsPodStruct(T) and Cls.Name in T.Bases:
            return True
    return False


def StructGetClassIsVirtual(Cls: TypeInfo, AllTypes: List[TypeInfo]) -> bool:
    """Czy struct powinien mieć wirtualne GetClass().

    Struct dostaje wirtualne GetClass() gdy uczestniczy w hierarchii zreflektowanych
    structów: dziedziczy po nie-POD structcie (→ override) albo jest bazą dla nie-POD
    structa (→ wprowadza virtual). PLU_STRUCT(NoVirtualClass) zawsze zostaje POD-em
    bez vtable (np. wierzchołki wysyłane surowo na GPU) i przerywa propagację —
    krawędź do/z niego jest ignorowana.
    """
    if Cls.Type != ClassType.STRUCT or _IsPodStruct(Cls):
        return False
    return (StructHasVirtualStructBase(Cls, AllTypes)
            or StructHasVirtualStructDerived(Cls, AllTypes))


# ─────────────────────────────────────────────
#  Pomocniki dla pybind11
# ─────────────────────────────────────────────

# Mapowanie prostych typów C++ → Python dla stubów .pyi
CPP_TO_PY_TYPE: dict = {
    # Typy całkowite
    "int":     "int",  "int8":    "int",  "int16":  "int",  "int32":  "int",  "int64":  "int",
    "uint8":   "int",  "uint16":  "int",  "uint32": "int",  "uint64": "int",
    "size_t":  "int",  "ptrdiff_t": "int",
    # Aliasy silnika (using Int32 = std::int32_t; itp.)
    "Int8":    "int",  "Int16":   "int",  "Int32":  "int",  "Int64":  "int",
    "UInt8":   "int",  "UInt16":  "int",  "UInt32": "int",  "UInt64": "int",
    # Zmiennoprzecinkowe
    "float":   "float", "double": "float",
    # Logiczne
    "bool":    "bool",
    # Stringi – wszystkie warianty BasicString/String
    "String":        "str",  "StringW":       "str",
    "BasicString":   "str",
    "std::string":   "str",  "std::wstring":  "str",
    # UUID
    "PluUUID": "str",
    # Void
    "void":    "None",
    # pybind11 typy (np. parametry RegisterPluClass)
    "pybind11::type": "type",  "py::type": "type",
    "pybind11::object": "object", "py::object": "object",
    # glm – klasy pybind11 rejestrowane przez RegisterMathTypes (PluEngine/Scripting/PythonMath.h)
    "Vec2":       "Vec2",        "glm::vec2":  "Vec2",
    "Vec3":       "Vec3",        "glm::vec3":  "Vec3",
    "Vec4":       "Vec4",        "glm::vec4":  "Vec4",
    "IVec2":      "IVec2",       "glm::ivec2": "IVec2",
    "IVec3":      "IVec3",       "glm::ivec3": "IVec3",
    "IVec4":      "IVec4",       "glm::ivec4": "IVec4",
    "Quaternion": "Quaternion",  "glm::quat":  "Quaternion",  "Quat": "Quaternion",
    "Matrix4":    "Matrix4",     "glm::mat4":  "Matrix4",
}

# Typy matematyczne wystawione do Pythona przez RegisterMathTypes – zarejestrowane w module przed
# enumami i klasami, więc mogą być domyślnymi wartościami argumentów.
MATH_TYPE_NAMES: set = {"Vec2", "Vec3", "Vec4", "IVec2", "IVec3", "IVec4", "Quaternion", "Matrix4"}

# (nazwa, typ składowej, składowe, czy zmiennoprzecinkowy) – musi odpowiadać RegisterMathTypes.
MATH_VECTOR_STUBS: list = [
    ("Vec2",  "float", ["x", "y"],           True),
    ("Vec3",  "float", ["x", "y", "z"],      True),
    ("Vec4",  "float", ["x", "y", "z", "w"], True),
    ("IVec2", "int",   ["x", "y"],           False),
    ("IVec3", "int",   ["x", "y", "z"],      False),
    ("IVec4", "int",   ["x", "y", "z", "w"], False),
]


def WriteMathStubs(P) -> None:
    """
    Dopisuje do .pyi stuby typów matematycznych. Bindingi są pisane ręcznie w
    LibEngine/src/PluEngine/Python/PythonMath.cpp – ten stub musi za nimi nadążać.
    """
    P.write("# Math types – hand-written bindings (PluEngine/Scripting/PythonMath.h).\n")
    P.write("# Tuples and lists convert implicitly wherever one of these is expected.\n\n")

    for Name, Comp, Components, IsFloat in MATH_VECTOR_STUBS:
        P.write(f"class {Name}:\n")
        for C in Components:
            P.write(f"    {C}: {Comp}\n")
        P.write(f"    @overload\n    def __init__(self) -> None: ...\n")
        P.write(f"    @overload\n    def __init__(self, scalar: {Comp}) -> None: ...\n")
        P.write(f"    @overload\n    def __init__(self, values: Sequence[{Comp}]) -> None: ...\n")
        P.write(f"    @overload\n    def __init__(self, "
                f"{', '.join(f'{C}: {Comp}' for C in Components)}) -> None: ...\n")
        P.write(f"    def __len__(self) -> int: ...\n")
        P.write(f"    def __getitem__(self, index: int) -> {Comp}: ...\n")
        P.write(f"    def __setitem__(self, index: int, value: {Comp}) -> None: ...\n")
        P.write(f"    def __iter__(self) -> Iterator[{Comp}]: ...\n")
        P.write(f"    def __neg__(self) -> {Name}: ...\n")
        P.write(f"    def __add__(self, other: {Name}) -> {Name}: ...\n")
        P.write(f"    def __sub__(self, other: {Name}) -> {Name}: ...\n")
        P.write(f"    def __mul__(self, other: {Name} | {Comp}) -> {Name}: ...\n")
        P.write(f"    def __rmul__(self, other: {Comp}) -> {Name}: ...\n")
        P.write(f"    def __iadd__(self, other: {Name}) -> {Name}: ...\n")
        P.write(f"    def __isub__(self, other: {Name}) -> {Name}: ...\n")
        P.write(f"    def __imul__(self, other: {Comp}) -> {Name}: ...\n")
        if IsFloat:
            P.write(f"    def __truediv__(self, other: {Name} | {Comp}) -> {Name}: ...\n")
            P.write(f"    def Length(self) -> float: ...\n")
            P.write(f"    def LengthSquared(self) -> float: ...\n")
            P.write(f"    def Normalized(self) -> {Name}: ...\n")
            P.write(f"    def Dot(self, other: {Name}) -> float: ...\n")
            P.write(f"    def Distance(self, other: {Name}) -> float: ...\n")
            P.write(f"    def Lerp(self, target: {Name}, alpha: float) -> {Name}: ...\n")
        if Name == "Vec3":
            P.write(f"    def Cross(self, other: Vec3) -> Vec3: ...\n")
        P.write("\n")

    P.write("class Quaternion:\n")
    P.write("    w: float\n    x: float\n    y: float\n    z: float\n")
    P.write("    @overload\n    def __init__(self) -> None: ...\n")
    P.write("    @overload\n    def __init__(self, w: float, x: float, y: float, z: float) -> None: ...\n")
    P.write("    @overload\n    def __init__(self, values: Sequence[float]) -> None: ...\n")
    P.write("    @overload\n    def __mul__(self, other: Quaternion) -> Quaternion: ...\n")
    P.write("    @overload\n    def __mul__(self, other: Vec3) -> Vec3: ...\n")
    P.write("    def Length(self) -> float: ...\n")
    P.write("    def Normalized(self) -> Quaternion: ...\n")
    P.write("    def Inverse(self) -> Quaternion: ...\n")
    P.write("    def Slerp(self, target: Quaternion, alpha: float) -> Quaternion: ...\n")
    P.write("    def ToEulerDegrees(self) -> Vec3: ...\n")
    P.write("    @staticmethod\n    def FromEulerDegrees(degrees: Vec3) -> Quaternion: ...\n\n")

    P.write("class Matrix4:\n")
    P.write("    @overload\n    def __init__(self) -> None: ...\n")
    P.write("    @overload\n    def __init__(self, diagonal: float) -> None: ...\n")
    P.write("    def __len__(self) -> int: ...\n")
    P.write("    def __getitem__(self, column: int) -> Vec4: ...\n")
    P.write("    def __setitem__(self, column: int, value: Vec4) -> None: ...\n")
    P.write("    @overload\n    def __mul__(self, other: Matrix4) -> Matrix4: ...\n")
    P.write("    @overload\n    def __mul__(self, other: Vec4) -> Vec4: ...\n")
    P.write("    @overload\n    def __mul__(self, other: float) -> Matrix4: ...\n")
    P.write("    def Inverse(self) -> Matrix4: ...\n")
    P.write("    def Transposed(self) -> Matrix4: ...\n")
    P.write("    @staticmethod\n    def Identity() -> Matrix4: ...\n\n")

# Regex do rozpoznawania szablonów kontenerów (kolejność ma znaczenie – bardziej szczegółowe pierwsze)
_RE_DYNAMIC_ARRAY   = re.compile(r"^DynamicArray\s*<(.+)>$")
# HashMap is the current name; GameHashMap is the transitional alias still accepted here.
_RE_GAME_HASH_MAP   = re.compile(r"^(?:Game)?HashMap\s*<(.+),\s*(.+?)(?:,.*)?>\s*$")  # K, V[, hasher, alloc]
_RE_BASIC_STRING    = re.compile(r"^BasicString\s*<.*>$")
_RE_USE_POINTER     = re.compile(r"^TUsePointer\s*<(.+)>$")
_RE_OWNING_POINTER  = re.compile(r"^TOwningPointer\s*<(.+)>$")
_RE_STD_FUNCTION    = re.compile(r"^std::function\s*<(.+)>$")

def _StripOuterTemplate(s: str) -> Optional[str]:
    """Jeśli string to Foo<X>, zwraca X. Uwzględnia zagnieżdżone nawiasy."""
    s = s.strip()
    Open = s.find("<")
    if Open == -1 or not s.endswith(">"):
        return None
    Depth = 0
    for I, C in enumerate(s):
        if C == "<": Depth += 1
        elif C == ">": Depth -= 1
    return s[Open + 1:-1].strip() if Depth == 0 else None

def _SplitTemplateArgs(s: str) -> List[str]:
    """Dzieli argumenty szablonu po najwyższym poziomie przecinków."""
    Args, Depth, Cur = [], 0, []
    for C in s:
        if C == "<": Depth += 1
        elif C == ">": Depth -= 1
        if C == "," and Depth == 0:
            Args.append("".join(Cur).strip()); Cur = []
        else:
            Cur.append(C)
    if Cur:
        Args.append("".join(Cur).strip())
    return Args

def CppTypeToPy(CppType: str) -> str:
    """Konwertuje typ C++ → Python do stubów .pyi. Obsługuje szablony silnika."""
    Raw   = CppType.strip()
    # Usuń namespace Plu::
    Clean = re.sub(r"\bPlu::", "", Raw).strip()

    # ── Strippuj const, &, * (np. "const Vec3&" → "Vec3") ────────────
    Clean = re.sub(r"\bconst\b", "", Clean).replace("&", "").replace("*", "")
    Clean = " ".join(Clean.split())  # normalizuj spacje

    # ── Kontenery silnika ──────────────────────────────────────────────

    # TClassPointer<T> → Type[T]  (używane jako PLU_PROPERTY)
    M = re.match(r"^(?:Plu::)?TClassPointer\s*<(.+)>$", Clean)
    if M:
        return f"Type[{CppTypeToPy(M.group(1))}]"

    # DynamicArray<T> → List[T]
    M = _RE_DYNAMIC_ARRAY.match(Clean)
    if M:
        return f"List[{CppTypeToPy(M.group(1))}]"

    # HashMap<K, V, ...> (or the GameHashMap alias) → Dict[K, V]
    Inner = _StripOuterTemplate(Clean)
    if (Clean.startswith("HashMap") or Clean.startswith("GameHashMap")) and Inner:
        Parts = _SplitTemplateArgs(Inner)
        if len(Parts) >= 2:
            return f"Dict[{CppTypeToPy(Parts[0])}, {CppTypeToPy(Parts[1])}]"

    # BasicString<char/wchar_t, ...> → str
    if _RE_BASIC_STRING.match(Clean):
        return "str"

    # TUsePointer<X> / TOwningPointer<X> → Optional[X]
    M = _RE_USE_POINTER.match(Clean) or _RE_OWNING_POINTER.match(Clean)
    if M:
        return f"Optional[{CppTypeToPy(M.group(1))}]"

    # std::function<Ret(Args...)> → Callable[[Args], Ret]
    # Outer strip zachowuje * wewnątrz Args (np. void(void*) nie traci gwiazdki)
    CleanOuter = re.sub(r"\bPlu::", "", _StripOuterQualifiers(Raw)).strip()
    M = _RE_STD_FUNCTION.match(CleanOuter)
    if M:
        Inner = M.group(1).strip()  # np. "void(void*)" lub "void(int, float)"
        FnMatch = re.match(r"^(.+?)\(([^)]*)\)$", Inner)
        if FnMatch:
            RetPy  = CppTypeToPy(FnMatch.group(1).strip())
            ArgStr = FnMatch.group(2).strip()
            if ArgStr and ArgStr != "void":
                ArgsPy = ", ".join(CppTypeToPy(A.strip()) for A in _SplitTemplateArgs(ArgStr))
            else:
                ArgsPy = ""
            return f"Callable[[{ArgsPy}], {RetPy}]"
        return "Callable"

    return CPP_TO_PY_TYPE.get(Clean, Clean)


def HasPyParam(Params: List[str], ParamName: str) -> bool:
    return ParamName in Params


def IsNoReflection(Cls: TypeInfo) -> bool:
    """Zwraca True jeśli klasa ma NoReflection – tylko bindingi, bez .generated.h/.cpp i TypeRegistry."""
    return HasPyParam(Cls.ReflectionParams, "NoReflection")


def GetPyParamValue(Params: List[str], Key: str) -> Optional[str]:
    for P in Params:
        if P.startswith(f"{Key}="):
            return P.split("=", 1)[1]
    return None


def IsPyExported(TypeOrProp) -> bool:
    """Zwraca True jeśli typ/pole ma parametr PyExport."""
    return HasPyParam(TypeOrProp.Params if isinstance(TypeOrProp, PropertyInfo)
                      else TypeOrProp.ReflectionParams, "PyExport")


def GetPyName(Cls: TypeInfo) -> str:
    """Zwraca nazwę Pythonową klasy (PyName=X lub oryginalna)."""
    Name = GetPyParamValue(Cls.ReflectionParams, "PyName")
    return Name if Name else Cls.Name


def GetPyDoc(Params: List[str]) -> str:
    """Zwraca napis PyDoc= jeśli podany, inaczej pusty string."""
    Val = GetPyParamValue(Params, "PyDoc")
    return Val.replace("_", " ") if Val else ""


_RE_CLASS_POINTER = re.compile(r"(?:Plu::)?TClassPointer\s*<(.+)>")

def _HasClassPointer(Params: List[ParamInfo]) -> bool:
    """Zwraca True jeśli którykolwiek parametr to TClassPointer<T> (obsługuje const T&, Plu::)."""
    for P in Params:
        Clean = re.sub(r"\bPlu::", "", _StripQualifiers(P.Type)).strip()
        if _RE_CLASS_POINTER.match(Clean):
            return True
    return False

def _NeedsLambda(Params: List[ParamInfo], ReturnType: str, AllClasses: List = []) -> bool:
    """Zwraca True jeśli funkcja wymaga lambdy (TClassPointer, TUsePointer asset param/return, std::function)."""
    if _HasClassPointer(Params):
        return True
    # Return type: TUsePointer/TOwningPointer → .GetRaw()
    CleanRet = re.sub(r"\bPlu::", "", _StripQualifiers(ReturnType)).strip()
    if _RE_USE_POINTER.match(CleanRet) or _RE_OWNING_POINTER.match(CleanRet):
        return True
    for P in Params:
        CleanP = re.sub(r"\bPlu::", "", _StripQualifiers(P.Type)).strip()
        # TUsePointer<T> gdzie T dziedziczy po IAssetData → GetAssetUserAsRaw
        MUP = _RE_USE_POINTER.match(CleanP) or _RE_OWNING_POINTER.match(CleanP)
        if MUP and AllClasses:
            InnerT = MUP.group(1).strip()
            InnerTClean = re.sub(r"\bPlu::", "", InnerT).strip()
            InnerTypeInfo = next((C for C in AllClasses if C.Name == InnerTClean), None)
            if InnerTypeInfo and IsTypeDerivedFrom("IAssetData", InnerTypeInfo, AllClasses):
                return True
        # TUsePointer<T> gdzie T to EngineObject → T* + UserFromPython
        if _ObjectUsePointerInner(P.Type, AllClasses):
            return True
        # std::function → py::function (używamy outer strip żeby zachować * wewnątrz szablonu)
        CleanPOuter = re.sub(r"\bPlu::", "", _StripOuterQualifiers(P.Type)).strip()
        if _RE_STD_FUNCTION.match(CleanPOuter):
            return True
    return False

def _AssetUsePointerInner(CppType: str, AllClasses: List = []):
    """For TUsePointer<T> where T derives from IAssetData returns T's TypeInfo, otherwise None.
    Such fields cannot use def_readwrite: pybind11 has no caster for TUsePointer, and Python only
    ever holds assets as raw IAssetData* (e.g. from GetAssetByUUID)."""
    Clean = re.sub(r"\bPlu::", "", _StripQualifiers(CppType)).strip()
    M = _RE_USE_POINTER.match(Clean)
    if not M or not AllClasses:
        return None
    InnerT = re.sub(r"\bPlu::", "", M.group(1)).strip()
    InnerTypeInfo = next((C for C in AllClasses if C.Name == InnerT), None)
    if InnerTypeInfo and IsTypeDerivedFrom("IAssetData", InnerTypeInfo, AllClasses):
        return InnerTypeInfo
    return None


def _ObjectUsePointerInner(CppType: str, AllClasses: List = []):
    """For TUsePointer<T> where T is an EngineObject (but not an asset) returns T's TypeInfo, otherwise None.
    Python holds such objects as raw T*: bindings take T* and rebuild the pointer with UserFromPython,
    trampolines hand Python .GetRaw()."""
    Clean = re.sub(r"\bPlu::", "", _StripQualifiers(CppType)).strip()
    M = _RE_USE_POINTER.match(Clean)
    if not M or not AllClasses:
        return None
    InnerT = re.sub(r"\bPlu::", "", M.group(1)).strip()
    InnerTypeInfo = next((C for C in AllClasses if C.Name == InnerT), None)
    if InnerTypeInfo is None or IsTypeDerivedFrom("IAssetData", InnerTypeInfo, AllClasses):
        return None
    if InnerT == "EngineObject" or IsTypeDerivedFrom("EngineObject", InnerTypeInfo, AllClasses):
        return InnerTypeInfo
    return None


def _ReturnsPointer(ReturnType: str) -> bool:
    """Zwraca True jeśli typ zwracany jest surowym wskaźnikiem (nie TUsePointer/TOwningPointer)."""
    RT = ReturnType.strip()
    # Surowy wskaźnik: zawiera * ale nie jest smart pointerem
    if "*" not in RT:
        return False
    Clean = re.sub(r"\bPlu::", "", RT).strip()
    Clean = re.sub(r"\bconst\b", "", Clean).strip()
    if _RE_USE_POINTER.match(Clean) or _RE_OWNING_POINTER.match(Clean):
        return False  # smart pointer – obsłużony osobno
    return True


def _StripQualifiers(CppType: str) -> str:
    """Usuwa const, &, * z typu C++ żeby uzyskać czysty identyfikator."""
    return re.sub(r"\bconst\b", "", CppType).replace("&", "").replace("*", "").strip()

def _StripOuterQualifiers(CppType: str) -> str:
    """Usuwa const/&/* tylko z zewnętrznego poziomu (nie wewnątrz <> szablonów)."""
    T = CppType.strip()
    T = re.sub(r"^\s*const\s+", "", T).strip()
    T = re.sub(r"\s*\bconst\b\s*$", "", T).strip()
    T = re.sub(r"[&*\s]+$", "", T).strip()
    return T

def _BuildParamList(Params: List[ParamInfo], SelfDecl: str, AllClasses: List = []) -> tuple:
    """
    Buduje listy parametrów lambdy, wywołań i rozpakowań.
    Obsługuje: TClassPointer<T> → py::object,
               TUsePointer<T> gdzie T:IAssetData → T* + GetAssetUserAsRaw,
               TUsePointer<T> gdzie T:EngineObject → T* + UserFromPython, reszta bez zmian.
    Typy glm są zwykłymi klasami modułu (RegisterMathTypes) – przechodzą bez konwersji.
    """
    LambdaParams: List[str] = ([SelfDecl] if SelfDecl else [])
    BindingCalls: List[str] = []
    Unpacks:      List[str] = []

    for I, P in enumerate(Params):
        Raw     = P.Type.strip()
        Clean   = _StripQualifiers(Raw)
        # Usuń też namespace Plu:: żeby regex matchował
        CleanNoNs = re.sub(r"\bPlu::", "", Clean).strip()
        ArgName = P.Name if P.Name else f"arg{I}"

        # TUsePointer<T> gdzie T dziedziczy po IAssetData → parametr jako T*, owija GetAssetUserAsRaw
        MUP = _RE_USE_POINTER.match(CleanNoNs) or _RE_OWNING_POINTER.match(CleanNoNs)
        if MUP:
            InnerT = MUP.group(1).strip()
            InnerTClean = re.sub(r"\bPlu::", "", InnerT).strip()
            # Sprawdź czy InnerT dziedziczy po IAssetData
            InnerTypeInfo = next((C for C in AllClasses if C.Name == InnerTClean), None)
            IsAsset = InnerTypeInfo is not None and IsTypeDerivedFrom("IAssetData", InnerTypeInfo, AllClasses)
        else:
            IsAsset = False

        # std::function<Ret(Args...)> → py::function + lambda wrapper
        # Outer strip zachowuje * wewnątrz Args (np. void(void*) nie staje się void(void))
        RawNoNs = re.sub(r"\bPlu::", "", _StripOuterQualifiers(Raw)).strip()
        if MSF := _RE_STD_FUNCTION.match(RawNoNs):
            Inner    = MSF.group(1).strip()          # np. "void()" lub "void(void*)"
            FnMatch  = re.match(r"^(.+?)\(([^)]*)\)$", Inner)
            if FnMatch:
                FnRet    = FnMatch.group(1).strip()  # "void" / "bool" itp.
                FnArgStr = FnMatch.group(2).strip()  # "" / "void*" / "int, float"
                # Buduj argumenty wywołania callbacku wewnątrz lambdy C++
                if FnArgStr and FnArgStr != "void":
                    FnArgTypes = _SplitTemplateArgs(FnArgStr)
                    FnArgDecls = ", ".join(f"{T} _a{i}" for i, T in enumerate(FnArgTypes))
                    FnArgCall  = ", ".join(f"_a{i}" for i in range(len(FnArgTypes)))
                else:
                    FnArgDecls = ""
                    FnArgCall  = ""
                RetKw    = "" if FnRet == "void" else f"({FnRet})"
                Wrapper  = f"[{ArgName}]({FnArgDecls}) {{ {ArgName}({FnArgCall}); }}"
                if FnRet != "void":
                    Wrapper = f"[{ArgName}]({FnArgDecls}) -> {FnRet} {{ return {RetKw}{ArgName}({FnArgCall}); }}"
            else:
                Wrapper = f"[{ArgName}]() {{ {ArgName}(); }}"
            LambdaParams.append(f"py::function {ArgName}")
            BindingCalls.append(Wrapper)
            continue
        MCP = _RE_CLASS_POINTER.match(CleanNoNs)
        ObjectInner = _ObjectUsePointerInner(Raw, AllClasses)
        if ObjectInner:
            # TUsePointer<T:EngineObject> → Python passes T*, the pointer is rebuilt from the handle
            LambdaParams.append(f"{ObjectInner.Name}* {ArgName}")
            BindingCalls.append(f"UserFromPython({ArgName})")
        elif MUP and IsAsset:
            # Python przekazuje IAssetData* (nie T*) – nie trzeba .__class__ po stronie Pythona
            LambdaParams.append(f"IAssetData* {ArgName}")
            BindingCalls.append(f"GetAssetUserAsRaw({ArgName})")
        elif MCP:
            # TClassPointer<T> → py::object zawierający klasę Pythona
            # Wyciągamy TypeInfo* przez TypeRegistry używając __name__
            LambdaParams.append(f"py::object {ArgName}_pytype")
            Unpacks.append(f"std::string {ArgName}_name = pybind11::str({ArgName}_pytype.attr(\"__name__\"));")
            Unpacks.append(f"TypeInfo* {ArgName} = TypeRegistry::GetInstance()->GetTypeOfName({ArgName}_name.c_str());")
            BindingCalls.append(ArgName)
        else:
            LambdaParams.append(f"{Raw} {ArgName}")
            BindingCalls.append(ArgName)

    return LambdaParams, BindingCalls, Unpacks


def _BuildReturnLine(ReturnType: str, CallExpr: str) -> tuple:
    """
    Buduje linię return z konwersją TUsePointer/TOwningPointer → raw ptr.
    Zwraca (line: str, needs_reference: bool).
    needs_reference=True → dodaj py::return_value_policy::reference żeby C++ rządził lifetime.
    """
    CleanRet   = _StripQualifiers(ReturnType)
    CleanRetNs = re.sub(r"\bPlu::", "", CleanRet).strip()
    if ReturnType.strip() == "void":
        return f"{CallExpr};", False
    elif _RE_USE_POINTER.match(CleanRetNs) or _RE_OWNING_POINTER.match(CleanRetNs):
        # Zwracamy surowy wskaźnik ale C++ nadal rządzi pamięcią – reference policy
        return f"return {CallExpr}.GetRaw();", True
    elif _ReturnsPointer(ReturnType):
        return f"return {CallExpr};", True
    else:
        return f"return {CallExpr};", False


def _BuildWrapperLambda(ClsName: str, FuncName: str, Params: List[ParamInfo],
                    ReturnType: str, IsConst: bool, AllClasses: List = []) -> tuple:
    """Generuje lambdę C++ opakowującą metodę. Zwraca (lambda_str, needs_reference)."""
    ConstRef = "const " if IsConst else ""
    SelfDecl = f"{ConstRef}{ClsName}& self"
    LambdaParams, BindingCalls, Unpacks = _BuildParamList(Params, SelfDecl, AllClasses)
    CallExpr             = f"self.{FuncName}({', '.join(BindingCalls)})"
    ReturnLine, NeedsRef = _BuildReturnLine(ReturnType, CallExpr)
    Body                 = (" ".join(Unpacks) + " " if Unpacks else "") + ReturnLine
    return f"[]({', '.join(LambdaParams)}) {{ {Body} }}", NeedsRef


def _BuildWrapperLambdaGlobal(FuncName: str, Params: List[ParamInfo], ReturnType: str, AllClasses: List = []) -> tuple:
    """Generuje lambdę C++ dla funkcji globalnej. Zwraca (lambda_str, needs_reference)."""
    LambdaParams, BindingCalls, Unpacks = _BuildParamList(Params, "", AllClasses)
    CallExpr             = f"{FuncName}({', '.join(BindingCalls)})"
    ReturnLine, NeedsRef = _BuildReturnLine(ReturnType, CallExpr)
    Body                 = (" ".join(Unpacks) + " " if Unpacks else "") + ReturnLine
    return f"[]({', '.join(LambdaParams)}) {{ {Body} }}", NeedsRef

# Literały, które zawsze da się skonwertować na obiekt Pythona w momencie .def()
_RE_LITERAL_DEFAULT = re.compile(
    r"^(?:-?\d+\.?\d*(?:[eE][-+]?\d+)?[fFuUlL]*|0[xX][0-9a-fA-F]+[uUlL]*"
    r"|true|false|nullptr|\"[^\"]*\"|'\\?.')$"
)
# Pusty kontener silnika, np. DynamicArray<GameObject*>{} – caster robi z niego pustą listę
_RE_EMPTY_CONTAINER_DEFAULT = re.compile(r"^(?:Plu::)?(?:DynamicArray|(?:Game)?HashMap|HashSet)\s*<.*>\s*(?:\{\s*\}|\(\s*\))$")
# Konstrukcja typu, np. RaycastDebugSettings() / Vec3{} – bezpieczna tylko dla typów już
# zarejestrowanych w module przed tym .def()
_RE_TYPE_CONSTRUCTION_DEFAULT = re.compile(r"^(?:Plu::)?(\w+)\s*(?:\{\s*\}|\(\s*\))$")
# Wartość enuma, np. CollisionResponse::Block
_RE_ENUM_VALUE_DEFAULT = re.compile(r"^(?:Plu::)?(\w+)\s*::\s*\w+$")


def _PyArgDefault(P: ParamInfo, RegisteredNames: Optional[set] = None) -> str:
    """
    Zwraca sufiks ' = <wartosc>' do py::arg albo pusty string.

    pybind11 konwertuje wartości domyślne od razu w .def(), więc emitujemy je tylko wtedy, gdy
    konwersja na pewno się uda: literały, puste kontenery PluSTL oraz konstrukcje/enumy typów już
    zarejestrowanych w module (w tym typów matematycznych z RegisterMathTypes). Parametry, które w
    lambdzie zmieniają typ (TClassPointer → py::object, std::function → py::function), defaultu nie
    dostają nigdy — tam tekst z C++ nie pasuje do typu parametru lambdy.
    """
    Default = P.Default.strip()
    if not Default:
        return ""

    Clean     = _StripQualifiers(P.Type.strip())
    CleanNoNs = re.sub(r"\bPlu::", "", Clean).strip()
    if (_RE_CLASS_POINTER.match(CleanNoNs)
            or _RE_USE_POINTER.match(CleanNoNs) or _RE_OWNING_POINTER.match(CleanNoNs)
            or _RE_STD_FUNCTION.match(re.sub(r"\bPlu::", "", _StripOuterQualifiers(P.Type)).strip())):
        return ""

    if _RE_LITERAL_DEFAULT.match(Default) or _RE_EMPTY_CONTAINER_DEFAULT.match(Default):
        return f" = {Default}"

    if RegisteredNames:
        M = _RE_TYPE_CONSTRUCTION_DEFAULT.match(Default) or _RE_ENUM_VALUE_DEFAULT.match(Default)
        if M and M.group(1) in RegisteredNames:
            return f" = {Default}"

    return ""


def _HasUnbindableParams(Params: List[ParamInfo], AllClasses: List = []) -> bool:
    """
    True gdy funkcji nie da się sensownie wystawić do Pythona: bierze TOwningPointer do typu, który
    nie jest assetem, albo TUsePointer do typu, który nie jest ani assetem, ani EngineObjectem. pybind11 nie ma castera dla smart pointerów silnika (a z
    surowego wskaźnika nie da się ich odtworzyć – TUsePointer powstaje tylko z ControlBlocka),
    więc takie .def rzucałoby cast_error przy pierwszym wywołaniu. Lepiej nie wystawiać wcale.
    """
    for P in Params:
        CleanNoNs = re.sub(r"\bPlu::", "", _StripQualifiers(P.Type.strip())).strip()
        M = _RE_USE_POINTER.match(CleanNoNs) or _RE_OWNING_POINTER.match(CleanNoNs)
        if not M:
            continue
        if _ObjectUsePointerInner(P.Type, AllClasses):
            continue
        InnerClean = re.sub(r"\bPlu::", "", M.group(1).strip()).strip()
        InnerInfo  = next((C for C in AllClasses if C.Name == InnerClean), None)
        if InnerInfo is None or not IsTypeDerivedFrom("IAssetData", InnerInfo, AllClasses):
            return True
    return False


def _BuildPySignature(Params: List[ParamInfo], RegisteredNames: Optional[set] = None) -> str:
    """Buduje string argumentów pybind11 py::arg("name") dla funkcji."""
    return ", ".join(f'py::arg("{P.Name}"){_PyArgDefault(P, RegisteredNames)}'
                     for P in Params if P.Name)


def _FuncPyCallArgs(Params: List[ParamInfo]) -> str:
    """Lista nazw parametrów do przekazania w lambdzie C++."""
    return ", ".join(P.Name if P.Name else f"arg{I}" for I, P in enumerate(Params))


def _CollectAllOverrideFns(Cls: TypeInfo, AllTypes: List[TypeInfo],
                           ExportedTypes: Optional[List[TypeInfo]] = None,
                           AllParsedTypes: Optional[List[TypeInfo]] = None) -> List[FunctionInfo]:
    if ExportedTypes is None:  ExportedTypes  = []
    if AllParsedTypes is None: AllParsedTypes = []
    """
    Zbiera wszystkie funkcje z PyOverride z całej hierarchii dziedziczenia (Cls + bazy).
    Priorytet źródeł (od najlepszego):
      1. AllParsedTypes – wszystkie typy z parsowania (pełne Functions, nie tylko PyExport)
      2. ExportedTypes  – podzbiór AllParsedTypes z PyExport (zostawiony dla kompatybilności)
      3. AllTypes       – z ClassList.txt (Functions puste, tylko nawigacja hierarchii)
    Funkcje z klas pochodnych mają priorytet nad bazowymi.
    """
    AllSources: dict = {T.Name: T for T in AllTypes}
    AllSources.update({T.Name: T for T in ExportedTypes})
    AllSources.update({T.Name: T for T in AllParsedTypes})  # najlepsze dane – nadpisują

    NameToFunc: dict = {}

    def Collect(T: TypeInfo):
        for BaseName in T.Bases:
            BaseType = AllSources.get(BaseName)
            if BaseType:
                Collect(BaseType)
        for F in T.Functions:
            if HasPyParam(F.MacroParams, "PyOverride"):
                NameToFunc[F.Name] = F

    Collect(Cls)
    # Funkcje z nieprzekładalnymi parametrami (patrz _HasUnbindableParams) pomijamy – ani
    # trampolina, ani .def nie mogłyby ich obsłużyć.
    return [F for F in NameToFunc.values() if not _HasUnbindableParams(F.Params, AllParsedTypes or AllTypes)]


def GeneratePybindBindings(Data: List[FileData], AllClasses: List[TypeInfo] = []):
    """
    Generuje:
      ReflectionCache/PluEngineBindings.cpp  – kod pybind11
      ReflectionCache/PluEngine.pyi          – stuby Pythona
    """

    # Zbierz wszystkie typy z PyExport
    ExportedTypes: List[TypeInfo] = []
    for FileEntry in Data:
        for Cls in FileEntry.Children:
            if Cls.Type == ClassType.INTERFACE:
                continue
            if IsPyExported(Cls):
                ExportedTypes.append(Cls)

    # Sortuj topologicznie – bazy muszą być zarejestrowane przed klasami pochodnymi
    def TopoSort(Types: List[TypeInfo]) -> List[TypeInfo]:
        NameToType = {T.Name: T for T in Types}
        Visited:    set = set()
        Result:     List[TypeInfo] = []

        def Visit(T: TypeInfo):
            if T.Name in Visited:
                return
            Visited.add(T.Name)
            # Najpierw odwiedź wszystkie bazy które są w ExportedTypes
            for Base in T.Bases:
                if Base in NameToType:
                    Visit(NameToType[Base])
            Result.append(T)

        for T in Types:
            Visit(T)
        return Result

    ExportedTypes = TopoSort(ExportedTypes)

    # Wszystkie sparsowane typy z Data (pełne Functions – nie tylko PyExport)
    AllParsedTypes: List[TypeInfo] = [Cls for FD in Data for Cls in FD.Children]
    # Zbierz funkcje globalne (wszystkie PLU_FUNCTION poza klasą, bez PyNotCallable)
    AllGlobalFuncs: List[GlobalFunctionInfo] = []
    for FileEntry in Data:
        for GF in FileEntry.GlobalFunctions:
            if not HasPyParam(GF.MacroParams, "PyNotCallable"):
                AllGlobalFuncs.append(GF)

    # Zbierz enumy z PyExport
    ExportedEnums: List[EnumInfo] = []
    for FileEntry in Data:
        for Enum in FileEntry.Enums:
            if HasPyParam(Enum.ReflectionParams, "PyExport"):
                ExportedEnums.append(Enum)

    if not ExportedTypes and not AllGlobalFuncs and not ExportedEnums:
        print("Bindings: nothing to export – nothing generated.")
        return

    os.makedirs(OutputDir, exist_ok=True)
    BindingsCpp = os.path.join(OutputDir, "PluEngineBindings.cpp")
    StubPyi     = os.path.join(OutputDir, "PluEngine.pyi")

    # ── PluEngineBindings.cpp ─────────────────────────────────────────
    B = io.StringIO()
    if True:
        B.write("// AUTO-GENERATED by ReflectionGenerator – DO NOT EDIT\n")
        B.write("// pybind11 bindings for PluEngine\n\n")
        B.write('#include <pybind11/embed.h>\n')
        B.write('#include <pybind11/stl.h>\n')
        B.write('#include <pybind11/operators.h>\n\n')
        B.write('#include "PluEngine/Scripting/PythonMath.h"\n')
        B.write('#include "PluEngine/Scripting/PythonPointers.h"\n\n')
        B.write("namespace py = pybind11;\n\n")

        # Includes per plik źródłowy
        IncludedFiles: set = set()
        for Cls in ExportedTypes:
            FP = str(Cls.FilePath).replace("\\", "/")
            if FP not in IncludedFiles:
                B.write(f'#include "{FP}"\n')
                IncludedFiles.add(FP)
        for GF in AllGlobalFuncs:
            FP = str(GF.FilePath).replace("\\", "/")
            if FP not in IncludedFiles:
                B.write(f'#include "{FP}"\n')
                IncludedFiles.add(FP)
        for Enum in ExportedEnums:
            FP = str(Enum.FilePath).replace("\\", "/")
            if FP not in IncludedFiles:
                B.write(f'#include "{FP}"\n')
                IncludedFiles.add(FP)

        B.write("\nusing namespace Plu;\n\n")

        # ── Trampoline klasy dla PyDerive + PyOverride ─────────────────
        for Cls in ExportedTypes:
            IsDerive    = HasPyParam(Cls.ReflectionParams, "PyDerive")
            OwnOverride = [F for F in Cls.Functions if HasPyParam(F.MacroParams, "PyOverride")
                           and not _HasUnbindableParams(F.Params, AllClasses)]
            if not IsDerive:
                # Walidacja: PyOverride bez PyDerive → #pragma error
                for F in OwnOverride:
                    B.write(f'#error "{Cls.Name}::{F.Name} has PyOverride but {Cls.Name} is missing PyDerive"\n')
                continue

            # Zbierz override'y z całej hierarchii (własne + odziedziczone)
            AllOverrideFns = _CollectAllOverrideFns(Cls, AllClasses, ExportedTypes, AllParsedTypes)
            if not AllOverrideFns:
                continue

            TmpName = f"Py{Cls.Name}"
            B.write(f"// Trampoline for {Cls.Name}\n")
            B.write(f"struct {TmpName} : public {Cls.Name} {{\n")
            B.write(f"    using {Cls.Name}::{Cls.Name};\n\n")
            for F in AllOverrideFns:
                ParamDecl = ", ".join(
                    f"{P.Type} {P.Name if P.Name else f'arg{I}'}"
                    for I, P in enumerate(F.Params)
                )
                CallArgs  = ", ".join(P.Name if P.Name else f"arg{I}" for I, P in enumerate(F.Params))
                ConstQual = " const" if F.IsConst else ""
                B.write(f"    {F.ReturnType} {F.Name}({ParamDecl}){ConstQual} override {{\n")
                # Smart-pointer params have no pybind11 caster – Python gets the raw pointer
                # (GetAssetUserAsRaw / UserFromPython turn it back on the way in). PYBIND11_OVERRIDE
                # would pass the same arguments to both Python and the base call, so it is split here.
                PyCallArgList = []
                for I, P in enumerate(F.Params):
                    ArgName   = P.Name if P.Name else f"arg{I}"
                    CleanNoNs = re.sub(r"\bPlu::", "", _StripQualifiers(P.Type)).strip()
                    IsSmart   = _RE_USE_POINTER.match(CleanNoNs) or _RE_OWNING_POINTER.match(CleanNoNs)
                    PyCallArgList.append(f"{ArgName}.GetRaw()" if IsSmart else ArgName)
                PyCallArgs = ", ".join(PyCallArgList)
                if PyCallArgs != CallArgs:
                    B.write(f"        PYBIND11_OVERRIDE_IMPL({F.ReturnType}, {Cls.Name}, \"{F.Name}\", {PyCallArgs});\n")
                    B.write(f"        return {Cls.Name}::{F.Name}({CallArgs});\n")
                else:
                    B.write(f"        PYBIND11_OVERRIDE(\n")
                    B.write(f"            {F.ReturnType},\n")
                    B.write(f"            {Cls.Name},\n")
                    B.write(f"            {F.Name}")
                    if CallArgs:
                        B.write(f",\n            {CallArgs}")
                    B.write(f"\n        );\n")
                B.write(f"    }}\n\n")
            B.write(f"}};\n\n")

        B.write("PYBIND11_EMBEDDED_MODULE(PluEngine, m) {\n")
        B.write('    m.doc() = "PluEngine Python bindings";\n\n')

        # ── Typy matematyczne ─────────────────────────────────────────
        # Vec2/3/4, IVec2/3/4, Quaternion i Matrix4 – ręcznie pisane bindingi. Muszą pójść przed
        # enumami i klasami: pybind11 rozwiązuje typy argumentów i wartości domyślne w .def().
        B.write("    // Math types (hand-written bindings)\n")
        B.write("    RegisterMathTypes(m);\n\n")

        # ── Enumy ──────────────────────────────────────────────────────
        if ExportedEnums:
            B.write("    // Enums\n")
        for Enum in ExportedEnums:
            PyName  = GetPyParamValue(Enum.ReflectionParams, "PyName") or Enum.Name
            PyDoc   = GetPyDoc(Enum.ReflectionParams)
            B.write(f'    py::enum_<{Enum.Name}>(m, "{PyName}"')
            if PyDoc:
                B.write(f', "{PyDoc}"')
            B.write(")\n")
            for V in Enum.Values:
                VDoc = GetPyDoc(V.MacroParams)
                B.write(f'        .value("{V.Name}", {Enum.Name}::{V.Name}')
                if VDoc:
                    B.write(f', "{VDoc}"')
                B.write(")\n")
            B.write(f'        .def_static("ToString", []({Enum.Name} v) {{ return ToString(v); }}, py::arg("value"))\n')
            B.write(f'        .def_static("FromString", [](const Plu::String& s) {{ return FromString<{Enum.Name}>(s); }}, py::arg("str"))\n')
            B.write(f'        ;\n\n')

        # Typy już zarejestrowane w module – tylko ich konstrukcje/wartości mogą trafić do
        # py::arg(...) = default (pybind11 konwertuje domyślne argumenty w momencie .def()).
        RegisteredNames: set = {E.Name for E in ExportedEnums} | MATH_TYPE_NAMES

        for Cls in ExportedTypes:
            PyName      = GetPyName(Cls)
            PyDoc       = GetPyDoc(Cls.ReflectionParams)
            IsAbstr     = HasPyParam(Cls.ReflectionParams, "Abstract")
            IsDerive    = HasPyParam(Cls.ReflectionParams, "PyDerive")
            # Własne PyOverride – do .def rejestracji
            OwnOverrideFns = [F for F in Cls.Functions if HasPyParam(F.MacroParams, "PyOverride")
                              and not _HasUnbindableParams(F.Params, AllClasses)]
            # Wszystkie PyOverride z hierarchii – decyduje o TmpName
            AllOverrideFns = _CollectAllOverrideFns(Cls, AllClasses, ExportedTypes, AllParsedTypes) if IsDerive else []
            TmpName     = f"Py{Cls.Name}" if (IsDerive and AllOverrideFns) else ""

            # Bazowa klasa
            BaseDecl = ""
            if Cls.Bases:
                FirstBase = Cls.Bases[0]
                if any(C.Name == FirstBase and IsPyExported(C) for C in ExportedTypes):
                    BaseDecl = f", {FirstBase}"

            # Dodatkowe opcje py::class_
            ClassOpts = ""
            if IsDerive:
                ClassOpts += ", py::dynamic_attr()"

            ClsTemplate = f"{Cls.Name}, {TmpName}{BaseDecl}" if TmpName else f"{Cls.Name}{BaseDecl}"

            B.write(f'    // {Cls.Type.name}: {Cls.Name}\n')
            B.write(f'    py::class_<{ClsTemplate}>(m, "{PyName}"')
            if PyDoc:
                B.write(f', "{PyDoc}"')
            B.write(f'{ClassOpts})\n')

            if not IsAbstr:
                B.write(f'        .def(py::init<>())\n')
                if IsTypeDerivedFrom("GameObjectComponent", Cls, AllClasses):
                    B.write(
                        f'        .def_static("create", [](GameObject* parent, String componentName) {{\n'
                        f'            auto component = DynamicCast<{Cls.Name}>(parent->AddComponent({Cls.Name}::GetStaticClass(), componentName));\n'
                        f'            if (!component) throw std::runtime_error("Failed to create {Cls.Name}");\n'
                        f'            return component.GetRaw();\n'
                        f'        }}, py::arg("parent"), py::arg("componentName"), py::return_value_policy::reference)\n'
                    )

            # Pola z PyExport
            PyProps = [P for P in Cls.Properties if IsPyExported(P)]
            for Prop in PyProps:
                PropDoc  = GetPyDoc(Prop.Params)
                ReadOnly = HasPyParam(Prop.Params, "PyReadOnly")
                # Sprawdź czy typ to TClassPointer<T> – wymaga custom settera przez TypeRegistry
                CleanPropType = re.sub(r"\bPlu::", "", _StripQualifiers(Prop.Type)).strip()
                MCP = re.match(r"^(?:Plu::)?TClassPointer\s*<(.+)>$", CleanPropType)
                AssetInner = _AssetUsePointerInner(Prop.Type, AllClasses)
                if AssetInner and not (Prop.GetterName or Prop.SetterName):
                    # TUsePointer<T:IAssetData> field: exposed as a raw IAssetData* (pybind11 downcasts to
                    # the most derived registered type); the setter recovers the TUsePointer from the asset
                    # manager and rejects assets of the wrong type instead of reinterpreting them.
                    T = AssetInner.Name
                    Getter = (f"[]({Cls.Name}& self) {{ return static_cast<IAssetData*>(self.{Prop.Name}.GetRaw()); }}")
                    if ReadOnly:
                        B.write(f'        .def_property_readonly("{Prop.Name}", {Getter}, py::return_value_policy::reference')
                    else:
                        Setter = (
                            f"[]({Cls.Name}& self, IAssetData* asset) {{ "
                            f"TUsePointer<{T}> typed; "
                            f"if (asset) typed = DynamicCast<{T}>(GetAssetUserAsRaw(asset)); "
                            f"if (asset && !typed) throw py::type_error(\"{Cls.Name}.{Prop.Name} expects a {T} asset\"); "
                            f"self.{Prop.Name} = typed; "
                            f"}}"
                        )
                        B.write(f'        .def_property("{Prop.Name}", {Getter}, {Setter}, py::return_value_policy::reference')
                    if PropDoc:
                        B.write(f', "{PropDoc}"')
                    B.write(")\n")
                elif MCP:
                    # def_property z getterem zwracającym pole i setterem przez TypeRegistry
                    Getter = f"[](const {Cls.Name}& self) {{ return self.{Prop.Name}; }}"
                    if ReadOnly:
                        B.write(f'        .def_property_readonly("{Prop.Name}", {Getter}')
                    else:
                        Setter = (
                            f"[]({Cls.Name}& self, py::object _pytype) {{ "
                            f"std::string _name = pybind11::str(_pytype.attr(\"__name__\")); "
                            f"self.{Prop.Name} = TypeRegistry::GetInstance()->GetTypeOfName(_name.c_str()); "
                            f"}}"
                        )
                        B.write(f'        .def_property("{Prop.Name}", {Getter}, {Setter}')
                    if PropDoc:
                        B.write(f', "{PropDoc}"')
                    B.write(")\n")
                else:
                    if Prop.GetterName or Prop.SetterName:
                        # Getter/Setter przez metody – ignorujemy PyReadOnly (błąd walidacji byłby osobno)
                        PropDoc  = GetPyDoc(Prop.Params)
                        NeedsRef = _ReturnsPointer(Prop.Type)
                        RefPolicy = ", py::return_value_policy::reference" if NeedsRef else ""
                        Getter: str = ""
                        Setter: str = ""
                        if Prop.GetterName:
                            Getter = f"[]({Cls.Name}& self) {{ return self.{Prop.GetterName}(); }}"
                        else:
                            Getter = f"[]({Cls.Name}& self) {{ return self.{Prop.Name}; }}"
                        if Prop.SetterName:
                            Setter = f"[]({Cls.Name}& self, const {Prop.Type}& v) {{ self.{Prop.SetterName}(v); }}"
                        else:
                            Setter = f"[]({Cls.Name}& self, const {Prop.Type}& v) {{ self.{Prop.Name} = v; }}"
                        B.write(f'        .def_property("{Prop.Name}", {Getter}, {Setter}')
                        if PropDoc:
                            B.write(f', "{PropDoc}"')
                        if NeedsRef:
                            B.write(f'{RefPolicy}')
                        B.write(")\n")
                    else:
                        Accessor = "def_readonly" if ReadOnly else "def_readwrite"
                        B.write(f'        .{Accessor}("{Prop.Name}", &{Cls.Name}::{Prop.Name}')
                        if PropDoc:
                            B.write(f', "{PropDoc}"')
                        if _ReturnsPointer(Prop.Type):
                            B.write(', py::return_value_policy::reference')
                        B.write(")\n")

            # Metody – wszystkie PLU_FUNCTION bez PyNotCallable
            for F in Cls.Functions:
                if HasPyParam(F.MacroParams, "PyNotCallable"):
                    continue
                if HasPyParam(F.MacroParams, "PyOverride"):
                    continue  # obsłużone przez trampoline – def nadal potrzebne
                if _HasUnbindableParams(F.Params, AllClasses):
                    continue
                ArgList = _BuildPySignature(F.Params, RegisteredNames)
                if _NeedsLambda(F.Params, F.ReturnType, AllClasses):
                    Callable, NeedsRef = _BuildWrapperLambda(Cls.Name, F.Name, F.Params, F.ReturnType, F.IsConst, AllClasses)
                else:
                    Callable, NeedsRef = f"&{Cls.Name}::{F.Name}", _ReturnsPointer(F.ReturnType)
                B.write(f'        .def("{F.Name}", {Callable}')
                if ArgList:
                    B.write(f', {ArgList}')
                if NeedsRef:
                    B.write(', py::return_value_policy::reference')
                B.write(")\n")

            # PyOverride metody też potrzebują .def dla wywołania z C++
            for F in OwnOverrideFns:
                ArgList = _BuildPySignature(F.Params, RegisteredNames)
                if _NeedsLambda(F.Params, F.ReturnType, AllClasses):
                    Callable, NeedsRef = _BuildWrapperLambda(Cls.Name, F.Name, F.Params, F.ReturnType, F.IsConst, AllClasses)
                else:
                    Callable, NeedsRef = f"&{Cls.Name}::{F.Name}", _ReturnsPointer(F.ReturnType)
                B.write(f'        .def("{F.Name}", {Callable}')
                if ArgList:
                    B.write(f', {ArgList}')
                if NeedsRef:
                    B.write(', py::return_value_policy::reference')
                B.write(")\n")

            B.write(f'        ;\n\n')
            RegisteredNames.add(Cls.Name)

        # ── Funkcje globalne ───────────────────────────────────────────
        if AllGlobalFuncs:
            B.write("    // Global functions\n")
        for GF in AllGlobalFuncs:
            if _HasUnbindableParams(GF.Params, AllClasses):
                continue
            ArgList = _BuildPySignature(GF.Params, RegisteredNames)
            if _NeedsLambda(GF.Params, GF.ReturnType, AllClasses):
                Callable, NeedsRef = _BuildWrapperLambdaGlobal(GF.Name, GF.Params, GF.ReturnType, AllClasses)
            else:
                Callable, NeedsRef = f"&{GF.Name}", _ReturnsPointer(GF.ReturnType)
            B.write(f'    m.def("{GF.Name}", {Callable}')
            if ArgList:
                B.write(f', {ArgList}')
            if NeedsRef:
                B.write(', py::return_value_policy::reference')
            FDoc = GetPyDoc(GF.MacroParams)
            if FDoc:
                B.write(f', "{FDoc}"')
            B.write(");\n")

        B.write("}\n")

    Changed = WriteIfChanged(BindingsCpp, B.getvalue())
    print(f"{'Generated' if Changed else 'Unchanged'} {BindingsCpp} ({len(ExportedTypes)} type(s), {len(AllGlobalFuncs)} global func(s))")

    # ── PluEngine.pyi (stuby) ─────────────────────────────────────────
    P = io.StringIO()
    if True:
        P.write("# AUTO-GENERATED by ReflectionGenerator – DO NOT EDIT\n")
        P.write("# Python type stubs for PluEngine\n\n")
        P.write("from __future__ import annotations\n")
        P.write("from typing import Callable, Dict, Iterator, List, Optional, Sequence, Tuple, Type, overload\n")
        P.write("import enum\n\n")

        WriteMathStubs(P)

        # W stubach cały moduł jest już „zarejestrowany”, więc do oceny domyślnych wartości
        # bierzemy komplet nazw typów i enumów.
        StubRegisteredNames: set = ({T.Name for T in ExportedTypes} | {E.Name for E in ExportedEnums}
                                    | MATH_TYPE_NAMES)

        def WriteFuncStub(indent: str, F, ClsName: str = ""):
            """Zapisuje stub metody lub funkcji globalnej."""
            if HasPyParam(F.MacroParams, "PyNotCallable"):
                return
            RetPy  = CppTypeToPy(F.ReturnType)
            IsOver = HasPyParam(F.MacroParams, "PyOverride")
            # Buduj listę argumentów
            PyArgs = []
            for I, Param in enumerate(F.Params):
                ArgName = Param.Name if Param.Name else f"arg{I}"
                # TClassPointer<T> → Type[T]
                ArgType = Param.Type.strip()
                M = re.match(r"(?:Plu::)?TClassPointer\s*<(.+)>", ArgType)
                if M:
                    InnerT = CppTypeToPy(M.group(1))
                    ArgType = f"Type[{InnerT}]"
                else:
                    # TUsePointer<T>/TOwningPointer<T> gdzie T:IAssetData → IAssetData w stubbie
                    CleanArg = re.sub(r"\bPlu::", "", _StripQualifiers(ArgType)).strip()
                    MUP = _RE_USE_POINTER.match(CleanArg) or _RE_OWNING_POINTER.match(CleanArg)
                    if MUP and AllClasses:
                        InnerT = re.sub(r"\bPlu::", "", MUP.group(1)).strip()
                        InnerTypeInfo = next((C for C in AllClasses if C.Name == InnerT), None)
                        if InnerTypeInfo and IsTypeDerivedFrom("IAssetData", InnerTypeInfo, AllClasses):
                            ArgType = "IAssetData"
                        else:
                            ArgType = CppTypeToPy(ArgType)
                    else:
                        ArgType = CppTypeToPy(ArgType)
                # Stub pokazuje sam fakt istnienia domyślnej wartości (konwencja .pyi: "= ...").
                # Tylko dla tych, które faktycznie trafiły do py::arg – reszta jest w Pythonie
                # nadal wymagana.
                DefaultMark = " = ..." if _PyArgDefault(Param, StubRegisteredNames) else ""
                PyArgs.append(f"{ArgName}: {ArgType}{DefaultMark}")
            ArgStr = ", ".join(PyArgs)
            SelfStr = "self, " if ClsName else ""
            if IsOver:
                P.write(f"{indent}def {F.Name}(self, {ArgStr}) -> {RetPy}: ...\n")
            else:
                P.write(f"{indent}def {F.Name}({SelfStr}{ArgStr}) -> {RetPy}: ...\n")

        for Cls in ExportedTypes:
            PyName   = GetPyName(Cls)
            PyDoc    = GetPyDoc(Cls.ReflectionParams)
            IsAbstr  = HasPyParam(Cls.ReflectionParams, "Abstract")
            IsDerive = HasPyParam(Cls.ReflectionParams, "PyDerive")

            BaseDecl = ""
            if Cls.Bases:
                FirstBase = Cls.Bases[0]
                if any(C.Name == FirstBase and IsPyExported(C) for C in ExportedTypes):
                    BaseDecl = GetPyName(next(C for C in ExportedTypes if C.Name == FirstBase))
            PyClass = f"class {PyName}({BaseDecl}):" if BaseDecl else f"class {PyName}:"
            P.write(f"{PyClass}\n")
            if PyDoc or IsDerive:
                DocLines = []
                if PyDoc:
                    DocLines.append(PyDoc)
                if IsDerive:
                    DocLines.append("Subclassable from Python (PyDerive).")
                P.write(f'    """{" ".join(DocLines)}"""\n')

            HasContent = False

            if not IsAbstr:
                P.write("    def __init__(self) -> None: ...\n")
                if IsTypeDerivedFrom("GameObjectComponent", Cls, AllClasses):
                    PyClsName = GetPyName(Cls)
                    P.write(f"    @staticmethod\n")
                    P.write(f"    def create(parent: GameObject, componentName: str) -> {PyClsName}: ...\n")
                HasContent = True

            # Pola z PyExport
            for Prop in Cls.Properties:
                if not IsPyExported(Prop):
                    continue
                HasContent = True
                PropDoc  = GetPyDoc(Prop.Params)
                PyType   = CppTypeToPy(Prop.Type)
                ReadOnly = HasPyParam(Prop.Params, "PyReadOnly")
                HasGetterSetter = bool(Prop.GetterName and Prop.SetterName)
                AssetInner = _AssetUsePointerInner(Prop.Type, AllClasses)
                if AssetInner and not (Prop.GetterName or Prop.SetterName):
                    # Mirrors the binding: getter yields the asset (as IAssetData when T is not exported),
                    # setter takes any IAssetData and type-checks it at runtime.
                    GetType = f"Optional[{GetPyName(AssetInner)}]" if IsPyExported(AssetInner) else "Optional[IAssetData]"
                    P.write(f"    @property\n")
                    P.write(f"    def {Prop.Name}(self) -> {GetType}:\n")
                    if PropDoc:
                        P.write(f'        """{PropDoc}"""\n')
                    P.write(f"        ...\n")
                    if not ReadOnly:
                        P.write(f"    @{Prop.Name}.setter\n")
                        P.write(f"    def {Prop.Name}(self, value: Optional[IAssetData]) -> None: ...\n")
                elif ReadOnly or HasGetterSetter:
                    P.write(f"    @property\n")
                    P.write(f"    def {Prop.Name}(self) -> {PyType}:\n")
                    if PropDoc:
                        P.write(f'        """{PropDoc}"""\n')
                    P.write(f"        ...\n")
                    if HasGetterSetter:
                        P.write(f"    @{Prop.Name}.setter\n")
                        P.write(f"    def {Prop.Name}(self, value: {PyType}) -> None: ...\n")
                else:
                    P.write(f"    {Prop.Name}: {PyType}")
                    if PropDoc:
                        P.write(f"  # {PropDoc}")
                    P.write("\n")

            # Metody (bez PyNotCallable)
            VisibleFuncs = [F for F in Cls.Functions if not HasPyParam(F.MacroParams, "PyNotCallable")
                            and not _HasUnbindableParams(F.Params, AllClasses)]
            for F in VisibleFuncs:
                HasContent = True
                WriteFuncStub("    ", F, ClsName=Cls.Name)

            if not HasContent:
                P.write("    ...\n")
            P.write("\n")

        # Enumy
        if ExportedEnums:
            P.write("# ── Enums ─────────────────────────────────────────────────────\n")
        for Enum in ExportedEnums:
            PyName = GetPyParamValue(Enum.ReflectionParams, "PyName") or Enum.Name
            PyDoc  = GetPyDoc(Enum.ReflectionParams)
            P.write(f"class {PyName}(enum.IntEnum):\n")
            if PyDoc:
                P.write(f'    """{PyDoc}"""\n')
            for V in Enum.Values:
                VDoc = GetPyDoc(V.MacroParams)
                P.write(f"    {V.Name} = ...")
                if VDoc:
                    P.write(f"  # {VDoc}")
                P.write("\n")
            P.write(f"    @staticmethod\n")
            P.write(f"    def ToString(value: {PyName}) -> str: ...\n")
            P.write(f"    @staticmethod\n")
            P.write(f"    def FromString(s: str) -> {PyName}: ...\n")
            P.write("\n")

        # Funkcje globalne
        if AllGlobalFuncs:
            P.write("# ── Global functions ──────────────────────────────────────\n")
        for GF in AllGlobalFuncs:
            WriteFuncStub("", GF)
        if AllGlobalFuncs:
            P.write("\n")

    Changed = WriteIfChanged(StubPyi, P.getvalue())
    print(f"{'Generated' if Changed else 'Unchanged'} {StubPyi} ({len(ExportedTypes)} type(s), {len(ExportedEnums)} enum(s), {len(AllGlobalFuncs)} global func(s))")


def ProjectApiMacro(Proj: str) -> str:
    """Export macro of the library a project compiles into ('' for the apps, which export nothing)."""
    return f"PLU{Proj[3:].upper()}_API " if Proj.startswith("Plu") else ""


def GetFileProject(F: FileData) -> str:
    """Wyciąga nazwę projektu z FileData – działa też gdy Children jest puste (tylko globalne funkcje)."""
    if F.Children:
        return F.Children[0].Project
    # Fallback: wyznacz projekt ze ścieżki pliku (pierwszy segment względem korzenia)
    try:
        Parts = F.FilePath.relative_to(Path(os.path.dirname(ScriptDir))).parts
        return Parts[1] if Parts[0] == "LibEngine" and len(Parts) > 1 else Parts[0]
    except ValueError:
        return "Unknown"


def GenerateReflectionData(Data: List[FileData]):
    if not Data:
        print("No reflection changes.")
        return

    print(f"Generating reflection data for {len(Data)} file(s)...")

    # ── Zbierz projekty ───────────────────────────────────────────────
    ProjectGroups: List[str] = []
    # Z bieżącej sesji
    for F in Data:
        Proj = GetFileProject(F)
        if Proj not in ProjectGroups:
            ProjectGroups.append(Proj)
    # Z dysku – projekty które mają ClassList.txt ale nie były w tej sesji
    if os.path.exists(OutputDir):
        for Entry in os.scandir(OutputDir):
            if Entry.is_dir() and Entry.name not in ProjectGroups:
                if os.path.exists(os.path.join(Entry.path, "ClassList.txt")):
                    ProjectGroups.append(Entry.name)

    if ForceMode:
        for Proj in ProjectGroups:
            ClassListPath = os.path.join(OutputDir, Proj, "ClassList.txt")
            if os.path.exists(ClassListPath):
                os.remove(ClassListPath)

    # ── Zapisz ClassList.txt ──────────────────────────────────────────
    for F in Data:
        if not F.Children:
            continue  # brak klas – pomijamy ClassList
        Proj            = GetFileProject(F)
        ClassListFile   = os.path.join(OutputDir, Proj, "ClassList.txt")
        os.makedirs(os.path.dirname(ClassListFile), exist_ok=True)

        # Exact names, not a substring search over the whole file: "Animation" occurs inside
        # "AnimationVectorKey", so testing `Cls.Name not in ExistingContent` silently dropped every
        # type whose name is a prefix of another one in the same project — and a dropped entry here
        # means no Register_Reflection_* call, so the type is missing from TypeRegistry at runtime.
        # The set is also updated as we go, so duplicates within a single run are caught too.
        ExistingNames = set()
        if os.path.exists(ClassListFile):
            with open(ClassListFile, encoding="utf-8") as CL:
                for Line in CL:
                    if Line.strip():
                        ExistingNames.add(Line.split(" - ", 1)[0].strip())

        with open(ClassListFile, "a", encoding="utf-8") as CL:
            for Cls in F.Children:
                if IsNoReflection(Cls):
                    continue
                if Cls.Name in ExistingNames and not ForceMode:
                    continue
                CL.write(f"{Cls.Name} - {Cls.Bases} - {Cls.Type} - {Cls.FilePath} - {Cls.ReflectionParams}\n")
                ExistingNames.add(Cls.Name)

    print(f"Found projects: {ProjectGroups}")

    # ── Wczytaj ALL_CLASSES ───────────────────────────────────────────
    AllClasses: List[TypeInfo] = []
    for Proj in ProjectGroups:
        ClassListFile = os.path.join(OutputDir, Proj, "ClassList.txt")
        if not os.path.exists(ClassListFile):
            continue
        with open(ClassListFile, "r", encoding="utf-8") as CL:
            for RawLine in CL:
                Parts = RawLine.strip().split(" - ")
                if len(Parts) < 4:
                    continue
                ClassName    = Parts[0]
                ClassBases   = ast.literal_eval(Parts[1])
                ClassTypeStr = Parts[2].removeprefix("ClassType.")
                ClassPath    = Parts[3]
                # 5. kolumna (opcjonalna, dla wstecznej zgodności) — parametry makra
                ClassParams  = ast.literal_eval(Parts[4]) if len(Parts) > 4 else []
                AllClasses.append(TypeInfo(
                    Name             = ClassName,
                    Type             = ClassType[ClassTypeStr],
                    FilePath         = Path(ClassPath),
                    Bases            = ClassBases,
                    Project          = Proj,
                    ReflectionParams = ClassParams,
                    Properties       = [],
                    UuidProperty     = None,
                ))

    # ── Odfiltruj interfejsy z baz ────────────────────────────────────
    InterfaceType = next((T for T in AllClasses if T.Name == "PluInterface"), None)
    for F in Data:
        for Cls in F.Children:
            if len(Cls.Bases) > 1 and InterfaceType:
                ToRemove = [B for B in Cls.Bases[1:] if IsTypeDerivedFrom("PluInterface", Cls, AllClasses)]
                for B in ToRemove:
                    print(f"Info: {Cls.Name} implementuje interfejs {B}, usuwam z baz.")
                    Cls.Bases.remove(B)

    # ── Generuj .generated.h i .generated.cpp ─────────────────────────
    for FileEntry in Data:
        # Klasy do refleksji (bez NoReflection)
        ReflectionChildren = [C for C in FileEntry.Children if not IsNoReflection(C)]
        if not ReflectionChildren and not FileEntry.Enums:
            continue  # plik zawiera tylko globalne funkcje lub same NoReflection klasy
        FilePath    = FileEntry.FilePath
        Proj        = GetFileProject(FileEntry)
        GenHeader   = os.path.join(OutputDir, Proj, FilePath.stem + ".generated.h")
        GenSource   = os.path.join(OutputDir, Proj, FilePath.stem + ".generated.cpp")

        # --- .generated.h ---
        H = io.StringIO()
        if True:
            H.write("#pragma once\n")
            H.write("#include <PluEngine/Core/Reflection/ReflectionBase.h>\n\n")
            if FileEntry.Enums:
                # Forward declarations – w namespace enuma jeśli podany, z typem bazowym
                for Enum in FileEntry.Enums:
                    BaseDecl = f" : {Enum.BaseType}" if Enum.BaseType else ""
                    if Enum.Namespace:
                        H.write(f"namespace {Enum.Namespace} {{ enum class {Enum.Name}{BaseDecl}; }}\n")
                    else:
                        H.write(f"enum class {Enum.Name}{BaseDecl};\n")
                H.write("\n")

            for Cls in ReflectionChildren:
                IsStruct = Cls.Type == ClassType.STRUCT
                ClsUpper = Cls.Name.upper()
                H.write(f"#define REFLECTION_BODY_{ClsUpper}() \\\n")
                H.write(f"    public: \\\n")
                H.write(f"        static Plu::TypeInfo* GetStaticClass(); \\\n")
                if not IsStruct:
                    H.write(f"        virtual Plu::TypeInfo* GetClass() override; \\\n")
                else:
                    # Structy dostają wirtualne GetClass() tylko gdy uczestniczą w hierarchii
                    # zreflektowanych structów (patrz StructGetClassIsVirtual). Struct z bazą
                    # o wirtualnym GetClass() używa 'override', struct będący jedynie bazą
                    # wprowadza virtual. Samodzielne structy oraz PLU_STRUCT(NoVirtualClass)
                    # zostają bez vtable (niewirtualne, POD-friendly).
                    if StructGetClassIsVirtual(Cls, AllClasses):
                        if StructHasVirtualStructBase(Cls, AllClasses):
                            H.write(f"        virtual Plu::TypeInfo* GetClass() override; \\\n")
                        else:
                            H.write(f"        virtual Plu::TypeInfo* GetClass(); \\\n")
                    else:
                        H.write(f"        Plu::TypeInfo* GetClass(); \\\n")
                H.write(f"    private: \\\n")
                if not IsStruct:
                    H.write(f"        friend void Register_Reflection_{Cls.Name}();\n")
                else:
                    H.write(f"        friend void Register_Reflection_{Cls.Name}(); \\\n")
                    H.write(f"        public:\n")

            if FileEntry.Enums:
                H.write("\nnamespace Plu {\n\n")
                for Enum in FileEntry.Enums:
                    # Jeśli enum jest w innym namespace, używamy pełnej kwalifikowanej nazwy
                    QualName = f"{Enum.Namespace}::{Enum.Name}" if Enum.Namespace else Enum.Name
                    H.write(f"String {ProjectApiMacro(Proj)}ToString({QualName} value);\n")
                    H.write(f"template<> {QualName} {ProjectApiMacro(Proj)}FromString<{QualName}>(const String& str);\n\n")
                H.write("} // namespace Plu\n")

        WriteIfChanged(GenHeader, H.getvalue())

        # --- .generated.cpp ---
        # Zbierz UUID props do dodatkowych include'ów
        UuidProps: dict = {}
        for Cls in ReflectionChildren:
            for Prop in Cls.Properties:
                if Prop.UuidForClass:
                    UuidClassPath = ""
                    for C in AllClasses:
                        if C.Name == Prop.UuidForClass:
                            UuidClassPath = str(C.FilePath)
                    UuidProps[Prop.Name] = {"class": Prop.UuidForClass, "classPath": UuidClassPath}

        S = io.StringIO()
        if True:
            S.write(f'#include "{FilePath}"\n')
            S.write("#include <PluEngine/Core/Reflection/TypeTraits.h>\n")
            # std::is_copy_assignable_v, used by the emitted CopyPtr lambdas.
            S.write("#include <type_traits>\n\n")

            WrittenIncludes = set()
            for _, Info in UuidProps.items():
                if Info["classPath"] and Info["classPath"] not in WrittenIncludes:
                    WrittenIncludes.add(Info["classPath"])
                    S.write(f'#include "{Info["classPath"]}"\n')

            for Cls in ReflectionChildren:
                for Prop in Cls.Properties:
                    if "TUsePointer<" in Prop.Type:
                        Inner = re.sub(r"(?:Plu::)?TUsePointer<(.+)>", r"\1", Prop.Type)
                        for C in AllClasses:
                            if C.Name == Inner and str(C.FilePath) not in WrittenIncludes:
                                WrittenIncludes.add(str(C.FilePath))
                                S.write(f'#include "{C.FilePath}"\n')

            S.write(f'#include "{FilePath.stem}.generated.h"\n\n')
            S.write(f"using namespace Plu;\n\n")

            for Cls in ReflectionChildren:
                if Cls.Type == ClassType.INTERFACE:
                    print("Skipping interface reflection")
                    continue

                IsStruct = Cls.Type == ClassType.STRUCT

                # GetStaticClass
                S.write(f"TypeInfo* {Cls.Name}::GetStaticClass() {{\n")
                S.write(f"    static TypeInfo* instance = nullptr;\n")
                S.write(f"    if (!instance) {{\n")
                S.write(f'        instance = new TypeInfo(sizeof({Cls.Name}), "{Cls.Name}", TypeType::{Cls.Type.name});\n')

                if "Abstract" not in Cls.ReflectionParams:
                    S.write(f"        instance->Constructor = []() -> void* {{ return new {Cls.Name}(); }};\n")
                S.write(f"        instance->IsAbstract = {"false" if "Abstract" not in Cls.ReflectionParams else "true"};\n")

                if Cls.Bases:
                    S.write(f"        instance->BaseType = {Cls.Bases[0]}::GetStaticClass();\n")

                for Prop in Cls.Properties:
                    S.write(f'        PropertyInfo* prop{Prop.Name} = new PropertyInfo{{ '
                            f'"{Prop.Name}", offsetof({Cls.Name}, {Prop.Name}), sizeof({Prop.Type}), '
                            f'"{Prop.Type}" }};\n')
                    if Prop.GetterName or Prop.SetterName:
                        if Prop.GetterName:
                            # Getter/Setter lambdy – nie używamy offsetof do serializacji
                            S.write(f'        prop{Prop.Name}->GetterPtr = [](void* obj, void* outBuf) {{\n')
                            S.write(f'            *static_cast<{Prop.Type}*>(outBuf) = static_cast<{Cls.Name}*>(obj)->{Prop.GetterName}();\n')
                            S.write(f'        }};\n')
                        else:
                            S.write(f'        prop{Prop.Name}->GetterPtr = [](void* obj, void* outBuf) {{\n')
                            S.write(f'            *static_cast<{Prop.Type}*>(outBuf) = static_cast<{Cls.Name}*>(obj)->{Prop.Name};\n')
                            S.write(f'        }};\n')
                        if Prop.SetterName:
                            S.write(f'        prop{Prop.Name}->SetterPtr = [](void* obj, const void* buf) {{\n')
                            S.write(f'            static_cast<{Cls.Name}*>(obj)->{Prop.SetterName}(*static_cast<const {Prop.Type}*>(buf));\n')
                            S.write(f'        }};\n')
                        else:
                            S.write(f'        prop{Prop.Name}->SetterPtr = [](void* obj, const void* buf) {{\n')
                            S.write(f'            static_cast<{Cls.Name}*>(obj)->{Prop.Name} = *static_cast<const {Prop.Type}*>(buf);\n')
                            S.write(f'        }};\n')
                        # Scratch lifecycle so the editor can hand the accessors a
                        # properly constructed T (never raw malloc'd bytes — that is
                        # UB for non-trivial types).
                        S.write(f'        prop{Prop.Name}->ConstructScratch = []() -> void* {{ return new {Prop.Type}(); }};\n')
                        S.write(f'        prop{Prop.Name}->DestroyScratch = [](void* p) {{ delete static_cast<{Prop.Type}*>(p); }};\n')
                    S.write(f'        prop{Prop.Name}->SerializePtr = TypeSerializer<{Prop.Type}>::Serialize;\n')
                    S.write(f'        prop{Prop.Name}->DeserializePtr = TypeSerializer<{Prop.Type}>::Deserialize;\n')
                    S.write(f'        prop{Prop.Name}->EditorControlPtr = TypeSerializer<{Prop.Type}>::EditorControl;\n')
                    # Field-to-field copy for JSON-free object duplication (CopyReflectedProperties).
                    # if constexpr keeps types that cannot be copy-assigned from breaking the build —
                    # such a field is simply left as the freshly constructed object made it.
                    S.write(f'        prop{Prop.Name}->CopyPtr = [](const void* src, void* dst) {{\n')
                    # The alias matters: for a pointer property, writing "const {Type}*" textually
                    # would yield "const GameObject**" (const on the pointee) instead of
                    # "GameObject* const*" (const on the property), which does not compile.
                    S.write(f'            using PropT = {Prop.Type};\n')
                    S.write(f'            if constexpr (std::is_copy_assignable_v<PropT>)\n')
                    S.write(f'                *static_cast<PropT*>(dst) = *static_cast<const PropT*>(src);\n')
                    S.write(f'        }};\n')
                    if Prop.UuidForClass:
                        if Prop.UuidForClass in [C.Name for C in AllClasses]:
                            S.write(f'        prop{Prop.Name}->UuidForClass = {Prop.UuidForClass}::GetStaticClass();\n')
                        else:
                            print(f"Error: UuidFor '{Prop.UuidForClass}' dla '{Prop.Name}' w '{Cls.Name}' nie znaleziono.")
                    S.write(f'        instance->AddProperty(prop{Prop.Name});\n')

                if Cls.UuidProperty:
                    S.write(f'        instance->TypeUuidProp = instance->FindProperty("{Cls.UuidProperty.Name}");\n')

                S.write(f'        instance->SerializeToJson = [](void* obj) -> nlohmann::json {{\n')
                S.write(f'            {Cls.Name}* objPtr = static_cast<{Cls.Name}*>(obj);\n')
                S.write(f'            return TypeSerializer<TypeInfo*>::Serialize(instance, objPtr);\n')
                S.write(f'        }};\n')
                S.write(f'        instance->DeserializeFromJson = [](DeserializationContext* dc, const nlohmann::json& j) -> void* {{\n')
                S.write(f'            {Cls.Name}* objPtr = static_cast<{Cls.Name}*>(TypeSerializer<TypeInfo*>::Deserialize(dc, j, instance));\n')
                S.write(f'            return objPtr;\n')
                S.write(f'        }};\n')
                S.write(f"    }}\n")
                S.write(f"    return instance;\n")
                S.write(f"}}\n\n")

                if IsTypeDerivedFrom("EngineObject", Cls, AllClasses):
                    S.write(f"TypeInfo* {Cls.Name}::GetClass() {{ auto* pt = GetPythonType(); return pt ? pt : GetStaticClass(); }}\n\n")
                else:
                    S.write(f"TypeInfo* {Cls.Name}::GetClass() {{ return GetStaticClass(); }}\n\n")

                S.write(f"void Register_Reflection_{Cls.Name}() {{\n")
                S.write(f"    TypeInfo* info = {Cls.Name}::GetStaticClass();\n")
                S.write(f"    TypeRegistry::GetInstance()->AddType(info);\n")
                S.write(f"}}\n\n")

            # ── Enumy ─────────────────────────────────────────────────
            if FileEntry.Enums:
                S.write("namespace Plu {\n\n")
                for Enum in FileEntry.Enums:
                    QualName = f"{Enum.Namespace}::{Enum.Name}" if Enum.Namespace else Enum.Name
                    # ToString
                    S.write(f"String ToString({QualName} value) {{\n")
                    S.write(f"    switch(value) {{\n")
                    for V in Enum.Values:
                        S.write(f'        case {QualName}::{V.Name}: return "{V.Name}";\n')
                    S.write(f'        default: return "Unknown";\n')
                    S.write(f"    }}\n")
                    S.write(f"}}\n\n")

                    # FromString – template specialization
                    S.write(f"template<>\n")
                    S.write(f"{QualName} FromString<{QualName}>(const String& str) {{\n")
                    for V in Enum.Values:
                        S.write(f'    if (str == "{V.Name}") return {QualName}::{V.Name};\n')
                    S.write(f"    return static_cast<{QualName}>(-1);\n")
                    S.write(f"}}\n\n")
                S.write("} // namespace Plu\n\n")

                for Enum in FileEntry.Enums:
                    QualName   = f"{Enum.Namespace}::{Enum.Name}" if Enum.Namespace else Enum.Name
                    SizeofBase = f"sizeof({Enum.BaseType})" if Enum.BaseType else "sizeof(int)"
                    S.write(f"void Register_Reflection_{Enum.Name}() {{\n")
                    S.write(f'    auto* info = new Plu::EnumInfo("{Enum.Name}", {SizeofBase});\n')
                    for V in Enum.Values:
                        S.write(f'    info->AddValue("{V.Name}", static_cast<UInt64>({QualName}::{V.Name}));\n')
                    S.write(f"    Plu::TypeRegistry::GetInstance()->AddEnum<{QualName}>(info);\n")
                    S.write(f"}}\n\n")

        WriteIfChanged(GenSource, S.getvalue())

    # ── Generuj EditorAssetObjectsCreators.cpp (tylko gdy projekt Editor istnieje) ──
    if "Editor" in ProjectGroups and False:
        # Wczytaj wszystkie struktury ze wszystkich ClassList.txt
        AllStructs: List[StructInfoInternal] = []
        for Proj in ProjectGroups:
            ClassListFile = os.path.join(OutputDir, Proj, "ClassList.txt")
            if not os.path.exists(ClassListFile):
                continue
            with open(ClassListFile, "r", encoding="utf-8") as CL:
                for RawLine in CL:
                    Parts = RawLine.strip().split(" - ")
                    if len(Parts) < 4:
                        continue
                    EntryTypeStr = Parts[2].removeprefix("ClassType.")
                    if EntryTypeStr == "STRUCT":
                        AllStructs.append(StructInfoInternal(
                            Name     = Parts[0],
                            Bases    = ast.literal_eval(Parts[1]),
                            FilePath = Parts[3],
                        ))

        # Odfiltruj tylko te struktury które dziedziczą po IAssetData
        AssetStructs: List[StructInfoInternal] = [
            S for S in AllStructs if IsStructAsset(S, AllStructs)
        ]

        EditorAssetsPath = os.path.join(OutputDir, "Editor", "EditorAssetObjectsCreators.cpp")
        os.makedirs(os.path.dirname(EditorAssetsPath), exist_ok=True)
        EA = io.StringIO()
        EA.write('#include <PluEngine/Core/Reflection/ReflectionBase.h>\n')
        EA.write('#include <PluEngine/Core/Reflection/TypeTraits.h>\n\n')
        EA.write('#include "PluEngine/Core/Objects/EngineObjectManager.h"\n')
        EA.write('#include "Managers/Assets/EditorAssetManager.h"\n')
        EA.write('#include "Managers/Assets/EditorAssetObject.h"\n')
        for S in AssetStructs:
            EA.write(f'#include "{S.FilePath.replace(os.sep, "/")}"\n')
        EA.write('using namespace Plu;\n\n')
        EA.write('extern TUsePointer<EngineObjectManager> gEngineObjectManager;\n\n')
        EA.write('void InitEditorAssetObjectCreators()\n{\n')
        for S in AssetStructs:
            EA.write(
                f'    EditorTypeRegistry::GetInstance()->AddConstructor('
                f'{S.Name}::GetStaticClass()->TypeName, '
                f'[](TOwningPointer<IAssetData> info) -> TOwningPointer<IEditorAssetObject> {{\n'
            )
            EA.write(f'        EngineObjectHandle handle_{S.Name} = gEngineObjectManager->CreateObject<EditorAssetObject<{S.Name}>>();\n')
            EA.write(f'        TOwningPointer<EditorAssetObject<{S.Name}>> editorAssetObject = gEngineObjectManager->GetObjectAsOwner<IEditorAssetObject>(handle_{S.Name});\n')
            EA.write(f'        editorAssetObject->AssetInfo = StaticCast<{S.Name}>(info);\n')
            EA.write(f'        return editorAssetObject;\n')
            EA.write(f'    }});\n')
        EA.write('}\n')
        # Zawsze zapisuj – dane pochodzą z ClassList.txt (pełne), nie z Data (cache)
        Changed = WriteIfChanged(EditorAssetsPath, EA.getvalue())
        print(f"{'Generated' if Changed else 'Unchanged'} EditorAssetObjectsCreators.cpp with {len(AssetStructs)} asset struct(s).")

    # ── Zapisz EnumList.txt ───────────────────────────────────────────
    # Zbierz enumy per projekt żeby otworzyć każdy plik tylko raz
    EnumsByProj: dict = {}
    for F in Data:
        if not F.Enums:
            continue
        Proj = GetFileProject(F)
        EnumsByProj.setdefault(Proj, []).extend(F.Enums)

    for Proj, ProjEnums in EnumsByProj.items():
        EnumListFile = os.path.join(OutputDir, Proj, "EnumList.txt")
        os.makedirs(os.path.dirname(EnumListFile), exist_ok=True)
        ExistingLines: set = set()
        if not ForceMode and os.path.exists(EnumListFile):
            try:
                with open(EnumListFile, "r", encoding="utf-8") as EL:
                    ExistingLines = {L.strip() for L in EL if L.strip()}
            except OSError:
                pass
        FileMode = "w" if ForceMode else "a"
        with open(EnumListFile, FileMode, encoding="utf-8") as EL:
            for Enum in ProjEnums:
                if Enum.Name not in ExistingLines:
                    EL.write(f"{Enum.Name}\n")
                    ExistingLines.add(Enum.Name)

    # ── Generuj Init*Reflection.cpp ───────────────────────────────────
    for Proj in ProjectGroups:
        ClassListFile = os.path.join(OutputDir, Proj, "ClassList.txt")
        EnumListFile  = os.path.join(OutputDir, Proj, "EnumList.txt")
        ProjectClassList: List[List[str]] = []
        ProjectEnumList:  List[str]       = []

        if os.path.exists(ClassListFile):
            with open(ClassListFile, "r", encoding="utf-8") as CL:
                for RawLine in CL:
                    Parts = RawLine.strip().split(" - ")
                    if len(Parts) >= 3:
                        ProjectClassList.append([Parts[0], Parts[2]])

        if os.path.exists(EnumListFile):
            with open(EnumListFile, "r", encoding="utf-8") as EL:
                ProjectEnumList = [L.strip() for L in EL if L.strip()]

        if not ProjectClassList and not ProjectEnumList and not Proj.startswith("Plu"):
            continue  # projekt zawiera tylko globalne funkcje
        # Engine modules always get an Init<Module>Reflection(), even an empty one: Application
        # calls all of them in layer order, so a module that has nothing reflected yet must still
        # link, and gaining its first reflected class later must not need an edit on the call site.

        InitPath = os.path.join(OutputDir, Proj, f"Init{Proj}Reflection.cpp")
        I = io.StringIO()
        I.write("#include <PluEngine/Core/Reflection/ReflectionBase.h>\n\n")
        I.write(f"// Project: {Proj}\n\n")
        for Entry in ProjectClassList:
            if Entry[1].removeprefix("ClassType.") == "INTERFACE":
                continue
            I.write(f"extern void Register_Reflection_{Entry[0]}();\n")
        for EnumName in ProjectEnumList:
            I.write(f"extern void Register_Reflection_{EnumName}();\n")
        I.write("\n")
        if Proj == "Editor" and False:
            I.write("extern void InitEditorAssetObjectCreators();\n\n")
        if Proj in ["Editor", "Runtime"]:
            I.write(f"void Init{Proj}Reflection()\n")
        else:
            I.write(f"void {ProjectApiMacro(Proj)}Init{Proj}Reflection()\n")
        I.write("{\n")
        if Proj == "Editor" and False:
            I.write("    InitEditorAssetObjectCreators();\n")
        for Entry in ProjectClassList:
            if Entry[1].removeprefix("ClassType.") == "INTERFACE":
                continue
            I.write(f"    Register_Reflection_{Entry[0]}();\n")
        for EnumName in ProjectEnumList:
            I.write(f"    Register_Reflection_{EnumName}();\n")
        I.write("}\n")
        WriteIfChanged(InitPath, I.getvalue())

    print(f"Success! Generated reflection data for {len(Data)} file(s) in {OutputDir}")
    return AllClasses


# ─────────────────────────────────────────────
#  Punkt wejścia
# ─────────────────────────────────────────────

if __name__ == "__main__":
    Files = ScanProject()
    AllReflectionData: List[FileData] = []

    for FilePath in Files:
        # Wyznacz nazwę projektu (pierwszy subfolder względem korzenia projektu)
        try:
            ProjectParts = FilePath.relative_to(Path(os.path.dirname(ScriptDir))).parts
            # LibEngine is split into modules, each of which is its own project
            # so that its generated sources compile into its own library.
            ProjectName = (ProjectParts[1] if ProjectParts[0] == "LibEngine"
                           and len(ProjectParts) > 1 else ProjectParts[0])
        except ValueError:
            ProjectName = "Unknown"

        # Obsługa ProcessedList.txt (cache modyfikacji)
        ProjectOutputDir     = os.path.join(OutputDir, ProjectName)
        os.makedirs(ProjectOutputDir, exist_ok=True)
        ProcessedListPath    = os.path.join(ProjectOutputDir, "ProcessedList.txt")
        FileMtime            = str(os.path.getmtime(FilePath))
        FileName             = FilePath.name

        # Wczytaj istniejący cache
        CacheData: dict = {}
        if os.path.exists(ProcessedListPath):
            try:
                with open(ProcessedListPath, "r", encoding="utf-8") as PL:
                    CacheData = json.load(PL)
            except (json.JSONDecodeError, OSError):
                CacheData = {}

        FileChanged = ForceMode or BindingsMode or CacheData.get(FileName) != FileMtime

        if not FileChanged:
            if not QuietMode:
                print(f"Skipping {FileName} (not modified)")
            continue

        Types, GlobalFuncs, Enums = ProcessFile(FilePath, ProjectName)

        # Zaktualizuj wpis cache po udanym parsowaniu
        CacheData[FileName] = FileMtime
        try:
            with open(ProcessedListPath, "w", encoding="utf-8") as PL:
                PL.write(json.dumps(CacheData, indent=2))
        except OSError as E:
            print(f"Warning: nie udało się zapisać ProcessedList.txt: {E}")

        # Usuń z ClassList.txt klasy które zniknęły z tego pliku
        ClassListFile = os.path.join(OutputDir, ProjectName, "ClassList.txt")
        if os.path.exists(ClassListFile):
            try:
                with open(ClassListFile, "r", encoding="utf-8") as CL:
                    OldLines = CL.readlines()
                # Zachowaj linie które NIE pochodzą z tego pliku LUB nadal w nim są
                CurrentNames = {T.Name for T in Types}
                FilePathStr  = str(FilePath)
                NewLines = [
                    L for L in OldLines
                    if FilePathStr not in L or L.split(" - ")[0].strip() in CurrentNames
                ]
                if len(NewLines) != len(OldLines):
                    Removed = [L.split(" - ")[0].strip() for L in OldLines if L not in NewLines]
                    print(f"  Removed from ClassList: {Removed}")
                    with open(ClassListFile, "w", encoding="utf-8") as CL:
                        CL.writelines(NewLines)
            except OSError:
                pass

        # Usuń z EnumList.txt enumy które zniknęły z tego pliku
        EnumListFile = os.path.join(OutputDir, ProjectName, "EnumList.txt")
        if os.path.exists(EnumListFile):
            try:
                with open(EnumListFile, "r", encoding="utf-8") as EL:
                    OldEnumLines = EL.readlines()
                CurrentEnumNames = {E.Name for E in Enums}
                # EnumList.txt nie ma ścieżki – musimy porównać z tym co parser znalazł
                # Usuwamy tylko jeśli enum był w tym pliku a teraz go nie ma
                # Szukamy enumów które były w ClassList tego pliku
                FileEnumNames = set()
                for E_old in OldEnumLines:
                    Name = E_old.strip()
                    # Sprawdź czy ten enum był w tym pliku przez generated.cpp
                    GenSource = os.path.join(OutputDir, ProjectName, FilePath.stem + ".generated.cpp")
                    if os.path.exists(GenSource):
                        with open(GenSource, "r", encoding="utf-8") as GS:
                            if f"Register_Reflection_{Name}()" in GS.read():
                                FileEnumNames.add(Name)
                NewEnumLines = [
                    L for L in OldEnumLines
                    if L.strip() not in FileEnumNames or L.strip() in CurrentEnumNames
                ]
                if len(NewEnumLines) != len(OldEnumLines):
                    Removed = [L.strip() for L in OldEnumLines if L not in NewEnumLines]
                    print(f"  Removed enums from EnumList: {Removed}")
                    with open(EnumListFile, "w", encoding="utf-8") as EL:
                        EL.writelines(NewEnumLines)
            except OSError:
                pass

        if Types or GlobalFuncs or Enums:
            if Types:
                ParamsCheck(Types)
            AllReflectionData.append(FileData(FilePath=FilePath, Children=Types, GlobalFunctions=GlobalFuncs, Enums=Enums))
            for T in Types:
                print(f"  Found {T.Type.name}: {T.Name} in {FileName} (project: {T.Project})")
            for GF in GlobalFuncs:
                print(f"  Found GLOBAL FUNC: {GF.ReturnType} {GF.Name}(...) in {FileName}")
            for E in Enums:
                print(f"  Found ENUM: {E.Name} ({len(E.Values)} values) in {FileName}")

    ReflAllClasses = GenerateReflectionData(AllReflectionData)
    if BindingsMode:
        GeneratePybindBindings(AllReflectionData, ReflAllClasses or [])