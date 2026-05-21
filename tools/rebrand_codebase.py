#!/usr/bin/env python3
import os
import re

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

EXCLUDE_DIRS = {".git", ".build", "build", "dist", "node_modules"}
EXCLUDE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".icns", ".avif", ".svg", ".gif", ".exe", ".dll", ".dylib", ".so", ".a", ".o", ".dSYM"}

REPLACEMENTS = [
    ("KRISHA_", "KRISHA_"),
    ("KRISHA", "KRISHA"),
    ("Krisha", "Krisha"),
    ("krisha", "krisha")
]

def rebrand_file(filepath):
    # Try reading as UTF-8
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            content = f.read()
    except Exception:
        # Skip binary or unreadable files
        return False

    original_content = content
    for old, new in REPLACEMENTS:
        content = content.replace(old, new)

    if content != original_content:
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"Rebranded: {os.path.relpath(filepath, PROJECT_ROOT)}")
        return True
    return False

def main():
    rebranded_count = 0
    total_count = 0
    
    print(f"Starting global rebranding in: {PROJECT_ROOT}")
    
    for root, dirs, files in os.walk(PROJECT_ROOT):
        # Filter directories in place to avoid entering excluded ones
        dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
        
        for file in files:
            ext = os.path.splitext(file)[1].lower()
            if ext in EXCLUDE_EXTENSIONS:
                continue
                
            filepath = os.path.join(root, file)
            total_count += 1
            if rebrand_file(filepath):
                rebranded_count += 1
                
    print("\n" + "="*50)
    print(f"Rebranding completed!")
    print(f"Total text files scanned: {total_count}")
    print(f"Total files modified: {rebranded_count}")
    print("="*50)

if __name__ == "__main__":
    main()
