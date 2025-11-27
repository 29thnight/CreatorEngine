import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_BINDER = REPO_ROOT / "ScriptBinder"
OUTPUT_DIR = REPO_ROOT / "API_DOCS"

CLASS_PATTERN = re.compile(r"class\s+(\w+)\s*([^\{;]*?)\{")
ACCESS_SPECIFIERS = {"public:", "protected:", "private:"}
COMMENT_PATTERN = re.compile(r"//.*?$|/\*.*?\*/", re.S | re.M)

def read_file(path: Path) -> str:
    with path.open(encoding="utf-8", errors="ignore") as f:
        return f.read()

def find_matching_brace(text: str, start_index: int) -> int:
    depth = 1
    i = start_index
    while i < len(text) and depth > 0:
        char = text[i]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        i += 1
    return i

def clean_member_line(line: str) -> str:
    line = line.strip()
    if "//" in line:
        line = line.split("//", 1)[0].strip()
    return line

def extract_class_blocks(header_content: str):
    header_content = re.sub(COMMENT_PATTERN, "", header_content)
    classes = []
    for match in CLASS_PATTERN.finditer(header_content):
        name = match.group(1)
        inheritance = match.group(2).strip()
        start = match.end()
        end = find_matching_brace(header_content, start)
        body = header_content[start:end-1]
        classes.append((name, inheritance, body))
    return classes

def parse_public_interface(body: str):
    current_access = "private"
    methods = []
    properties = []
    brace_depth = 0

    for raw_line in body.splitlines():
        line = clean_member_line(raw_line)
        if not line:
            continue
        lower = line.lower()
        if lower in ACCESS_SPECIFIERS:
            current_access = lower[:-1]
            continue
        if current_access != "public":
            continue
        # Skip forward declarations or nested classes
        if line.startswith("class "):
            continue
        brace_delta = line.count("{") - line.count("}")
        if brace_depth > 0:
            brace_depth += brace_delta
            continue
        if "{" in line:
            brace_depth += brace_delta
            continue
        if line.endswith("};") or line == "};":
            continue
        if "return " in line:
            continue
        if "(" in line and ")" in line and line.endswith(";"):
            methods.append(line)
        elif line.endswith(";"):
            properties.append(line)
    return methods, properties

def write_markdown(class_name: str, inheritance: str, methods, properties, source: Path):
    OUTPUT_DIR.mkdir(exist_ok=True)
    doc_path = OUTPUT_DIR / f"{class_name}.md"
    header_rel = source.relative_to(REPO_ROOT)
    lines = [
        f"# {class_name}",
        "",
        f"**Header:** `{header_rel}`",
    ]
    if inheritance:
        formatted_inherit = inheritance.replace(" :", ":").strip()
        lines.extend(["", f"**Inheritance:** `{formatted_inherit}`"])
    lines.extend([
        "",
        "> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.",
        "",
    ])
    lines.append("## Public Methods")
    if methods:
        for method in methods:
            lines.append(f"- `{method}`")
    else:
        lines.append("- (none)")
    lines.extend(["", "## Public Properties"])
    if properties:
        for prop in properties:
            lines.append(f"- `{prop}`")
    else:
        lines.append("- (none)")
    doc_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return doc_path

def generate_docs():
    headers = sorted(SCRIPT_BINDER.rglob("*.h"))
    if not headers:
        print("No header files found under ScriptBinder.")
        return
    generated_files = []
    for header in headers:
        content = read_file(header)
        class_blocks = extract_class_blocks(content)
        for class_name, inheritance, body in class_blocks:
            methods, properties = parse_public_interface(body)
            doc_path = write_markdown(class_name, inheritance, methods, properties, header)
            generated_files.append(doc_path)
    print(f"Generated {len(generated_files)} documentation files in {OUTPUT_DIR}.")

if __name__ == "__main__":
    try:
        generate_docs()
    except Exception as exc:
        print(f"Error while generating docs: {exc}")
        sys.exit(1)
