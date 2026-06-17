#!/usr/bin/env node
// LittleFS image size-budget check (#94).
//
// The web UI is flashed into LittleFS (firmware/data/www) via `pio run -t uploadfs`.
// If the data dir outgrows a target's filesystem partition, the upload silently
// truncates and the UI breaks in confusing ways. This script sums firmware/data
// and fails if it exceeds BUDGET_BYTES so oversized assets are caught in CI,
// before flash.
//
// Tune BUDGET_BYTES to the SMALLEST target's LittleFS partition you ship the UI
// to. The 16 MB S3 boards have ~9.9 MB (openframe_16mb.csv); esp32dev (huge_app)
// and the esp8266 are far tighter, so the budget below is deliberately
// conservative. Override at runtime with `--budget=<bytes>` or FS_BUDGET_BYTES.
import { readdirSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { dirname } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DATA_DIR = join(__dirname, '..', 'data');

const argBudget = process.argv.find((a) => a.startsWith('--budget='));
const BUDGET_BYTES = Number(
  (argBudget && argBudget.split('=')[1]) || process.env.FS_BUDGET_BYTES || 1_572_864, // 1.5 MiB
);

function walk(dir) {
  const files = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, entry.name);
    if (entry.isDirectory()) files.push(...walk(p));
    else if (entry.isFile()) files.push({ path: p, size: statSync(p).size });
  }
  return files;
}

let files;
try {
  files = walk(DATA_DIR);
} catch (err) {
  console.error(`[fs-size] cannot read ${DATA_DIR}: ${err.message}`);
  console.error('[fs-size] build the frontend first (npm run build) so firmware/data/www exists.');
  process.exit(2);
}

const total = files.reduce((n, f) => n + f.size, 0);
const kib = (n) => `${(n / 1024).toFixed(1)} KiB`;

// Show the 10 largest files for context.
files.sort((a, b) => b.size - a.size);
console.log('[fs-size] largest files:');
for (const f of files.slice(0, 10)) {
  console.log(`  ${kib(f.size).padStart(12)}  ${relative(DATA_DIR, f.path)}`);
}
console.log(`[fs-size] total: ${kib(total)} across ${files.length} files`);
console.log(`[fs-size] budget: ${kib(BUDGET_BYTES)} (${((total / BUDGET_BYTES) * 100).toFixed(1)}% used)`);

if (total > BUDGET_BYTES) {
  console.error(`[fs-size] FAIL: LittleFS payload ${kib(total)} exceeds budget ${kib(BUDGET_BYTES)}.`);
  console.error('[fs-size] Trim/compress assets, or raise the budget if the target FS is larger.');
  process.exit(1);
}
console.log('[fs-size] OK');
