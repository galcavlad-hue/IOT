"""Generate a PDF document with the full project source code and folder structure."""
import os
from fpdf import FPDF

PROJECT_ROOT = r"c:\Users\t026glicv\test"
OUTPUT_PDF = os.path.join(PROJECT_ROOT, "project_source.pdf")
SKIP_DIRS = {".pio", ".git", "node_modules", "__pycache__"}
SKIP_FILES = {"generate_pdf.py", "project_source.pdf"}


class CodePDF(FPDF):
    def header(self):
        self.set_font("Courier", "B", 9)
        self.set_text_color(100, 100, 100)
        self.cell(0, 5, "IoT Irrigation System - Full Source Code", align="C")
        self.ln(6)
        self.line(10, self.get_y(), 200, self.get_y())
        self.ln(2)

    def footer(self):
        self.set_y(-15)
        self.set_font("Courier", "I", 8)
        self.set_text_color(128, 128, 128)
        self.cell(0, 10, f"Page {self.page_no()}/{{nb}}", align="C")

    def section_title(self, title):
        self.set_font("Courier", "B", 12)
        self.set_text_color(0, 80, 160)
        self.cell(0, 8, title, new_x="LMARGIN", new_y="NEXT")
        self.line(10, self.get_y(), 200, self.get_y())
        self.ln(3)

    def file_heading(self, path):
        self.set_font("Courier", "B", 10)
        self.set_text_color(180, 60, 20)
        self.cell(0, 7, path, new_x="LMARGIN", new_y="NEXT")
        self.ln(1)

    def code_line(self, line_num, text):
        self.set_font("Courier", "", 7)
        # Line number
        self.set_text_color(160, 160, 160)
        self.cell(12, 3.5, f"{line_num:4d}", align="R")
        self.cell(2, 3.5, " ")
        # Code text
        self.set_text_color(30, 30, 30)
        # Truncate very long lines
        max_chars = 130
        if len(text) > max_chars:
            text = text[:max_chars] + " ..."
        # Replace problematic characters
        text = text.replace("\t", "    ")
        try:
            self.cell(0, 3.5, text, new_x="LMARGIN", new_y="NEXT")
        except Exception:
            self.cell(0, 3.5, text.encode("ascii", "replace").decode(), new_x="LMARGIN", new_y="NEXT")


def build_tree(root):
    """Build a text-based folder tree."""
    lines = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in sorted(dirnames) if d not in SKIP_DIRS]
        depth = dirpath.replace(root, "").count(os.sep)
        indent = "  " * depth
        folder = os.path.basename(dirpath) + "/"
        if depth == 0:
            folder = os.path.basename(root) + "/"
        lines.append(f"{indent}{folder}")
        sub_indent = "  " * (depth + 1)
        for f in sorted(filenames):
            if f not in SKIP_FILES:
                lines.append(f"{sub_indent}{f}")
    return lines


def collect_files(root):
    """Collect all source files in order."""
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in sorted(dirnames) if d not in SKIP_DIRS]
        for f in sorted(filenames):
            if f in SKIP_FILES:
                continue
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, root)
            files.append((rel, full))
    return files


def main():
    pdf = CodePDF(orientation="P", unit="mm", format="A4")
    pdf.alias_nb_pages()
    pdf.set_auto_page_break(auto=True, margin=15)

    # --- Page 1: Folder Structure ---
    pdf.add_page()
    pdf.section_title("PROJECT FOLDER STRUCTURE")
    pdf.ln(2)
    tree = build_tree(PROJECT_ROOT)
    pdf.set_font("Courier", "", 9)
    pdf.set_text_color(30, 30, 30)
    for line in tree:
        pdf.cell(0, 4.5, line, new_x="LMARGIN", new_y="NEXT")

    # --- Source Files ---
    files = collect_files(PROJECT_ROOT)
    for rel_path, full_path in files:
        pdf.add_page()
        pdf.section_title(f"FILE: {rel_path}")
        try:
            with open(full_path, "r", encoding="utf-8", errors="replace") as f:
                lines = f.readlines()
        except Exception as e:
            pdf.set_font("Courier", "I", 9)
            pdf.set_text_color(200, 0, 0)
            pdf.cell(0, 5, f"[Error reading file: {e}]")
            continue

        pdf.set_font("Courier", "I", 8)
        pdf.set_text_color(100, 100, 100)
        pdf.cell(0, 4, f"{len(lines)} lines", new_x="LMARGIN", new_y="NEXT")
        pdf.ln(2)

        for i, line in enumerate(lines, 1):
            line = line.rstrip("\n\r")
            if pdf.get_y() > 280:
                pdf.add_page()
                pdf.file_heading(f"{rel_path} (continued)")
                pdf.ln(1)
            pdf.code_line(i, line)

    pdf.output(OUTPUT_PDF)
    print(f"PDF generated: {OUTPUT_PDF}")
    print(f"  Total files: {len(files)}")
    print(f"  Total pages: {pdf.page_no()}")


if __name__ == "__main__":
    main()
