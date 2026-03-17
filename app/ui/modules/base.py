"""
UI module base classes.
"""

from dataclasses import dataclass


@dataclass
class ModuleDescriptor:
    name: str
    region: str  # "header", "footer", "page"


class UIModule:
    def __init__(self, name: str, region: str):
        self._desc = ModuleDescriptor(name=name, region=region)

    @property
    def name(self) -> str:
        return self._desc.name

    @property
    def region(self) -> str:
        return self._desc.region

    def build(self, parent=None):
        raise NotImplementedError
