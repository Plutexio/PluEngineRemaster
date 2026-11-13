# Stubs for PluEngineUtils.py, appended to ReflectionCache/PluEngine.pyi by the reflection generator.
# Keep in sync with PluEngineUtils.py.

class Property:
    """A typed field of a Python engine class (Tooltip, Min, Max for the engine).
    Declare it in the class body; instances read and write it like a plain attribute.
    @Prop.setter / @Prop.getter attach accessors (the decorated function must reuse the name):
    setter(self, value) -> None or the value to store; getter(self, stored) -> value read."""
    Name: str
    Type: type
    Default: object
    Tooltip: str
    Min: Optional[float]
    Max: Optional[float]
    Getter: Optional[Callable[[Any, Any], Any]]
    Setter: Optional[Callable[[Any, Any], Any]]
    def __init__(self, Default: object, Tooltip: str = "", Type: Optional[type] = None,
                 Min: Optional[float] = None, Max: Optional[float] = None,
                 Getter: Optional[Callable[[Any, Any], Any]] = None,
                 Setter: Optional[Callable[[Any, Any], Any]] = None) -> None: ...
    def __set_name__(self, owner: type, name: str) -> None: ...
    def getter(self, function: Callable[[Any, Any], Any]) -> Property: ...
    def setter(self, function: Callable[[Any, Any], Any]) -> Property: ...
    def __get__(self, instance: object, owner: Optional[type] = None) -> Any: ...
    def __set__(self, instance: object, value: Any) -> None: ...
