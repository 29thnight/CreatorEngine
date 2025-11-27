import re
from pathlib import Path

API_DIR = Path(__file__).resolve().parents[1] / "API_DOCS"

PREFIX_DESCRIPTIONS = {
    "get": "{subject}을(를) 가져옵니다.",
    "set": "{subject}을(를) 설정합니다.",
    "add": "{subject}을(를) 추가합니다.",
    "remove": "{subject}을(를) 제거합니다.",
    "find": "{subject}을(를) 탐색합니다.",
    "update": "{subject}을(를) 갱신합니다.",
    "create": "{subject}을(를) 생성합니다.",
    "destroy": "{subject}을(를) 파괴합니다.",
    "load": "{subject}을(를) 불러옵니다.",
    "save": "{subject}을(를) 저장합니다.",
    "apply": "{subject}을(를) 적용합니다.",
    "calculate": "{subject}을(를) 계산합니다.",
    "refresh": "{subject}을(를) 새로 고칩니다.",
    "clear": "{subject}을(를) 비웁니다.",
    "enable": "{subject}을(를) 활성화합니다.",
    "disable": "{subject}을(를) 비활성화합니다.",
    "is": "{subject} 여부를 확인합니다.",
}


def split_words(identifier: str) -> str:
    cleaned = re.sub(r"operator[^\w]+", "operator", identifier)
    cleaned = cleaned.replace("~", "")
    spaced = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", cleaned)
    spaced = spaced.replace("_", " ")
    return spaced.strip().lower()


def describe_member(name: str, kind: str) -> str:
    lower = name.lower()
    for prefix, template in PREFIX_DESCRIPTIONS.items():
        if lower.startswith(prefix):
            subject = split_words(name[len(prefix):] or name)
            return template.format(subject=subject).strip()
    if kind == "Method":
        return f"{split_words(name)} 동작을 수행합니다."
    return f"{split_words(name)} 상태를 보관합니다."


def extract_name(entry: str, kind: str) -> str:
    if kind == "Method":
        match = re.search(r"([~A-Za-z_][\w:]*)\s*(?=\()", entry)
        if match:
            return match.group(1).split("::")[-1]

    tokens = re.split(r"[\s=;(),]+", entry.strip())
    ignored = {"const", "static", "mutable", "volatile", "constexpr", "using"}
    primitive_types = {
        "int",
        "uint",
        "uint32",
        "uint64",
        "int32",
        "int64",
        "float",
        "double",
        "bool",
        "char",
        "short",
        "long",
        "size_t",
    }

    for token in reversed(tokens):
        if not token or set(token) <= {"`", "-"}:
            continue
        if token in ignored:
            continue
        if token.lower() in primitive_types:
            continue
        if not re.search(r"[A-Za-z_]", token):
            continue
        return token.split("::")[-1]

    return "Unknown"


def parse_entries(lines, kind: str):
    bullet_entries = []
    table_entries = []

    for line in lines:
        stripped = line.strip()

        if stripped.startswith("-"):
            content = stripped.lstrip("- ")
            content = content.split("=", 1)[0]
            content = content.strip("`")
            if not content:
                continue
            if content.strip().lower().startswith("(none"):
                continue
            name = extract_name(content, kind)
            description = describe_member(name, kind)
            bullet_entries.append((name, kind, description))
            continue

        if stripped.startswith("|"):
            match = re.match(r"\|\s*`([^`]+)`\s*\|", stripped)
            if match:
                name = match.group(1)
                description = describe_member(name, kind)
                table_entries.append((name, kind, description))

    return bullet_entries or table_entries


def build_table(title: str, entries: list[tuple[str, str, str]]) -> str:
    if not entries:
        return f"### {title} 역할\n표로 요약할 멤버가 없습니다."

    header = [
        f"### {title} 역할",
        "| 멤버 | 예상 역할 |",
        "| --- | --- |",
    ]
    rows = [f"| `{name}` | {desc} |" for name, _, desc in entries]
    return "\n".join(header + rows)


def rebuild_section(content_lines: list[str], header: str) -> list[str]:
    rebuilt = []
    i = 0
    while i < len(content_lines):
        line = content_lines[i]
        rebuilt.append(line)
        if line.strip() == f"## {header}":
            section_start = i + 1
            j = section_start
            while j < len(content_lines) and not content_lines[j].startswith("## "):
                j += 1

            section_body = content_lines[section_start:j]
            entries = parse_entries(section_body, "Method" if header == "Public Methods" else "Property")

            deduped = []
            seen = set()
            for name, kind, desc in entries:
                if name in seen:
                    continue
                seen.add(name)
                deduped.append((name, kind, desc))

            table_block = build_table(header, deduped).splitlines()
            rebuilt.append("")
            rebuilt.extend(table_block)
            i = j
            continue
        i += 1
    return rebuilt


def apply_section(path: Path) -> None:
    content = path.read_text(encoding="utf-8")
    stripped = re.sub(r"## (?:역할|정적 역할) 요약\n(?:.*?)(?=\n## |\Z)", "", content, flags=re.DOTALL)
    lines = stripped.splitlines()

    for header in ("Public Methods", "Public Properties"):
        lines = rebuild_section(lines, header)

    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def main():
    for path in sorted(API_DIR.glob("*.md")):
        apply_section(path)


if __name__ == "__main__":
    main()
