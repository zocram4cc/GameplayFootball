import os
import re

def modernize_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    original = content
    
    # Replace pointer initialization with nullptr
    # e.g., Player* player = 0;
    content = re.sub(r'([\w:\*]+\s*\*\s*[\w_]+\s*=\s*)0\s*;', r'\1nullptr;', content)
    
    # Replace return 0; where the function returns a pointer
    # Too dangerous with regex unless I parse signatures, let's skip.
    
    # Range-based for loop for simple `at(i)` without other `i` uses
    # It's a bit complex, let's write a simple matcher for standard patterns
    
    # 1. for (unsigned int i = 0; i < players.size(); i++) -> for (auto* player : players)
    # We will just do it for vector<T*> like humanGamers, players, etc. in team.cpp and match.cpp
    
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

print(f"Modified {modified} files for nullptr.")
