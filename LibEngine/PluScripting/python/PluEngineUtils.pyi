# Stubs for PluEngineUtils.py, appended to ReflectionCache/PluEngine.pyi by the reflection generator.
# Keep in sync with PluEngineUtils.py.

class Property:
    """A typed field of a Python engine class (Tooltip, Min, Max for the engine).
    Declare it in the class body; instances read and write it like a plain attribute."""
    Name: str
    Type: type
    Default: object
    Tooltip: str
    Min: Optional[float]
    Max: Optional[float]
    def __init__(self, Default: object, Tooltip: str = "", Type: Optional[type] = None,
                 Min: Optional[float] = None, Max: Optional[float] = None) -> None: ...
    def __set_name__(self, owner: type, name: str) -> None: ...
    def __get__(self, instance: object, owner: Optional[type] = None) -> Any: ...
    def __set__(self, instance: object, value: Any) -> None: ...
