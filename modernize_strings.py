import os
import re

def modernize_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    original = content
    
    # Replace .compare("") == 0 with .empty()
    content = re.sub(r'([\w\-\>\.]+)\.compare\(\s*""\s*\)\s*==\s*0', r'\1.empty()', content)
    
    # Replace .compare("") != 0 with !.empty()
    content = re.sub(r'([\w\-\>\.]+)\.compare\(\s*""\s*\)\s*!=\s*0', r'!\1.empty()', content)
    
    # Replace .compare("string") == 0 with == "string"
    # Needs to handle "string" capture
    content = re.sub(r'([\w\-\>\.]+)\.compare\(\s*("[^"]*")\s*\)\s*==\s*0', r'\1 == \2', content)

    # Replace .compare("string") != 0 with != "string"
    content = re.sub(r'([\w\-\>\.]+)\.compare\(\s*("[^"]*")\s*\)\s*!=\s*0', r'\1 != \2', content)

    # Note: What if they compare variables? e.g. .compare(var) == 0
    content = re.sub(r'([\w\-\>\.]+)\.compare\(\s*([\w\-\>\.]+)\s*\)\s*==\s*0', r'\1 == \2', content)
    content = re.sub(r'([\w\-\>\.]+)\.compare\(\s*([\w\-\>\.]+)\s*\)\s*!=\s*0', r'\1 != \2', content)

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

print(f"Modified {modified} files for string comparison.")
