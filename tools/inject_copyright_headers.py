import os

directories = [
    "/Users/shankar/College/EQ project/radioform/packages/dsp",
    "/Users/shankar/College/EQ project/radioform/packages/spokes"
]
extensions = (".cpp", ".h", ".c", ".mm")

header = (
    "// Copyright (C) Radioform / Original Authors\n"
    "// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.\n"
    "// Licensed under the GNU GPLv3.\n\n"
)

modified_files = []

for base_dir in directories:
    if not os.path.exists(base_dir):
        print(f"Directory not found: {base_dir}")
        continue
    for root, dirs, files in os.walk(base_dir):
        # Skip build and other irrelevant directories
        if 'build' in dirs:
            dirs.remove('build')
        if '.build' in dirs:
            dirs.remove('.build')
        for file in files:
            if file.endswith(extensions):
                filepath = os.path.join(root, file)
                try:
                    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                        content = f.read()
                    
                    # Normalize line endings to avoid double injection due to carriage returns
                    normalized_content = content.replace("\r\n", "\n")
                    
                    if not normalized_content.startswith(header):
                        new_content = header + content
                        with open(filepath, "w", encoding="utf-8", newline="\n") as f:
                            f.write(new_content)
                        modified_files.append(filepath)
                        print(f"Injected header into: {filepath}")
                    else:
                        print(f"Already contains header: {filepath}")
                except Exception as e:
                    print(f"Error processing {filepath}: {e}")

print(f"Legal header restoration complete. Total files modified: {len(modified_files)}")
