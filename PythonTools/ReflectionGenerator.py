import clang.cindex
import os
import json
import shlex
import re
import json
from pathlib import Path
import sys
import EngineUtils
import argparse
from enum import Enum
from dataclasses import dataclass
import subprocess

class ClassType(Enum):
    CLASS = 1
    STRUCT = 2
    ENUM = 3
    INTERFACE = 4
    UNKNOWN = 5

# --- KONFIGURACJA ŚCIEŻEK ---
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
GURU_PROJECT_ROOT = PROJECT_ROOT
BUILD_DIR = os.path.join(PROJECT_ROOT, "cmake-build-debug")
DATABASE_FILE = os.path.join(BUILD_DIR, "compile_commands.json")
OUTPUT_FILE = os.path.join(PROJECT_ROOT, "ReflectionCache")
QUIET_MODE = False
FORCE_MODE = False

if sys.platform.__contains__("win32"):
    print("On Windows!")
    BUILD_DIR = os.path.join(PROJECT_ROOT, "cmake-build-debug")
    DATABASE_FILE = os.path.join(BUILD_DIR, "compile_commands.json")
    clang.cindex.Config.set_library_path(r"D:\ProgramsPlo\LLVM\bin")

argParser = argparse.ArgumentParser(prog="Reflection Generator", description="Generates reflection data for PluEngine")

argParser.add_argument("-q", "--quiet", action="store_true")
argParser.add_argument("-p","--project")
argParser.add_argument("-F","--force", action="store_true", help="Ignore ProcessedList.txt?")
argParser.add_argument("-d", "--data", help="Skip parsing and use existing data from JSON file", action="store_true")
argParser.add_argument("-f", "--file", help="Only process a single file", type=str)
args = argParser.parse_args()
QUIET_MODE = bool(args.quiet)
FORCE_MODE = bool(args.force)
DATA_ONLY_MODE = bool(args.data)
SINGLE_FILE = ""
if args.file:
    SINGLE_FILE = args.file
    print(f"SINGLE FILE MODE: {SINGLE_FILE}")

if DATA_ONLY_MODE:
    print("DATA ONLY MODE")


if SINGLE_FILE == "":
    try:
        SubprojectToReprocess = args.project
        if SubprojectToReprocess != "ALL":
            PROJECT_ROOT = os.path.join(PROJECT_ROOT, SubprojectToReprocess)
        else:
            print("ALL")
    except:
        SubprojectToReprocess = ""
        print("No project selected!")
        exit()

if QUIET_MODE:
    print("QUIET")

if FORCE_MODE:
    print("FORCE")

@dataclass
class PropertyInfo:
    name: str
    type: str
    params: list[str]
    #Extra info
    uuidForClass: str

@dataclass
class TypeInfo:
    name: str
    type: ClassType
    filepath: Path
    bases: list[str]
    project: str
    reflection_params: list[str]
    properties: list[PropertyInfo]
    uuid_property: PropertyInfo

@dataclass
class FileData:
    filePath: Path
    children: list[TypeInfo]

@dataclass
class StructInfoInternal:
    name: str
    bases: list[str]
    filepath: str

scanned_classes = []

def get_clang_resource_dir():
    try:
        return subprocess.check_output(["clang", "-print-resource-dir"]).decode().strip()
    except:
        return ""

def filter_args(args):
    # Dodajemy driver-mode, aby uniknąć błędów "linker input unused"
    filtered = ["--driver-mode=g++"]

    # Lista folderów, które ZAWSZE powinny być w Include
    extra_includes = [
        os.path.join(GURU_PROJECT_ROOT, "Engine", "include"),
        os.path.join(GURU_PROJECT_ROOT, "Editor"),
    ]

    for inc in extra_includes:
        filtered.append(f"-I{inc}")

    # Twoja dotychczasowa logika filtrowania...
    forbidden = ('-fmodules-ts', '-fmodule-mapper', '-fdeps-format', '-fpreprocessed', '-MT', '-MF', '-MD', '-MP')
    skip_next = False

    for arg in args:
        if skip_next:
            skip_next = False
            continue
        if arg in ('-MF', '-MT', '-o', '/Fo', '/Fd', '/c'):
            skip_next = True
            continue

        # Ignoruj flagi MSVC i stare standardy
        if arg.startswith(('/std:', '-std:', '/Zi', '/RTC', '/EH', '/nologo')):
            continue

        if not arg.startswith(forbidden) and not arg.startswith('/'):
            filtered.append(arg)

    # Wymuszenie nowoczesnego C++
    filtered.extend(["-x", "c++", "-std=c++20", "-Wno-everything", "-ferror-limit=0", "-DPLU_API="])

    res_dir = get_clang_resource_dir()
    if res_dir:
        filtered.append(f"-I{res_dir}/include")

    return filtered

file_cache = {}

def get_line_from_file(file_path, line_number):
    if file_path not in file_cache:
        with open(file_path, 'r', errors='ignore') as f:
            file_cache[file_path] = f.readlines()

    # line_number w Clangu jest od 1, w liście od 0
    idx = line_number - 1
    if 0 <= idx < len(file_cache[file_path]):
        return file_cache[file_path][idx]
    return ""

def has_macro_above(node, macro_name):
    """Sprawdza czy w liniach nad węzłem znajduje się makro (regex)"""
    file_path = node.location.file.name
    line_idx = node.location.line

    # Sprawdzamy do 3 linii w górę (na wypadek spacji/komentarzy)
    for i in range(1, 2):
        line = get_line_from_file(file_path, line_idx - i)
        if re.search(rf"{macro_name}\s*\(", line):
            return True
    return False

def get_compilation_map():
    if not os.path.exists(DATABASE_FILE):
        print(f"Błąd: Brak {DATABASE_FILE}. Uruchom CMake.")
        return {}

    with open(DATABASE_FILE, 'r') as f:
        db_json = json.load(f)

    folder_map = {}
    for entry in db_json:
        directory = os.path.dirname(entry['file'])
        if directory not in folder_map:
            args = entry['arguments'] if 'arguments' in entry else shlex.split(entry['command'])
            folder_map[directory] = filter_args(args[1:-1])
    return folder_map

def get_macro_params(node, macro_name):
    """Zwraca listę parametrów z makra nad węzłem lub None jeśli brak makra"""
    file_path = node.location.file.name
    line_idx = node.location.line

    # Przeszukujemy 3 linie nad deklaracją
    for i in range(1, 2):
        line = get_line_from_file(file_path, line_idx - i)
        # Szukamy: NAZWA_MAKRA( parametry )
        match = re.search(rf"{macro_name}\s*\((.*?)\)", line)
        if match:
            params_str = match.group(1).strip()
            if not params_str:
                return []
            # Dzielimy po przecinku i czyścimy spacje
            return [p.strip() for p in params_str.split(",")]

    return None # Brak makra

def isTypeDerivedFrom(base: TypeInfo, derived: TypeInfo, allTypes: list[TypeInfo]) -> bool:
    # Sprawdza czy derived dziedziczy po base
    if base is None:
        return
    for b in derived.bases:
        if b == base.name:
            return True
        # Rekurencja dla bazowych klas
        for t in allTypes:
            if t.name == b:
                if isTypeDerivedFrom(base, t, allTypes):
                    return True
    return False

engineObjectType: TypeInfo = None
interfaceType: TypeInfo = None

def find_reflection_data(node, file_path) -> list[TypeInfo]:
    global interfaceType, engineObjectType
    results = []

    for child in node.get_children():
        # Pomijaj pliki zewnętrzne
        if not child.location.file or child.location.file.name != file_path:
            continue

        # Szukamy Klas
        if child.kind == clang.cindex.CursorKind.CLASS_DECL and child.is_definition():
            # Sprawdzamy Regexem, czy nad klasą jest PLU_CLASS
            if has_macro_above(child, "PLU_CLASS"):
                print(f"   [OK] Found: {child.spelling}")

                params = get_macro_params(child, "PLU_CLASS")
                if params is not None:
                    print(f"        Reflection Params:")
                    print(f"        {params}")

                # Wyciągamy klasy bazowe z AST (to akurat Clang robi dobrze)
                bases = []
                for sub in child.get_children():
                    if sub.kind == clang.cindex.CursorKind.CXX_BASE_SPECIFIER:
                        bases.append(sub.type.spelling)

                classPath = Path(child.location.file.name)
                relative = classPath.relative_to(GURU_PROJECT_ROOT)
                firstSubfolder = relative.parts[0]

                print("        Bases: " + bases.__str__())

                newType = TypeInfo(
                    name=child.spelling,
                    bases=bases,
                    filepath=child.location.file.name,
                    project=firstSubfolder,
                    type=ClassType.CLASS,
                    reflection_params=list(params) if params is not None else [],
                    properties=[],
                    uuid_property=None
                )

                # Szukamy pól
                for member in child.get_children():
                    if member.kind == clang.cindex.CursorKind.FIELD_DECL:
                        if has_macro_above(member, "PLU_PROPERTY"):
                            params = get_macro_params(member, "PLU_PROPERTY")
                            if params is not None:
                                print(f"        Field - {member.spelling}: {params}")
                            newProp: PropertyInfo = PropertyInfo(
                                name=member.spelling,
                                type=member.type.spelling,
                                params= params if params is not None else [],
                                uuidForClass=""
                            )
                            newType.properties.append(newProp)
                if len(newType.bases) == 0:
                    if newType.name == "EngineObject":
                        print("   [INFO] Found EngineObject base class.")
                        engineObjectType = newType
                    else:
                        print(f"Error: Class {newType.name} has no bases.")
                results.append(newType)
            elif has_macro_above(child, "PLU_INTERFACE"):
                print(f"   [OK] Found Interface: {child.spelling}")
                bases = []
                for sub in child.get_children():
                    if sub.kind == clang.cindex.CursorKind.CXX_BASE_SPECIFIER:
                        bases.append(sub.type.spelling)
                classPath = Path(child.location.file.name)
                relative = classPath.relative_to(GURU_PROJECT_ROOT)
                firstSubfolder = relative.parts[0]
                newType = TypeInfo(
                    name=child.spelling,
                    bases=bases,
                    filepath=child.location.file.name,
                    project=firstSubfolder,
                    type=ClassType.INTERFACE,
                    reflection_params=get_macro_params(child, "PLU_INTERFACE") or [],
                    properties=[],
                    uuid_property=None
                )
                if len(newType.bases) == 0:
                    if newType.name == "PluInterface":
                        print("   [INFO] Found PluInterface base class.")
                        interfaceType = newType
                    else:
                        print(f"Error: Class {newType.name} has no bases.")
                results.append(newType)

        if child.kind == clang.cindex.CursorKind.STRUCT_DECL and child.is_definition():
            # Sprawdzamy Regexem, czy nad strukturą jest PLU_CLASS
            if has_macro_above(child, "PLU_STRUCT"):
                print(f"   [OK] Found Struct: {child.spelling}")

                params = get_macro_params(child, "PLU_STRUCT")
                if params is not None:
                    print(f"        Reflection Params:")
                    print(f"        {params}")

                # Wyciągamy klasy bazowe z AST (to akurat Clang robi dobrze)
                bases = []
                for sub in child.get_children():
                    if sub.kind == clang.cindex.CursorKind.CXX_BASE_SPECIFIER:
                        bases.append(sub.type.spelling)

                classPath = Path(child.location.file.name)
                relative = classPath.relative_to(GURU_PROJECT_ROOT)
                firstSubfolder = relative.parts[0]

                print("        Bases: " + bases.__str__())

                newType = TypeInfo(
                    name=child.spelling,
                    bases=bases,
                    filepath=child.location.file.name,
                    project=firstSubfolder,
                    type=ClassType.STRUCT,
                    reflection_params=list(params) if params is not None else [],
                    properties=[],
                    uuid_property=None
                )

                # Szukamy pól
                for member in child.get_children():
                    if member.kind == clang.cindex.CursorKind.FIELD_DECL:
                        if has_macro_above(member, "PLU_PROPERTY"):
                            params = get_macro_params(member, "PLU_PROPERTY")
                            if params is not None:
                                print(f"        Field - {member.spelling}: {params}")
                            newProp: PropertyInfo = PropertyInfo(
                                name=member.spelling,
                                type=member.type.spelling,
                                params= params if params is not None else [],
                                uuidForClass=""
                            )
                            newType.properties.append(newProp)
                results.append(newType)
        # Rekurencja dla namespace'ów
        results.extend(find_reflection_data(child, file_path))

    return results

def params_check(data: list[TypeInfo]):
    for cls in data:
        for prop in cls.properties:
            if prop.type.endswith("*") or prop.type.endswith("&"):
                print(f"Warning: Property {prop.name} in class {cls.name} is a pointer or reference ({prop.type}). Reflection may not work correctly.")
            if "std::vector" in prop.type or "std::map" in prop.type or "std::unordered_map" in prop.type:
                print(f"Warning: Property {prop.name} in class {cls.name} is a STD STL container ({prop.type}). Use containers from PluEngine PluSTL library.")
            if "std::shared_ptr" in prop.type or "std::unique_ptr" in prop.type:
                print(f"Warning: Property {prop.name} in class {cls.name} is a STD smart pointer ({prop.type}). Use smart pointers from PluEngine PluSTL library.")
            if "glm::" in prop.type:
                print(f"Warning: Property {prop.name} in class {cls.name} is a GLM type ({prop.type}). Use math types defined in PluEngine PluTypes.h header.")
            if prop.type == "auto":
                print(f"Error: Property {prop.name} in class {cls.name} has type 'auto'. Reflection cannot deduce types from 'auto'.")
            for param in prop.params:
                if param.startswith("UuidFor="):
                    uuidForClass = param.split("=")[1]
                    if prop.type != "PluUUID" and prop.type != "Plu::PluUUID":
                        print(f"Error: Property {prop.name} in class {cls.name} is marked as UuidFor={uuidForClass} but has type '{prop.type}', expected 'PluUUID'.")
                    else:   
                        print(f"Info: Property {prop.name} in class {cls.name} is a valid UUID property for class '{uuidForClass}'.")
                        prop.uuidForClass = uuidForClass
                    
        #Class params
        if cls.reflection_params:
            for param in cls.reflection_params:
                if param.startswith("UUID="):
                    uuidProp = param.split("=")[1]
                    if not any(p.name == uuidProp for p in cls.properties):
                        print(f"Error: Class {cls.name} has UUID parameter referencing non-existent property '{uuidProp}'.")
                    else:
                        uuidPropType = next(p.type for p in cls.properties if p.name == uuidProp)
                        if uuidPropType != "PluUUID" and uuidPropType != "Plu::PluUUID":
                            print(f"Error: Class {cls.name} has UUID parameter referencing property '{uuidProp}' of type '{uuidPropType}', expected 'PluUUID'.")
                        else:
                            print(f"Info: Class {cls.name} has valid UUID property '{uuidProp}' of type '{uuidPropType}'.")
                            for p in cls.properties:
                                if p.name == uuidProp:
                                    cls.uuid_property = p

def generated_synthetic_cpp():
    print(f"Started Scanning for Headers in: {PROJECT_ROOT}")

    reflectionHeaders = []

    for root, dirs, files in os.walk(PROJECT_ROOT):
        if "cmake-build" in root or ".git" in root: continue

        for file in files:
            if file.endswith(('.h', '.hpp')):
                full_path = os.path.abspath(os.path.join(root, file))

                # Szybki filtr wstępny
                with open(full_path, 'r', errors='ignore') as f:
                    data = f.read()
                    if not ("PLU_CLASS" in data or "PLU_STRUCT" in data): continue

                reflectionHeaders.append(full_path)
    synthetic_cpp_path = os.path.join(OUTPUT_FILE, "SyntheticReflectionIncludes.cpp")
    with open(synthetic_cpp_path, "w") as f:
        f.write("// This file is auto-generated to include all reflection headers.\n")
        f.write("// It ensures that all reflection macros are processed by the compiler.\n\n")
        for header in reflectionHeaders:
            relative_path = os.path.relpath(header, PROJECT_ROOT)
            relative_path = relative_path.removeprefix("Editor/")
            relative_path = relative_path.removeprefix("Engine/include/")
            f.write(f'#include "{relative_path.replace(os.sep, "/")}"\n')
    return synthetic_cpp_path
    
def process_project():
    if DATA_ONLY_MODE:
        GenerateReflectionData([])
        return
    
    if SINGLE_FILE != "":
        print(f"Processing single file: {SINGLE_FILE}")
        folder_map = get_compilation_map()
        if not folder_map:
            return

        index = clang.cindex.Index.create()
        all_reflection_data = []

        full_path = Path(os.path.abspath(SINGLE_FILE))
        root = os.path.dirname(full_path)

        args = folder_map.get(root, next(iter(folder_map.values())))
        tu = index.parse(full_path, args=args)

        #print any errors
        for diag in tu.diagnostics:
            print(diag.severity, diag.location, diag.spelling)

        data = find_reflection_data(tu.cursor, full_path)
        if data:
            all_reflection_data.append(FileData(full_path, data))
        params_check(data)

        GenerateReflectionData(all_reflection_data)
        return

    folder_map = get_compilation_map()
    if not folder_map: return

    index = clang.cindex.Index.create()
    all_reflection_data: list[FileData] = []

    print(f"Started Scanning in: {PROJECT_ROOT}")

    for root, dirs, files in os.walk(PROJECT_ROOT):
        if "cmake-build" in root or ".git" in root: continue

        for file in files:
            if file.endswith(('.h', '.hpp', '.cpp')):
                full_path = os.path.abspath(os.path.join(root, file))

                # Szybki filtr wstępny
                with open(full_path, 'r', errors='ignore') as f:
                    data = f.read()
                    if not ("PLU_CLASS" in data or "PLU_STRUCT" in data or "PLU_INTERFACE" in data): continue

                classPath = Path(full_path)
                relative = classPath.relative_to(GURU_PROJECT_ROOT)
                firstSubfolder = relative.parts[0]
                pathToProcessedList = os.path.join(OUTPUT_FILE, firstSubfolder, "ProcessedList.txt")

                # Upewnij się, że folder istnieje
                os.makedirs(os.path.join(OUTPUT_FILE, firstSubfolder), exist_ok=True)

                if not FORCE_MODE:
                    if os.path.exists(pathToProcessedList):
                        with open(pathToProcessedList, "r+") as f:
                            try:
                                data = json.load(f)
                            except json.JSONDecodeError:
                                data = {}
                            file_mod_time = EngineUtils.modify_date(full_path)
                            if data.get(file) == file_mod_time:
                                if not QUIET_MODE: print(f"Skipping {file}")
                                continue
                            else:
                                data[file] = file_mod_time
                                f.seek(0)
                                f.write(json.dumps(data, indent=2))
                                f.truncate()
                    else:
                        # Plik nie istnieje, tworzymy nowy
                        data = {file: EngineUtils.modify_date(full_path)}
                        with open(pathToProcessedList, "w") as f:
                            f.write(json.dumps(data, indent=2))

                if not QUIET_MODE: print(f"Parsing: {file}...")
                args = folder_map.get(root, next(iter(folder_map.values())))
                tu = index.parse(full_path, args=args)

                #print any errors
                for diag in tu.diagnostics:
                    print(diag.severity, diag.location, diag.spelling)

                data = find_reflection_data(tu.cursor, full_path)
                if data:
                    params_check(data)
                    newFileData = FileData(Path(full_path), data)
                    for cls in data:
                        print(f"  Found class: {cls.name} in file {file} project {cls.project}")
                    all_reflection_data.append(newFileData)

    GenerateReflectionData(all_reflection_data)

def isStructAsset(cls: StructInfoInternal, allClasses: list[StructInfoInternal]) -> bool:
    # Sprawdza czy klasa dziedziczy po Asset lub StructAsset
    for base in cls.bases:
        for c in allClasses:
            if c.name == base:
                if c.name == "IAssetInfo":
                    return True
                else:
                    return isStructAsset(c, allClasses)
    return False

def GenerateReflectionData(data: list[FileData]):
    # Grupowanie klas według projektów dla plików Init
    project_groups: list[str] = []

    if len(data) == 0 and not DATA_ONLY_MODE:
        print("No reflection changes")
        return
    
    if len(data) > 0:
        print(f"Generating Reflection Data for {len(data)} files...")

    if DATA_ONLY_MODE:
        for root, dirs, files in os.walk(OUTPUT_FILE):
            for dir in dirs:
                project_groups.append(dir)

    for f in data:
        # Ścieżki plików
        proj = f.children[0].project
        if proj not in project_groups:
            project_groups.append(proj)
    if FORCE_MODE:
        for project in project_groups:
            pathToProcessedList = os.path.join(OUTPUT_FILE, project, "ClassList.txt")
            if os.path.exists(pathToProcessedList):
                os.remove(pathToProcessedList)
    for f in data:
        proj = f.children[0].project
        projectClassListFile = os.path.join(OUTPUT_FILE, proj, "ClassList.txt")

        with open(projectClassListFile, "a") as classListFile:
            for cls in f.children:
                if cls.name not in open(projectClassListFile).read() or FORCE_MODE:
                    classListFile.write(f"{cls.name} - {cls.bases} - {cls.type} - {cls.filepath}\n")

    print("Found projects: " + project_groups.__str__())

    ALL_CLASSES: list[TypeInfo] = []
    for proj in project_groups:
        projectClassListFile = os.path.join(OUTPUT_FILE, proj, "ClassList.txt")
        with open(projectClassListFile, "r") as classListFile:
            for line in classListFile:
                classEntry = line.strip()
                className = classEntry.split(" - ")[0]
                classBasesStr = classEntry.split(" - ")[1]
                classType = classEntry.split(" - ")[2]
                classFilePath = classEntry.split(" - ")[3]

                basesArr = eval(classBasesStr)
                ALL_CLASSES.append(TypeInfo(
                    name=className,
                    type=ClassType[classType.removeprefix("ClassType.")],
                    filepath=Path(classFilePath),
                    bases=basesArr,
                    project=proj,
                    reflection_params=[],
                    properties=[],
                    uuid_property=None
                ))
    for f in data:
        for cls in f.children:
            if len(cls.bases) > 1:
                for idx, base in enumerate(cls.bases):
                    if idx == 0:
                        continue
                    if isTypeDerivedFrom(interfaceType, cls, ALL_CLASSES):
                        print(f"Info: Class {cls.name} implements interface {base}, removing from bases.")
                        cls.bases.remove(base)


    for file in data:
        filePath = file.filePath
        proj = file.children[0].project
        fileGeneratedHeader = os.path.join(OUTPUT_FILE, proj, filePath.stem + ".generated.h")
        fileGeneratedSource = os.path.join(OUTPUT_FILE, proj, filePath.stem + ".generated.cpp")
    

        # --- GENEROWANIE .h ---
        with open(fileGeneratedHeader, "w") as f:
            f.write("#pragma once\n")
            f.write("#include <PluEngine/Reflection/ReflectionBase.h>\n\n")

            for cls in file.children:
                isStruct = cls.type == ClassType.STRUCT
                # Makro musi mieć unikalną nazwę na klasę lub być generyczne bez średnika
                fcls: str = cls.name
                fcls = fcls.upper()
                f.write(f"#define REFLECTION_BODY_{fcls}() \\\n")
                f.write(f"    public: \\\n")
                f.write(f"        static Plu::TypeInfo* GetStaticClass(); \\\n")
                if not isStruct:
                    f.write(f"        virtual Plu::TypeInfo* GetClass() override; \\\n")
                else:
                    f.write(f"        Plu::TypeInfo* GetClass(); \\\n")
                f.write(f"    private: \\\n")
                if not isStruct:
                    f.write(f"        friend void Register_Reflection_{cls.name}();\n")
                else:
                    f.write(f"        friend void Register_Reflection_{cls.name}(); \\\n")
                    f.write(f"        public:\n")

        # --- GENEROWANIE .cpp ---
        uuidProps = {}
        for cls in file.children:
            for prop in cls.properties:
                if prop.uuidForClass != "":
                    uuidClassPath = ""
                    for c in ALL_CLASSES:
                        if c.name == prop.uuidForClass:
                            uuidClassPath = c.filepath
                    uuidProps[prop.name] = {
                        "class": prop.uuidForClass,
                        "classPath": uuidClassPath
                    }
        with open(fileGeneratedSource, "w") as f:
            f.write(f'#include "{file.filePath}"\n')
            f.write("#include <PluEngine/Reflection/TypeTraits.h>\n\n")
            writtenIncludes = set()
            for uuidProp, info in uuidProps.items():
                if info["classPath"] not in writtenIncludes:
                    writtenIncludes.add(info["classPath"])
                    f.write(f'#include "{info["classPath"]}"\n')
            f.write(f'#include "{filePath.stem}.generated.h"\n\n')

            for cls in file.children:
                for prop in cls.properties:
                    if prop.type.startswith("Plu::TUsePointer<") or prop.type.startswith("TUsePointer<"):
                        for c in ALL_CLASSES:
                            if c.name == prop.type.replace("Plu::TUsePointer<","").replace("TUsePointer<","").replace(">",""):
                                if c.filepath not in writtenIncludes:
                                    writtenIncludes.add(c.filepath)
                                    f.write(f'#include "{c.filepath}"\n')

            f.write(f"using namespace Plu;\n\n")

            for cls in file.children:
                # 1. Implementacja StaticClass
                if cls.type == ClassType.INTERFACE:
                    print("Skipping interface reflection")
                    continue
                f.write(f"TypeInfo* {cls.name}::GetStaticClass() {{\n")
                f.write(f"    static TypeInfo* instance = nullptr;\n")
                f.write(f"    if (!instance) {{\n")
                f.write(f'        instance = new TypeInfo(sizeof({cls.name}), "{cls.name}", TypeType::{cls.type.name});\n')
                if (not cls.reflection_params or "Abstract" not in cls.reflection_params) and cls.type != ClassType.INTERFACE:
                    f.write(f"        instance->Constructor = []() -> void* {{ return new {cls.name}(); }};\n")
                if len(cls.bases) > 0:
                    f.write(f"        instance->BaseType = {cls.bases[0]}::GetStaticClass();\n")

                for prop in cls.properties:
                    # Tutaj dodajemy właściwości do TypeInfo
                    f.write(f'        PropertyInfo* prop{prop.name} = new PropertyInfo{{ "{prop.name}", offsetof({cls.name}, {prop.name}), sizeof({prop.type}), PropertyType::Unknown, "{prop.type}" }};\n')
                    f.write(f'        prop{prop.name}->SerializePtr = TypeSerializer<{prop.type}>::Serialize;\n')
                    f.write(f'        prop{prop.name}->DeserializePtr = TypeSerializer<{prop.type}>::Deserialize;\n')
                    f.write(f'        prop{prop.name}->EditorControlPtr = TypeSerializer<{prop.type}>::EditorControl;\n')
                    if prop.uuidForClass != "":
                        if prop.uuidForClass in [c.name for c in ALL_CLASSES]:
                            f.write(f'        prop{prop.name}->UuidForClass = {prop.uuidForClass}::GetStaticClass();\n')
                        else:
                            print(f"Error: UuidFor class '{prop.uuidForClass}' for property '{prop.name}' in class '{cls.name}' not found among processed classes.")
                    f.write(f'        instance->AddProperty(prop{prop.name});\n')
                if cls.uuid_property is not None:
                    f.write(f'        instance->TypeUuidProp = instance->FindProperty("{cls.uuid_property.name}");\n')
                f.write(f'        instance->SerializeToJson = [](void* obj) -> nlohmann::json {{\n')
                f.write(f'            {cls.name}* objPtr = static_cast<{cls.name}*>(obj);\n')
                f.write(f'            return TypeSerializer<TypeInfo*>::Serialize(instance,objPtr);\n')
                f.write(f'        }};\n')
                f.write(f'        instance->DeserializeFromJson = [](DeserializationContext* dc, nlohmann::json& j) -> void* {{\n')
                f.write(f'            {cls.name}* objPtr = static_cast<{cls.name}*>(TypeSerializer<TypeInfo*>::Deserialize(dc, j, instance));\n')
                f.write(f'            return objPtr;\n')
                f.write(f'        }};\n')
                f.write(f"    }}\n")
                f.write(f"    return instance;\n")
                f.write(f"}}\n\n")

                f.write(f"TypeInfo* {cls.name}::GetClass() {{ return GetStaticClass(); }}\n\n")

                # 2. Definicja funkcji rejestrującej (To naprawia błąd linkowania!)
                f.write(f"void Register_Reflection_{cls.name}() {{\n")
                f.write(f"    TypeInfo* info = {cls.name}::GetStaticClass();\n")
                f.write(f"    TypeRegistry::GetInstance()->AddType(info);\n")
                f.write(f"}}\n")

    # Generating EditorAssetObject creation wrappers
    

    assets_structs: list[StructInfoInternal] = []
    all_structs: list[StructInfoInternal] = []

    if "Editor" in project_groups:
        for line in open(os.path.join(OUTPUT_FILE, "Engine", "ClassList.txt"), "r"):
            classEntry = line.strip()
            className = classEntry.split(" - ")[0]
            classBasesStr = classEntry.split(" - ")[1]
            classType = classEntry.split(" - ")[2]
            classFilePath = classEntry.split(" - ")[3]

            if classType.__contains__("STRUCT"):
                basesArr = eval(classBasesStr)
                all_structs.append(StructInfoInternal(
                    name=className,
                    bases=basesArr,
                    filepath=classFilePath
                ))

        for struct in all_structs:
            if isStructAsset(struct, all_structs):
                assets_structs.append(struct)
            
        editor_assets_path = os.path.join(OUTPUT_FILE, "Editor", "EditorAssetObjectsCreators.cpp")
        with open(editor_assets_path, "w") as f:
            f.write('#include <PluEngine/Reflection/ReflectionBase.h>\n')
            f.write('#include <PluEngine/Reflection/TypeTraits.h>\n\n')
            f.write('#include "PluEngine/Objects/EngineObjectManager.h"\n')
            f.write('#include "Managers/Assets/EditorAssetManager.h"\n')
            f.write('#include "Managers/Assets/EditorAssetObject.h"\n')
            for structPath in assets_structs:
                f.write(f'#include "{structPath.filepath.replace(os.sep, "/")}"\n')
            f.write('using namespace Plu;\n\n')
            f.write('extern TUsePointer<EngineObjectManager> gEngineObjectManager;\n\n') 
            f.write(f'void InitEditorAssetObjectCreators() {{\n\n')
            for structName in assets_structs:
                #EditorTypeRegistry::GetInstance()->AddConstructor();
                f.write(f'    EditorTypeRegistry::GetInstance()->AddConstructor({structName.name}::GetStaticClass()->TypeName, [](TOwningPointer<IAssetInfo> info) -> TOwningPointer<IEditorAssetObject> {{ \n')
                f.write(f'        EngineObjectHandle handle_{structName.name} = gEngineObjectManager->CreateObject<EditorAssetObject<{structName.name}>>();\n')
                f.write(f'        TOwningPointer<EditorAssetObject<{structName.name}>> editorAssetObject = gEngineObjectManager->GetObjectAsOwner<IEditorAssetObject>(handle_{structName.name});\n')
                f.write(f'        editorAssetObject->AssetInfo = StaticCast<{structName.name}>(info);\n')
                f.write(f'        return editorAssetObject;\n')
                f.write(f'    }});\n')
            f.write('}\n')

    for project in project_groups:
        projectClassListFile = os.path.join(OUTPUT_FILE, project, "ClassList.txt")
        projectClassList: list[list[str]] = []
        with open(projectClassListFile, "r") as classListFile:
            for line in classListFile:
                projectClassList.append([line.strip().split(" - ")[0], line.strip().split(" - ")[2]])
        init_path = os.path.join(OUTPUT_FILE, project, f"Init{project}Reflection.cpp")
        with open(init_path, "w") as f:
            f.write("#include <PluEngine/Reflection/ReflectionBase.h>\n\n")
            f.write(f"// Project: {project}\n\n")
            for cls in projectClassList:
                if cls[1].removeprefix("ClassType.") == "INTERFACE":
                    continue
                f.write(f"extern void Register_Reflection_{cls[0]}();\n")
            f.write("\n")
            if project == "Editor":
                f.write("extern void InitEditorAssetObjectCreators();\n\n")
            f.write("\n")
            if project == "Editor":
                f.write(f"void Init{project}Reflection()\n")
            else:
                f.write(f"void PLU_API Init{project}Reflection()\n")
            f.write("{\n")
            if project == "Editor":
                f.write("InitEditorAssetObjectCreators();\n")
            for cls in projectClassList:
                if cls[1].removeprefix("ClassType.") == "INTERFACE":
                    continue
                f.write(f"    Register_Reflection_{cls[0]}();\n")
            f.write("}\n")
        

    print(f"Success! Generated Reflection data for {len(data)} files in {OUTPUT_FILE}")

if __name__ == "__main__":
    process_project()