#!/usr/bin/env node
// md2docx.js — gera docs/parte3_codec_com_perdas.docx a partir do .md (capa, índice com
// ligações, tabelas, blocos de código, citações e a figura RD).
//
// Precisa de Node.js e dos pacotes "docx" e "marked" (numa pasta qualquer):
//   npm install docx marked
// Uso (a partir da raiz do repositório):
//   NODE_PATH=/pasta/com/node_modules node scripts/md2docx.js \
//     docs/parte3_codec_com_perdas.md docs/parte3_codec_com_perdas.docx \
//     docs/relatorio/figuras/rd_lossy.png
const fs = require('fs');
const { marked } = require('marked');
const d = require('docx');

const [, , mdPath, outPath, figPath] = process.argv;
const tokens = marked.lexer(fs.readFileSync(mdPath, 'utf8'));

const FONT = 'Calibri', MONO = 'Consolas';
const BLUE = '1F3864', ACCENT = '2E74B5';
const PAGE_W = 11906, MARGIN = 1134, CONTENT_W = PAGE_W - 2 * MARGIN;   // A4, margens de 2 cm

const decode = s => s.replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&')
                     .replace(/&lt;/g, '<').replace(/&gt;/g, '>');

function run(text, st) {
  return new d.TextRun({ text: text.replace(/\n/g, ' '), font: st.font || FONT, bold: st.bold,
    italics: st.italics, strike: st.strike, color: st.color, size: st.size });
}

function inline(toks, st = {}) {
  const out = [];
  for (const t of toks || []) {
    switch (t.type) {
      case 'strong': out.push(...inline(t.tokens, { ...st, bold: true })); break;
      case 'em': out.push(...inline(t.tokens, { ...st, italics: true })); break;
      case 'del': out.push(...inline(t.tokens, { ...st, strike: true })); break;
      case 'codespan':
        out.push(new d.TextRun({ text: decode(t.text), font: MONO, size: st.size ? st.size - 1 : 20,
          bold: st.bold, italics: st.italics, shading: { type: d.ShadingType.CLEAR, fill: 'F2F2F2', color: 'auto' } }));
        break;
      case 'link': out.push(...inline(t.tokens, { ...st, color: ACCENT })); break;
      case 'br': out.push(new d.TextRun({ break: 1 })); break;
      case 'text':
        if (t.tokens && t.tokens.length) out.push(...inline(t.tokens, st));
        else out.push(run(decode(t.text), st));
        break;
      case 'escape': out.push(run(decode(t.text), st)); break;
      default: if (t.text) out.push(run(decode(t.text), st));
    }
  }
  return out;
}

const plain = toks => (toks || []).map(t => t.tokens ? plain(t.tokens) : decode(t.text || '')).join('');

let bookmarkN = 0, listInstance = 0;
const headings = [];

function heading(t) {
  const lv = { 1: d.HeadingLevel.HEADING_1, 2: d.HeadingLevel.HEADING_1, 3: d.HeadingLevel.HEADING_2 }[t.depth]
             || d.HeadingLevel.HEADING_3;
  const id = 'sec' + (++bookmarkN);
  if (t.depth <= 3) headings.push({ id, text: plain(t.tokens), depth: t.depth });
  return new d.Paragraph({ heading: lv, keepNext: true, children: [new d.Bookmark({ id, children: inline(t.tokens) })] });
}

function list(t, level = 0, inst = null) {
  const out = [];
  if (t.ordered && inst === null) inst = ++listInstance;
  for (const item of t.items) {
    let first = true;
    for (const sub of item.tokens) {
      if (sub.type === 'list') { out.push(...list(sub, level + 1, sub.ordered ? ++listInstance : null)); continue; }
      if (sub.type === 'space') continue;
      if (sub.type === 'table' || sub.type === 'code') { out.push(...block(sub)); continue; }
      const kids = (sub.type === 'text' || sub.type === 'paragraph')
        ? inline(sub.tokens && sub.tokens.length ? sub.tokens : [sub]) : inline([sub]);
      out.push(new d.Paragraph({
        children: kids, spacing: { after: 60 },
        numbering: first ? (t.ordered ? { reference: 'num', level, instance: inst } : { reference: 'bul', level }) : undefined,
        indent: first ? undefined : { left: 720 * (level + 1) },
      }));
      first = false;
    }
  }
  return out;
}

function code(t) {
  const lines = t.text.split('\n');
  const border = { style: d.BorderStyle.SINGLE, size: 4, color: 'D9D9D9', space: 4 };
  return lines.map((l, i) => new d.Paragraph({
    children: [new d.TextRun({ text: l.length ? l : ' ', font: MONO, size: 17 })],
    shading: { type: d.ShadingType.CLEAR, fill: 'F5F5F5', color: 'auto' },
    spacing: { before: i === 0 ? 120 : 0, after: i === lines.length - 1 ? 160 : 0, line: 240 },
    border: { left: border, right: border, top: i === 0 ? border : undefined, bottom: i === lines.length - 1 ? border : undefined },
    keepLines: true, keepNext: i < lines.length - 1 && lines.length < 25,
  }));
}

function table(t) {
  const n = t.header.length;
  const cellsOf = c => [t.header[c], ...t.rows.map(r => r[c])];
  const lens = t.header.map((h, c) => Math.max(...cellsOf(c).map(x => plain(x.tokens).length)));
  const words = t.header.map((h, c) => Math.max(...cellsOf(c).flatMap(x => plain(x.tokens).split(/\s+/)).map(x => x.length)));
  // primeiro, espaço para a palavra mais comprida de cada coluna; o resto pelo texto total
  const minW = words.map(x => (x + 1) * 110 + 180);
  const free = Math.max(0, CONTENT_W - minW.reduce((a, b) => a + b, 0));
  const extra = lens.map((l, c) => Math.max(0, Math.min(l, 80) - words[c]));
  const sumExtra = extra.reduce((a, b) => a + b, 0) || 1;
  let widths = minW.map((m, c) => Math.floor(m + free * extra[c] / sumExtra));
  const tot = widths.reduce((a, b) => a + b, 0);
  if (tot > CONTENT_W) widths = widths.map(x => Math.floor(x * CONTENT_W / tot));
  widths[n - 1] += CONTENT_W - widths.reduce((a, b) => a + b, 0);
  const border = { style: d.BorderStyle.SINGLE, size: 4, color: 'BFBFBF' };
  const borders = { top: border, bottom: border, left: border, right: border };
  const cell = (c, i, hdr, zebra) => new d.TableCell({
    width: { size: widths[i], type: d.WidthType.DXA }, borders,
    shading: hdr ? { type: d.ShadingType.CLEAR, fill: 'DCE6F2', color: 'auto' }
                 : zebra ? { type: d.ShadingType.CLEAR, fill: 'F7F9FC', color: 'auto' } : undefined,
    margins: { top: 50, bottom: 50, left: 90, right: 90 },
    children: [new d.Paragraph({ spacing: { after: 0 }, children: inline(c.tokens, { size: 19, bold: hdr || undefined }) })],
  });
  return [new d.Table({
    width: { size: CONTENT_W, type: d.WidthType.DXA }, columnWidths: widths,
    rows: [new d.TableRow({ tableHeader: true, children: t.header.map((c, i) => cell(c, i, true)) }),
           ...t.rows.map((r, ri) => new d.TableRow({ cantSplit: true, children: r.map((c, i) => cell(c, i, false, ri % 2 === 1)) }))],
  }), new d.Paragraph({ spacing: { after: 120 }, children: [] })];
}

function figure() {
  if (!figPath || !fs.existsSync(figPath)) return [];
  return [
    new d.Paragraph({ alignment: d.AlignmentType.CENTER, spacing: { before: 120 },
      children: [new d.ImageRun({ type: 'png', data: fs.readFileSync(figPath), transformation: { width: 576, height: 360 } })] }),
    new d.Paragraph({ alignment: d.AlignmentType.CENTER, spacing: { after: 200 },
      children: [new d.TextRun({ text: 'Figura 1 — SNR médio vs. débito: o nosso codec, MP3 (LAME) e AAC (ffmpeg), média dos 20 ficheiros oficiais.',
        italics: true, size: 19, color: '595959' })] }),
  ];
}

function quote(t) {
  // citação (">"): parágrafos com barra azul à esquerda e fundo claro
  const bar = { style: d.BorderStyle.SINGLE, size: 18, color: ACCENT, space: 8 };
  return t.tokens.filter(x => x.type === 'paragraph').map(p => new d.Paragraph({
    children: inline(p.tokens), border: { left: bar }, indent: { left: 240 },
    shading: { type: d.ShadingType.CLEAR, fill: 'EEF3FA', color: 'auto' }, spacing: { before: 80, after: 120 },
  }));
}

function block(t) {
  switch (t.type) {
    case 'heading': return [heading(t)];
    case 'paragraph': {
      const p = [new d.Paragraph({ children: inline(t.tokens), spacing: { after: 120 } })];
      if (/^Figura: `docs\/relatorio\/figuras\/rd_lossy\.png`/.test(t.text)) p.push(...figure());
      return p;
    }
    case 'list': return list(t);
    case 'code': return code(t);
    case 'table': return table(t);
    case 'blockquote': return quote(t);
    default: return [];
  }
}

// Capa a partir do título; o texto introdutório vai para "Sobre este documento";
// o índice do markdown é substituído por um índice com ligações internas.
let i = 0;
const title = plain(tokens.find(t => t.type === 'heading' && t.depth === 1).tokens);
const intro = [];
while (tokens[i].type !== 'hr') { intro.push(tokens[i]); i++; }
const introBody = intro.filter(t => t.type !== 'heading' && t.type !== 'space').slice(2);
const rest = [];
for (i++; i < tokens.length; i++) {
  const t = tokens[i];
  if (t.type === 'heading' && t.depth === 2 && plain(t.tokens) === 'Índice') {
    while (tokens[i + 1] && tokens[i + 1].type !== 'hr') i++;
    continue;
  }
  rest.push(t);
}
const body = rest.flatMap(block);
const introParas = [new d.Paragraph({ heading: d.HeadingLevel.HEADING_1, children: [new d.TextRun('Sobre este documento')] }),
                    ...introBody.flatMap(block)];
const toc = [
  new d.Paragraph({ heading: d.HeadingLevel.HEADING_1, children: [new d.TextRun('Índice')] }),
  ...headings.map(h => new d.Paragraph({
    spacing: { after: h.depth === 2 ? 60 : 20, before: h.depth === 2 ? 80 : 0 }, indent: { left: h.depth === 3 ? 440 : 0 },
    children: [new d.InternalHyperlink({ anchor: h.id, children: [new d.TextRun({ text: h.text, style: 'Hyperlink',
      bold: h.depth === 2, size: h.depth === 2 ? 22 : 20 })] })],
  })),
];
const T = (text, o = {}) => new d.TextRun({ text, font: FONT, ...o });
const hoje = new Date().toLocaleDateString('pt-PT', { day: 'numeric', month: 'long', year: 'numeric' });
const cover = [
  new d.Paragraph({ spacing: { before: 3200 }, children: [] }),
  new d.Paragraph({ children: [T('Information and Coding 2026/27 · Trabalho Prático n.º 1', { size: 24, color: '595959' })] }),
  new d.Paragraph({ border: { bottom: { style: d.BorderStyle.SINGLE, size: 12, color: ACCENT, space: 8 } },
    spacing: { before: 200, after: 200, line: 360, lineRule: d.LineRuleType.AUTO },
    children: [T(title, { size: 48, bold: true, color: BLUE })] }),
  new d.Paragraph({ spacing: { before: 200 }, children: [T('Documento interno do grupo: tudo o que foi feito na Parte III', { size: 26, italics: true, color: '404040' })] }),
  new d.Paragraph({ spacing: { before: 2400 }, children: [T('Responsável pela Parte III: Francisco Matos', { size: 22 })] }),
  new d.Paragraph({ children: [T('Grupo: Duarte Lourenço · Francisco Matos', { size: 22 })] }),
  new d.Paragraph({ children: [T('Gerado a ' + hoje + ' a partir de docs/parte3_codec_com_perdas.md', { size: 22 })] }),
  new d.Paragraph({ children: [new d.PageBreak()] }),
];
const page = { size: { width: PAGE_W, height: 16838 }, margin: { top: MARGIN, bottom: MARGIN, left: MARGIN, right: MARGIN } };
const doc = new d.Document({
  creator: 'Francisco Matos', title,
  styles: {
    default: { document: { run: { font: FONT, size: 21 }, paragraph: { spacing: { line: 276, lineRule: d.LineRuleType.AUTO } } } },
    paragraphStyles: [
      { id: 'Heading1', name: 'Heading 1', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 32, bold: true, color: BLUE, font: FONT },
        paragraph: { spacing: { before: 480, after: 160 }, outlineLevel: 0,
          border: { bottom: { style: d.BorderStyle.SINGLE, size: 6, color: ACCENT, space: 4 } } } },
      { id: 'Heading2', name: 'Heading 2', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 26, bold: true, color: ACCENT, font: FONT }, paragraph: { spacing: { before: 320, after: 120 }, outlineLevel: 1 } },
      { id: 'Heading3', name: 'Heading 3', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 22, bold: true, color: '404040', font: FONT }, paragraph: { spacing: { before: 240, after: 80 }, outlineLevel: 2 } },
    ],
  },
  numbering: { config: [
    { reference: 'bul', levels: [0, 1, 2].map(l => ({ level: l, format: d.LevelFormat.BULLET, text: ['•', '◦', '▪'][l],
        alignment: d.AlignmentType.LEFT, style: { paragraph: { indent: { left: 720 * (l + 1), hanging: 360 } } } })) },
    { reference: 'num', levels: [0, 1, 2].map(l => ({ level: l, format: l === 1 ? d.LevelFormat.LOWER_LETTER : d.LevelFormat.DECIMAL,
        text: `%${l + 1}.`, alignment: d.AlignmentType.LEFT, style: { paragraph: { indent: { left: 720 * (l + 1), hanging: 360 } } } })) },
  ] },
  sections: [
    { properties: { page }, children: cover },
    { properties: { page: { ...page, pageNumbers: { start: 1 } } },
      headers: { default: new d.Header({ children: [new d.Paragraph({ alignment: d.AlignmentType.RIGHT,
        children: [new d.TextRun({ text: 'Parte III — Codec de áudio com perdas', size: 17, color: '808080' })] })] }) },
      footers: { default: new d.Footer({ children: [new d.Paragraph({ alignment: d.AlignmentType.CENTER,
        children: [new d.TextRun({ children: ['Página ', d.PageNumber.CURRENT], size: 17, color: '808080' })] })] }) },
      children: [...introParas, ...toc, new d.Paragraph({ children: [new d.PageBreak()] }), ...body] },
  ],
});
d.Packer.toBuffer(doc).then(b => { fs.writeFileSync(outPath, b); console.log('escrito', outPath, b.length, 'bytes'); });
