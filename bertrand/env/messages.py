"""Print colorized warnings, errors, and debug messages to the console."""
import sys
from typing import NoReturn

import colorama


colorama.init(autoreset=True)
WHITE = colorama.Fore.WHITE                 # default
RED = colorama.Fore.LIGHTRED_EX             # failures
YELLOW = colorama.Fore.LIGHTYELLOW_EX       # warnings/emphasis in messages
GREEN = colorama.Fore.LIGHTGREEN_EX         # corrections
CYAN = colorama.Fore.LIGHTCYAN_EX           # diagnostics/verbose output
BLUE = colorama.Fore.LIGHTBLUE_EX
MAGENTA = colorama.Fore.LIGHTMAGENTA_EX     # debug (internal)


# pylint: disable=invalid-name


def DEBUG(message: str) -> None:
    """Print a debug message to the console and continue.

    Parameters
    ----------
    message : str
        The message to print to the console.
    """
    print(f"{MAGENTA}DEBUG{WHITE}: {message}")


def INFO(message: str) -> None:
    """Print an informational message to the console and continue.

    Parameters
    ----------
    message : str
        The message to print to the console.
    """
    print(f"{CYAN}INFO{WHITE}: {message}")


def WARN(message: str) -> None:
    """Print a warning message to the console and continue.

    Parameters
    ----------
    message : str
        The message to print to the console.
    """
    print(f"{YELLOW}WARNING{WHITE}: {message}")


def FAIL(message: str) -> NoReturn:
    """Print a failure message to the console and exit the program with an error code.

    Parameters
    ----------
    message : str
        The message to print before exiting.
    """
    print(f"{RED}FAILURE{WHITE}: {message}")
    sys.exit(1)
