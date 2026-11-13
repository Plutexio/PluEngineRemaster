"""Pure-Python part of the PluEngine module.

Compiled into PluScripting at build time (cmake/PluEmbedPython.cmake) and merged into the PluEngine
module when it is imported (Plu::InstallPythonUtils): every name listed in __all__ becomes
PluEngine.<name>. Type stubs for these names are in PluEngineUtils.pyi next to this file.

This runs while the PluEngine module is still being initialised, so it must not import PluEngine
at module level: that would re-enter the module init.
"""

import copy

__all__ = ["Property"]

# Marks "no value stored yet" where None cannot: None is a legal return of a setter.
_Missing = object()


class Property:
    """A typed field of a Python engine class, with the metadata the engine needs to expose it.

    Declare it in the class body, not in __init__:

        class Turret(PluEngine.GameObject):
            Range = PluEngine.Property(25.0, "How far the turret sees, in metres", Min=0.0)

            def OnTick(self, deltaTime):
                if self.DistanceToPlayer() < self.Range: ...

    Instances read and write it like a plain attribute; assigning converts to Type. Every instance
    starts with its own copy of Default. Type is taken from Default unless given.

    The engine reads and writes the value through the attribute (getattr/setattr), so the editor,
    scene save/load and the PIE copy all see what a script sees.

    Getter and setter
    -----------------
    Like the builtin property, accessors are attached with decorators that reuse the property's name:

            @Range.setter
            def Range(self, value):
                self.RebuildSensor(value)

            @Range.getter
            def Range(self, stored):
                return stored * self.RangeMultiplier

    The value itself is still stored by the Property; the accessors are hooks around that storage:

    - setter(self, value): value is already converted to Type and already stored, so reading the
      property inside the setter (or in anything it calls) gives the new value. Returning None
      keeps it; returning anything else stores that instead (converted again), e.g. a clamped
      value. Raise to reject the assignment: the previous value is restored. Do not assign the
      same property inside its own setter: that recurses.
    - getter(self, stored) -> value: receives the stored value and returns what is read. The result
      is converted to Type.

    The setter runs on every assignment: from scripts, from the editor, when a scene is loaded and
    when the object is copied into PIE. It does not run for the initial Default.
    Getter=/Setter= in the constructor do the same with a function defined beforehand.
    """

    # Marker for the engine side to recognise a property by, without naming this class.
    __plu_property__ = True

    def __init__(self, Default, Tooltip="", Type=None, Min=None, Max=None, Getter=None, Setter=None):
        if Default is None:
            raise TypeError("PluEngine.Property needs a default value")
        self.Name = None
        self.Type = Type if Type is not None else type(Default)
        self.Default = self._Convert(Default)
        self.Tooltip = Tooltip
        self.Min = Min
        self.Max = Max
        self.Getter = self._CheckAccessor(Getter, "Getter")
        self.Setter = self._CheckAccessor(Setter, "Setter")
        # Set on a Property that getter()/setter() replaced with a copy. If it still ends up in a
        # class, the decorated function was given a different name and the copy is not the one used.
        self._Superseded = False

    def __set_name__(self, owner, name):
        if self._Superseded:
            raise TypeError(
                f"{owner.__name__}.{name}: a getter/setter of this PluEngine.Property was defined under "
                f"another name; the decorated function must be named {name!r}")
        self.Name = name

    @staticmethod
    def _CheckAccessor(function, kind):
        if function is not None and not callable(function):
            raise TypeError(f"PluEngine.Property {kind} must be callable, got {type(function).__name__}")
        return function

    def _Convert(self, value):
        # An exact type check: bool is an int subclass, and an int property must not keep a bool.
        return value if type(value) is self.Type else self.Type(value)

    def _WithAccessor(self, kind, function):
        # A copy, like the builtin property: decorating a base class's Property in a subclass must
        # not change the base class.
        result = copy.copy(self)
        setattr(result, kind, self._CheckAccessor(function, kind))
        self._Superseded = True
        return result

    def getter(self, function):
        """Decorator: function(self, stored) -> value, called on every read."""
        return self._WithAccessor("Getter", function)

    def setter(self, function):
        """Decorator: function(self, value) -> None or the value to store, called on every assignment."""
        return self._WithAccessor("Setter", function)

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        values = instance.__dict__
        try:
            value = values[self.Name]
        except KeyError:
            # A copy rather than Default itself: Vec3 is mutable, and handing out the shared default
            # would let `self.Offset.x = 1` change it for every instance of the class.
            value = self.Type(self.Default)
            values[self.Name] = value
        if self.Getter is not None:
            return self._Convert(self.Getter(instance, value))
        return value

    def __set__(self, instance, value):
        value = self._Convert(value)
        values = instance.__dict__
        if self.Setter is None:
            values[self.Name] = value
            return
        # Stored before the setter runs, so the setter (and anything it calls) reads the new value
        # through the attribute, not the previous one.
        previous = values.get(self.Name, _Missing)
        values[self.Name] = value
        try:
            replaced = self.Setter(instance, value)
            if replaced is not None:
                values[self.Name] = self._Convert(replaced)
        except BaseException:
            if previous is _Missing:
                values.pop(self.Name, None)
            else:
                values[self.Name] = previous
            raise

    def __repr__(self):
        return f"PluEngine.Property({self.Name!r}, {self.Type.__name__}, Default={self.Default!r})"
