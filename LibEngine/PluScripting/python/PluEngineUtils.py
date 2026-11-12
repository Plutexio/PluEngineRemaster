"""Pure-Python part of the PluEngine module.

Compiled into PluScripting at build time (cmake/PluEmbedPython.cmake) and merged into the PluEngine
module when it is imported (Plu::InstallPythonUtils): every name listed in __all__ becomes
PluEngine.<name>. Type stubs for these names are in PluEngineUtils.pyi next to this file.

This runs while the PluEngine module is still being initialised, so it must not import PluEngine
at module level: that would re-enter the module init.
"""

__all__ = ["Property"]


class Property:
    """A typed field of a Python engine class, with the metadata the engine needs to expose it.

    Declare it in the class body, not in __init__:

        class Turret(PluEngine.GameObject):
            Range = PluEngine.Property(25.0, "How far the turret sees, in metres", Min=0.0)

            def OnTick(self, deltaTime):
                if self.DistanceToPlayer() < self.Range: ...

    Instances read and write it like a plain attribute; assigning converts to Type. Every instance
    starts with its own copy of Default. Type is taken from Default unless given.

    The reflection collects these (PythonPropertyInfo on the class's TypeInfo), but does not handle
    the value yet: for now a Property is not edited in the editor, saved with the scene or copied
    by PIE.
    """

    # Marker for the engine side to recognise a property by, without naming this class.
    __plu_property__ = True

    def __init__(self, Default, Tooltip="", Type=None, Min=None, Max=None):
        if Default is None:
            raise TypeError("PluEngine.Property needs a default value")
        self.Name = None
        self.Type = Type if Type is not None else type(Default)
        self.Default = self._Convert(Default)
        self.Tooltip = Tooltip
        self.Min = Min
        self.Max = Max

    def __set_name__(self, owner, name):
        self.Name = name

    def _Convert(self, value):
        # An exact type check: bool is an int subclass, and an int property must not keep a bool.
        return value if type(value) is self.Type else self.Type(value)

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        values = instance.__dict__
        try:
            return values[self.Name]
        except KeyError:
            # A copy rather than Default itself: Vec3 is mutable, and handing out the shared default
            # would let `self.Offset.x = 1` change it for every instance of the class.
            value = self.Type(self.Default)
            values[self.Name] = value
            return value

    def __set__(self, instance, value):
        instance.__dict__[self.Name] = self._Convert(value)

    def __repr__(self):
        return f"PluEngine.Property({self.Name!r}, {self.Type.__name__}, Default={self.Default!r})"
