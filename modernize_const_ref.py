import os
import re

def process_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    original = content

    # Replace std::string pass-by-value with const std::string&
    # e.g., void Log(..., std::string className, ...)
    # Wait, we have to make sure we don't catch local declarations or things that shouldn't be ref'd.
    # To be extremely safe, we only replace it for function arguments.
    
    # We will target specifically the ones we found earlier and the ones we know are safe.
    
    content = re.sub(r'(\b[A-Za-z0-9_:]+\s+[A-Za-z0-9_]+\s*\([^)]*\b)std::string\s+([A-Za-z0-9_]+)(\b[^)]*\))', r'\1const std::string& \2\3', content)
    content = re.sub(r'(\b[A-Za-z0-9_:]+\s+[A-Za-z0-9_]+\s*\([^)]*\b)std::vector<([\w\s\*:]+)>\s+([A-Za-z0-9_]+)(\b[^)]*\))', r'\1const std::vector<\2>& \3\4', content)
    content = re.sub(r'(\b[A-Za-z0-9_:]+\s+[A-Za-z0-9_]+\s*\([^)]*\b)Vector3\s+([A-Za-z0-9_]+)(\b[^)]*\))', r'\1const Vector3& \2\3', content)

    # Let's just run it multiple times since regex only replaces one occurrence per match.
    for _ in range(5):
        content = re.sub(r'(\b[A-Za-z0-9_:]+\s+[A-Za-z0-9_]+\s*\([^)]*?\b)std::string\s+([A-Za-z0-9_]+)(\b[^)]*?\))', r'\1const std::string& \2\3', content)
        content = re.sub(r'(\b[A-Za-z0-9_:]+\s+[A-Za-z0-9_]+\s*\([^)]*?\b)std::vector<([\w\s\*:]+)>\s+([A-Za-z0-9_]+)(\b[^)]*?\))', r'\1const std::vector<\2>& \3\4', content)
        content = re.sub(r'(\b[A-Za-z0-9_:]+\s+[A-Za-z0-9_]+\s*\([^)]*?\b)Vector3\s+([A-Za-z0-9_]+)(\b[^)]*?\))', r'\1const Vector3& \2\3', content)

    if content != original:
        with open(filepath, 'w') as f:
            f.write(content)
        return True
    return False

modified = 0
for root, dirs, files in os.walk('src'):
    for file in files:
        if file.endswith('.cpp') or file.endswith('.hpp') or file.endswith('.h'):
            if process_file(os.path.join(root, file)):
                modified += 1

print(f"Modified {modified} files for const reference.")
