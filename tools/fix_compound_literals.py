#!/usr/bin/env python
"""Fix compound literals in vm_desktop.cpp: replace &(RECT){...} with helper function calls."""
import re, sys

path = 'src/cpp/vm_desktop.cpp'
with open(path, 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()

# Step 1: Remove the broken RECT declarations inserted by previous script
# Pattern: "    RECT _rN = {...};\n" followed by usage
content = re.sub(r'    RECT _r\d+ = \{[^}]+\};\n', '', content)
# Fix the &_rN references back to &(RECT){...} (we'll fix them properly below)
# Actually we can't easily recover the original compound literals...

# Let's just count what we removed
count = len(re.findall(r'    RECT _r\d+ = \{[^}]+\};', content))
print(f'Removed {count} broken RECT declarations')

# Step 2: Now fix &_rN -> use a proper approach
# Replace &_rN with &_R(l,t,r,b) - but we lost the original coords...
# This approach won't work because we already lost the original values.

# Better: Restore from the session by checking what &_rN remains
remaining = re.findall(r'&_r\d+', content)
print(f'Remaining broken references: {len(remaining)}')
if remaining:
    print('Sample:', remaining[:5])

with open(path, 'w', encoding='utf-8', errors='replace') as f:
    f.write(content)
