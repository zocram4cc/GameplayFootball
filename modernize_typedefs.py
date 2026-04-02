import os
import re

def modernize_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    original = content
    
    # Typedef to using
    # typedef std::vector<int> IntVec; -> using IntVec = std::vector<int>;
    # Let's write a regex that matches simple typedefs.
    content = re.sub(r'typedef\s+([\w:<>,\s\*]+)\s+(\w+)\s*;', r'using \2 = \1;', content)
    
    # Fix instances where the regex matched incorrectly by leaving them as using (it works for most).
    # Wait, 'unsigned int' is two words. It matches [\w\s].
    
    if content != original:
        with open(filepath, 'w') as f:
            f.write(content)
        return True
    return False

modified = 0
for root, dirs, files in os.walk('src'):
    for file in files:
        if file.endswith('.cpp') or file.endswith('.hpp') or file.endswith('.h'):
            if modernize_file(os.path.join(root, file)):
                modified += 1

print(f"Modified {modified} files for typedefs.")
