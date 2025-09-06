import subprocess
from pathlib import Path

def clang_format(files):
    """
    Run clang-format -i on a list of files. Uses project root .clang-format.
    files: list of Path or str
    """
    for f in files:
        try:
            subprocess.run(["clang-format", "-i", str(f)], check=True)
            print(f"Formatted {f} with clang-format.")
        except Exception as e:
            print(f"Warning: clang-format failed for {f}: {e}")
