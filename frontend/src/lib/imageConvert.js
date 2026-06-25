// Convert an uploaded image file into the device's OFIM format, pre-scaled to the
// target dimensions and packed for the panel kind (colour → RGB565, mono → 1-bit).
// The device never decodes JPEG/PNG; it just blits these bytes.
//
// OFIM header (10 bytes, little-endian): 'O','F','I', version(1), format(1),
// width(2), height(2), reserved(1). format 0 = RGB565 (LE), 1 = 1-bit (MSB-first,
// rows byte-padded).

export function loadImageElement(file) {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file)
    const img = new Image()
    img.onload = () => { resolve(img); }
    img.onerror = (e) => { URL.revokeObjectURL(url); reject(e) }
    img.src = url
  })
}

// Returns { bytes: Uint8Array, width, height }. `color` true → RGB565, else 1-bit.
export async function fileToOfim(file, targetW, targetH, color) {
  const w = Math.max(1, Math.round(targetW))
  const h = Math.max(1, Math.round(targetH))
  const img = await loadImageElement(file)

  const cv = document.createElement('canvas')
  cv.width = w
  cv.height = h
  const ctx = cv.getContext('2d')
  ctx.drawImage(img, 0, 0, w, h)
  const { data } = ctx.getImageData(0, 0, w, h)  // RGBA, row-major

  const header = [0x4F, 0x46, 0x49, 1, color ? 0 : 1, w & 0xFF, (w >> 8) & 0xFF, h & 0xFF, (h >> 8) & 0xFF, 0]

  let body
  if (color) {
    body = new Uint8Array(w * h * 2)
    let o = 0
    for (let p = 0; p < w * h; p++) {
      const r = data[p * 4], g = data[p * 4 + 1], b = data[p * 4 + 2]
      const v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
      body[o++] = v & 0xFF            // little-endian
      body[o++] = (v >> 8) & 0xFF
    }
  } else {
    const bytesPerRow = Math.ceil(w / 8)
    body = new Uint8Array(bytesPerRow * h)
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const p = (y * w + x) * 4
        const lum = 0.3 * data[p] + 0.59 * data[p + 1] + 0.11 * data[p + 2]
        if (lum >= 128) body[y * bytesPerRow + (x >> 3)] |= (0x80 >> (x & 7))
      }
    }
  }

  const bytes = new Uint8Array(header.length + body.length)
  bytes.set(header, 0)
  bytes.set(body, header.length)
  return { bytes, width: w, height: h }
}
