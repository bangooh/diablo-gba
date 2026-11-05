"""Build entry point: regenerate from the user-supplied MPQ."""
import runpy
from pathlib import Path
if __name__ == '__main__':
    runpy.run_path(str(Path(__file__).resolve().parent/'scripts/regenerate_complete.py'),run_name='__main__')
