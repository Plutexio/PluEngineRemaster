"""Sends Python's stdout and stderr to the engine log.

Installed once per interpreter by Plu::RedirectPythonOutputToLog() (EmbeddedPython.h), which passes
a callback taking (message, isError).
"""
import sys


class _LogRedirector:
    def __init__(self, callback, isError):
        self._callback = callback
        self._isError = isError

    def write(self, message):
        text = message.strip()
        if text:
            self._callback(text, self._isError)
        return len(message)

    def flush(self):
        pass


def Install(callback):
    sys.stdout = _LogRedirector(callback, False)
    sys.stderr = _LogRedirector(callback, True)
