// Post-build: gzip the web assets in firmware/data/www so they fit the small
// LittleFS partitions (esp32dev's huge_app FS is only ~0.87 MB; the raw UI is
// ~1.14 MB). The device server already serves `<path>.gz` with
// `Content-Encoding: gzip` (see ApiServer::sendStaticFile), so we replace each
// compressible file with its .gz and drop the original — the browser gets the
// decompressed bytes transparently.
//
// Uses Node's built-in zlib only — no extra dependency.

import { readdirSync, statSync, readFileSync, writeFileSync, rmSync } from 'node:fs'
import { gzipSync, constants } from 'node:zlib'
import { fileURLToPath } from 'node:url'
import { dirname, join, extname } from 'node:path'

const WWW = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'firmware', 'data', 'www')

// Text-ish assets that compress well. Binary formats (png/jpg/woff2/…) are
// already compressed — gzipping them wastes CPU and can grow the file.
const COMPRESSIBLE = new Set(['.js', '.css', '.html', '.svg', '.json', '.webmanifest', '.map', '.txt', '.xml', '.ico'])
const MIN_BYTES = 256  // below this, the gzip header outweighs any savings

let before = 0
let after = 0
let count = 0

function walk(dir) {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const full = join(dir, entry.name)
    if (entry.isDirectory()) {
      walk(full)
      continue
    }
    if (entry.name.endsWith('.gz')) continue            // already compressed
    if (!COMPRESSIBLE.has(extname(entry.name).toLowerCase())) continue

    const raw = readFileSync(full)
    before += raw.length
    if (raw.length < MIN_BYTES) {
      after += raw.length
      continue
    }
    const gz = gzipSync(raw, { level: constants.Z_BEST_COMPRESSION })
    writeFileSync(full + '.gz', gz)
    rmSync(full)
    after += gz.length
    count++
  }
}

try {
  statSync(WWW)
} catch {
  console.error(`gzip-www: ${WWW} not found — run \`vite build\` first.`)
  process.exit(1)
}

walk(WWW)
const pct = before ? Math.round((1 - after / before) * 100) : 0
console.log(`gzip-www: compressed ${count} files — ${(before / 1024).toFixed(0)} KB → ${(after / 1024).toFixed(0)} KB (-${pct}%)`)
