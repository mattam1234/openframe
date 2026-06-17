import fs from 'fs';
import path from 'path';

export interface FirmwareFile {
  name: string;
  size: number;
  mtime: number;
}

// Stores uploaded firmware binaries on disk and serves them over plain HTTP on
// the trusted LAN, so devices can OTA from the CMS without TLS/cert handling.
export class FirmwareStore {
  readonly dir: string;

  constructor(dataDir: string) {
    this.dir = path.join(dataDir, 'firmware');
    fs.mkdirSync(this.dir, { recursive: true });
  }

  // Keep only safe, flat filenames — no path traversal, no surprises.
  static sanitize(name: string): string {
    return path.basename(name).replace(/[^\w.-]/g, '_');
  }

  list(): FirmwareFile[] {
    return fs
      .readdirSync(this.dir)
      .filter((f) => fs.statSync(path.join(this.dir, f)).isFile())
      .map((name) => {
        const s = fs.statSync(path.join(this.dir, name));
        return { name, size: s.size, mtime: s.mtimeMs };
      })
      .sort((a, b) => b.mtime - a.mtime);
  }

  has(name: string): boolean {
    const p = path.join(this.dir, FirmwareStore.sanitize(name));
    return fs.existsSync(p) && fs.statSync(p).isFile();
  }

  remove(name: string): boolean {
    const p = path.join(this.dir, FirmwareStore.sanitize(name));
    if (!fs.existsSync(p)) return false;
    fs.unlinkSync(p);
    return true;
  }
}
