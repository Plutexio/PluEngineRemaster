#!/usr/bin/env python3
"""
Layering check for the LibEngine module split.

LibEngine is split into layered modules (LibEngine/{Module}/{include,src}). A module may include
headers from the layers strictly below it and from nowhere else — not from a layer above, and not
from a sibling on its own level.

Until every module becomes its own shared library, all of them share one include path, so nothing
stops an upward include from compiling. This script is that check in the meantime: it walks every
`#include "PluEngine/..."` in the tree, resolves the owning module from the first path segment,
and reports every edge that points at or above its own layer.

BASELINE below is the ratchet. A module at 0 is closed: any new upward include from it fails the
check. A module above 0 still has known work left (see the migration plan); lower its number as
those includes get cut, never raise it.

Usage:
    Python/venv-linux/bin/python PythonTools/LayeringCheck.py        # report + exit code
    Python/venv-linux/bin/python PythonTools/LayeringCheck.py -q     # totals only
    Python/venv-linux/bin/python PythonTools/LayeringCheck.py --update-baseline
"""

import argparse
import os
import re
import sys
from collections import defaultdict

ScriptDir  = os.path.dirname(os.path.abspath(__file__))
ProjectDir = os.path.dirname(ScriptDir)
LibDir     = os.path.join(ProjectDir, "LibEngine")

# Module -> layer. Lower may not depend on higher; equal may not depend on equal.
Layers = {
    "PluCore": 0,
    "PluPlatform": 1, "PluAssetCore": 1,
    "PluAssetTypes": 2,
    "PluEffects": 3, "PluScripting": 3,
    "PluRender": 4, "PluAssetPipeline": 4,
    "PluGameplay": 5,
    # Above gameplay since the physics rework: PhysicsWorld builds bodies from gameplay components
    # and listens to scene events, while gameplay never names physics.
    "PluPhysics": 6,
    "PluApp": 7,
}

# First path segment under PluEngine/ -> owning module.
Owners = {
    "Core": "PluCore", "Platform": "PluPlatform",
    # Public but platform-gated: the reflection generator skips /Platforms/<OS>/ for other systems.
    "Platforms": "PluPlatform", "AssetCore": "PluAssetCore",
    "AssetTypes": "PluAssetTypes", "Render": "PluRender", "Physics": "PluPhysics",
    "Scripting": "PluScripting", "AssetPipeline": "PluAssetPipeline",
    "Gameplay": "PluGameplay", "Effects": "PluEffects",
}

# Root headers kept at PluEngine/<name>.h rather than under a module directory.
RootHeaderOwners = {"Application.h": "PluApp"}
RootHeaderDefault = "PluCore"

# Known-remaining upward includes per module. 0 means closed — keep it there.
Baseline = {
    "PluCore": 0,
    "PluAssetCore": 0,
    "PluPlatform": 0,
    "PluAssetTypes": 0,
    "PluEffects": 0,
    "PluScripting": 0,
    "PluRender": 0,
    "PluAssetPipeline": 0,
    "PluGameplay": 0,
    "PluPhysics": 0,
    "PluApp": 0,
}

IncludePattern = re.compile(r'#include\s*[<"]PluEngine/([^">]+)[">]')


def OwnerOf(IncludePath: str) -> str:
    """Module that owns a header, given its path below PluEngine/."""
    Segment = IncludePath.split("/")[0]
    if Segment in Owners:
        return Owners[Segment]
    return RootHeaderOwners.get(IncludePath, RootHeaderDefault)


def CollectViolations() -> dict:
    """(fromModule, toModule) -> [(file, includePath), ...] for every non-downward edge."""
    Violations = defaultdict(list)
    for Module in Layers:
        ModuleDir = os.path.join(LibDir, Module)
        if not os.path.isdir(ModuleDir):
            print(f"WARNING: module directory missing: {ModuleDir}")
            continue
        for DirPath, _, Files in os.walk(ModuleDir):
            for FileName in Files:
                if not FileName.endswith((".h", ".cpp", ".inl")):
                    continue
                FullPath = os.path.join(DirPath, FileName)
                try:
                    Content = open(FullPath, encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                for Match in IncludePattern.finditer(Content):
                    Target = OwnerOf(Match.group(1))
                    if Target == Module:
                        continue
                    if Layers[Target] >= Layers[Module]:
                        Violations[(Module, Target)].append(
                            (os.path.relpath(FullPath, ProjectDir), Match.group(1)))
    return Violations


def Main() -> int:
    Parser = argparse.ArgumentParser(prog="Layering Check",
                                     description="Checks the LibEngine module layering")
    Parser.add_argument("-q", "--quiet", action="store_true", help="Totals only, no file list")
    Parser.add_argument("--update-baseline", action="store_true",
                        help="Print a Baseline dict matching the current tree")
    Args = Parser.parse_args()

    Violations = CollectViolations()
    PerModule = defaultdict(int)
    for (Source, _), Items in Violations.items():
        PerModule[Source] += len(Items)

    if Args.update_baseline:
        print("Baseline = {")
        for Module in sorted(Layers, key=lambda M: (Layers[M], M)):
            print(f'    "{Module}": {PerModule[Module]},')
        print("}")
        return 0

    if not Args.quiet and Violations:
        for (Source, Target), Items in sorted(
                Violations.items(), key=lambda KV: (Layers[KV[0][0]], -len(KV[1]))):
            Direction = "sideways" if Layers[Target] == Layers[Source] else "upward"
            print(f"\n  {Source} -> {Target}  [{len(Items)}, {Direction}]")
            for File, Include in sorted(set(Items)):
                print(f"      {File}")
                print(f"          -> PluEngine/{Include}")

    print(f"\n{'MODULE':<20}{'FOUND':>7}{'ALLOWED':>9}")
    Regressions = []
    for Module in sorted(Layers, key=lambda M: (Layers[M], M)):
        Found, Allowed = PerModule[Module], Baseline.get(Module, 0)
        if Found > Allowed:
            Status, Regressions = "  REGRESSION", Regressions + [(Module, Found, Allowed)]
        elif Found < Allowed:
            Status = "  improved — lower the baseline"
        elif Found == 0:
            Status = "  closed"
        else:
            Status = ""
        print(f"L{Layers[Module]} {Module:<17}{Found:>6}{Allowed:>9}{Status}")
    print(f"{'TOTAL':<20}{sum(PerModule.values()):>7}{sum(Baseline.values()):>9}")

    if Regressions:
        print("\nFAILED: a module gained upward includes it is not allowed to have.")
        for Module, Found, Allowed in Regressions:
            print(f"  {Module}: {Found} > {Allowed}")
        print("Cut the include, or move the code to the layer it belongs in.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(Main())
