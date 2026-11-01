"""Map 7zip -snc's Windows names back to original names in the virtual APFS tree."""
import os
from pathlib import Path
import re

def extracted_path(root, path):
    if os.name=='nt': path='/'.join(re.sub(r'[<>:"\\|?*]', '_', c) for c in path.split('/'))
    return Path(root)/path
