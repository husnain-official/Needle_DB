import re
from pathlib import Path
from types import SimpleNamespace

def load_cpp_schema(filepath: str = "services/engine/include/schema.hpp") -> SimpleNamespace:
    # Read the C++ header file
    content = Path(filepath).read_text()

    # Isolate the 'namespace schema { ... }' block
    # re.DOTALL allows '.' to match newlines, and (.*) grabs everything up to the LAST '}'
    namespace_match = re.search(r"namespace schema\s*\{(.*)\}", content, re.DOTALL)
    
    if not namespace_match:
        raise ValueError("[Schema_loader]   |   Error <Could not find 'namespace schema' block>.")
    
    schema_body = namespace_match.group(1)
    schema_vars = {}

    # 1. Parse integer constants (Now searching schema_body again)
    int_pattern = re.compile(r"constexpr\s+[u]?int\d+_t\s+([A-Z_]+)\s*=\s*(\d+);")
    for match in int_pattern.finditer(schema_body):
        name, value = match.groups()
        schema_vars[name] = int(value)

    # 2. Parse the magic number character array
    char_pattern = re.compile(r"constexpr\s+char\s+([A-Z_]+)\[\d+\]\s*=\s*\{([^}]+)\};")
    for match in char_pattern.finditer(schema_body):
        name, elements = match.groups()
        chars = re.findall(r"'([^']+)'", elements)
        clean_string = "".join(c for c in chars if c != '\\0')
        schema_vars[name] = clean_string

    return SimpleNamespace(**schema_vars)   

# --- Usage Example ---
if __name__ == "__main__":
    schema = load_cpp_schema("services/engine/include/schema.hpp")
    print(f"Loaded schema from C++ header: {schema}")
