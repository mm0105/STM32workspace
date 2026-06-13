const fs = require("fs");
const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  AlignmentType, LevelFormat, HeadingLevel, BorderStyle, WidthType,
  ShadingType, VerticalAlign,
} = require("docx");

const SRC = process.argv[2];
const OUT = process.argv[3];
const md = fs.readFileSync(SRC, "utf8").replace(/\r\n/g, "\n");
const lines = md.split("\n");

// A4 content width (1 inch margins): 11906 - 2880 = 9026
const CONTENT_W = 9026;
const MONO = "Consolas";

// ---- inline parsing: **bold**, `code` ----
function parseInline(text, base = {}) {
  const runs = [];
  // tokenize on ** and `
  const re = /(\*\*[^*]+\*\*|`[^`]+`)/g;
  let last = 0, m;
  while ((m = re.exec(text)) !== null) {
    if (m.index > last) {
      runs.push(new TextRun({ text: text.slice(last, m.index), ...base }));
    }
    const tok = m[0];
    if (tok.startsWith("**")) {
      runs.push(new TextRun({ text: tok.slice(2, -2), bold: true, ...base }));
    } else {
      runs.push(new TextRun({ text: tok.slice(1, -1), font: MONO, ...base }));
    }
    last = re.lastIndex;
  }
  if (last < text.length) {
    runs.push(new TextRun({ text: text.slice(last), ...base }));
  }
  if (runs.length === 0) runs.push(new TextRun({ text: "", ...base }));
  return runs;
}

const children = [];

function splitTableRow(line) {
  let s = line.trim();
  if (s.startsWith("|")) s = s.slice(1);
  if (s.endsWith("|")) s = s.slice(0, -1);
  return s.split("|").map((c) => c.trim());
}

function makeTable(rows) {
  // rows: array of cell-arrays; row[0] = header, row[1] = separator (skip)
  const header = rows[0];
  const body = rows.slice(2);
  const ncol = header.length;
  const colW = Math.floor(CONTENT_W / ncol);
  const widths = Array(ncol).fill(colW);
  widths[ncol - 1] = CONTENT_W - colW * (ncol - 1); // fix rounding

  const border = { style: BorderStyle.SINGLE, size: 1, color: "AAAAAA" };
  const borders = { top: border, bottom: border, left: border, right: border };

  function cell(text, i, isHeader) {
    return new TableCell({
      borders,
      width: { size: widths[i], type: WidthType.DXA },
      shading: isHeader
        ? { fill: "D5E8F0", type: ShadingType.CLEAR }
        : undefined,
      margins: { top: 60, bottom: 60, left: 110, right: 110 },
      verticalAlign: VerticalAlign.CENTER,
      children: [
        new Paragraph({
          children: parseInline(text, isHeader ? { bold: true } : {}),
        }),
      ],
    });
  }

  const trows = [];
  trows.push(
    new TableRow({
      tableHeader: true,
      children: header.map((t, i) => cell(t, i, true)),
    })
  );
  for (const r of body) {
    const cells = [];
    for (let i = 0; i < ncol; i++) cells.push(cell(r[i] || "", i, false));
    trows.push(new TableRow({ children: cells }));
  }

  return new Table({
    width: { size: CONTENT_W, type: WidthType.DXA },
    columnWidths: widths,
    rows: trows,
  });
}

function headingLevelFor(hashes) {
  return [
    HeadingLevel.HEADING_1,
    HeadingLevel.HEADING_2,
    HeadingLevel.HEADING_3,
    HeadingLevel.HEADING_4,
  ][hashes - 1] || HeadingLevel.HEADING_4;
}

let i = 0;
while (i < lines.length) {
  const line = lines[i];
  const trimmed = line.trim();

  // fenced code block
  if (trimmed.startsWith("```")) {
    i++;
    const code = [];
    while (i < lines.length && !lines[i].trim().startsWith("```")) {
      code.push(lines[i]);
      i++;
    }
    i++; // skip closing fence
    const codeParas = code.map(
      (cl) =>
        new Paragraph({
          shading: { fill: "F4F4F4", type: ShadingType.CLEAR },
          spacing: { before: 0, after: 0, line: 240, lineRule: "auto" },
          children: [
            new TextRun({ text: cl || " ", font: MONO, size: 17 }),
          ],
        })
    );
    children.push(...codeParas);
    children.push(new Paragraph({ spacing: { after: 120 }, children: [] }));
    continue;
  }

  // table
  if (trimmed.startsWith("|")) {
    const tbl = [];
    while (i < lines.length && lines[i].trim().startsWith("|")) {
      tbl.push(splitTableRow(lines[i]));
      i++;
    }
    if (tbl.length >= 2) {
      children.push(makeTable(tbl));
      children.push(new Paragraph({ spacing: { after: 120 }, children: [] }));
    }
    continue;
  }

  // horizontal rule
  if (/^-{3,}$/.test(trimmed)) {
    children.push(
      new Paragraph({
        border: {
          bottom: { style: BorderStyle.SINGLE, size: 6, color: "2E75B6", space: 1 },
        },
        spacing: { before: 60, after: 120 },
        children: [],
      })
    );
    i++;
    continue;
  }

  // heading
  const hm = trimmed.match(/^(#{1,6})\s+(.*)$/);
  if (hm) {
    children.push(
      new Paragraph({
        heading: headingLevelFor(hm[1].length),
        children: parseInline(hm[2]),
      })
    );
    i++;
    continue;
  }

  // blockquote
  if (trimmed.startsWith(">")) {
    const qtext = trimmed.replace(/^>\s?/, "");
    children.push(
      new Paragraph({
        spacing: { after: 60 },
        indent: { left: 360 },
        border: {
          left: { style: BorderStyle.SINGLE, size: 12, color: "CCCCCC", space: 6 },
        },
        children: parseInline(qtext, { italics: true, color: "555555" }),
      })
    );
    i++;
    continue;
  }

  // ordered list
  const om = trimmed.match(/^(\d+)\.\s+(.*)$/);
  if (om) {
    children.push(
      new Paragraph({
        numbering: { reference: "ol", level: 0 },
        children: parseInline(om[2]),
      })
    );
    i++;
    continue;
  }

  // unordered list
  const ulm = trimmed.match(/^[-*]\s+(.*)$/);
  if (ulm) {
    children.push(
      new Paragraph({
        numbering: { reference: "ul", level: 0 },
        children: parseInline(ulm[1]),
      })
    );
    i++;
    continue;
  }

  // blank line
  if (trimmed === "") {
    i++;
    continue;
  }

  // plain paragraph
  children.push(
    new Paragraph({ spacing: { after: 80 }, children: parseInline(trimmed) })
  );
  i++;
}

const doc = new Document({
  styles: {
    default: { document: { run: { font: "微软雅黑", size: 21 } } },
    paragraphStyles: [
      { id: "Title", name: "Title", basedOn: "Normal", next: "Normal",
        run: { size: 40, bold: true, font: "微软雅黑" },
        paragraph: { spacing: { before: 240, after: 240 }, alignment: AlignmentType.CENTER } },
      { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 30, bold: true, font: "微软雅黑", color: "1F4E79" },
        paragraph: { spacing: { before: 280, after: 160 }, outlineLevel: 0 } },
      { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 26, bold: true, font: "微软雅黑", color: "2E75B6" },
        paragraph: { spacing: { before: 220, after: 120 }, outlineLevel: 1 } },
      { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 23, bold: true, font: "微软雅黑", color: "44546A" },
        paragraph: { spacing: { before: 160, after: 100 }, outlineLevel: 2 } },
      { id: "Heading4", name: "Heading 4", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 21, bold: true, font: "微软雅黑" },
        paragraph: { spacing: { before: 120, after: 80 }, outlineLevel: 3 } },
    ],
  },
  numbering: {
    config: [
      { reference: "ul",
        levels: [{ level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT,
          style: { paragraph: { indent: { left: 540, hanging: 280 } } } }] },
      { reference: "ol",
        levels: [{ level: 0, format: LevelFormat.DECIMAL, text: "%1.", alignment: AlignmentType.LEFT,
          style: { paragraph: { indent: { left: 540, hanging: 280 } } } }] },
    ],
  },
  sections: [
    {
      properties: {
        page: {
          size: { width: 11906, height: 16838 },
          margin: { top: 1440, right: 1440, bottom: 1440, left: 1440 },
        },
      },
      children,
    },
  ],
});

Packer.toBuffer(doc).then((buf) => {
  fs.writeFileSync(OUT, buf);
  console.log("WROTE", OUT, buf.length, "bytes");
});
