import os
import re

def modernize_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    original = content
    
    # 1. auto iterator replacements
    # std::vector<Player*>::iterator iter = players.begin();
    # -> auto iter = players.begin();
    content = re.sub(r'std::vector<[\w\*\s:]+>::iterator\s+(\w+)\s*=\s*([\w\->\.]+)\.begin\(\);', r'auto \1 = \2.begin();', content)
    content = re.sub(r'std::vector<[\w\*\s:]+>::const_iterator\s+(\w+)\s*=\s*([\w\->\.]+)\.begin\(\);', r'auto \1 = \2.begin();', content)
    content = re.sub(r'std::list<[\w\*\s:]+>::iterator\s+(\w+)\s*=\s*([\w\->\.]+)\.begin\(\);', r'auto \1 = \2.begin();', content)
    content = re.sub(r'std::list<[\w\*\s:]+>::const_iterator\s+(\w+)\s*=\s*([\w\->\.]+)\.begin\(\);', r'auto \1 = \2.begin();', content)
    content = re.sub(r'std::map<[\w\*\s:,]+>::iterator\s+(\w+)\s*=\s*([\w\->\.]+)\.begin\(\);', r'auto \1 = \2.begin();', content)

    # 2. auto in basic for loops with iterators
    # for (std::vector<Player*>::iterator iter = players.begin(); iter != players.end(); iter++)
    content = re.sub(r'for\s*\(\s*std::vector<[\w\*\s:]+>::iterator\s+(\w+)\s*=\s*([\w\->\.]+)\.begin\(\)\s*;\s*', r'for (auto \1 = \2.begin(); ', content)
    content = re.sub(r'for\s*\(\s*std::list<[\w\*\s:]+>::iterator\s+(\w+)\s*=\s*([\w\->\.]+)\.begin\(\)\s*;\s*', r'for (auto \1 = \2.begin(); ', content)
    
    # 3. override in virtual functions (header files)
    # This is trickier, skip unless very simple.
    
    # 4. Range-based for loops
    # for (unsigned int i = 0; i < players.size(); i++) -> for (auto* player : players)
    # Wait, we need to replace players.at(i) with player. This is very specific. Let's do a general pass for common ones.

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

print(f"Modified {modified} files for iterators.")
