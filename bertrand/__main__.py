"""Run Bertrand from the command line to get include directory, version number, etc.
"""
from __future__ import annotations

from .env.cli import External, Internal
from .env.run import inside_image

# # create swap memory for large builds
# swapfile = env_root / "swapfile"
# sudo = sudo_prefix()
# if swap:
#     run([*sudo, "fallocate", "-l", f"{swap}G", str(swapfile)])
#     run([*sudo, "chmod", "600", str(swapfile)])
#     run([*sudo, "mkswap", str(swapfile)])
#     run([*sudo, "swapon", str(swapfile)])

# try:

# # clear swap memory
# finally:
#     if swapfile.exists():
#         print("Cleaning up swap file...")
#         run([*sudo, "swapoff", str(swapfile)], check=False)
#         swapfile.unlink(missing_ok=True)


def main() -> None:
    """Entry point for the Bertrand CLI."""
    if inside_image():
        Internal()()
    else:
        External()()
