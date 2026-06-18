/**
 * Structural validation for the device's on-disk config JSON (#53).
 *
 * The filesystem editor already checks JSON *syntax* (JSON.parse) and shows a
 * diff before overwriting. This adds a light *structural* check on top, keyed by
 * filename, so an edit that parses but has the wrong shape — e.g. `outputs.json`
 * with `outputs` as an object instead of an array — is caught before it's flashed
 * and silently ignored (or, worse, partially applied) by the firmware on reboot.
 *
 * Deliberately conservative. It only mirrors the shapes the firmware actually
 * keys on (verified against the *Manager loaders), so a valid-but-unusual config
 * is never blocked:
 *   - `errors`   → the firmware would reject/ignore this; the editor blocks the save.
 *   - `warnings` → probably a mistake, but legal; the editor lets you save anyway.
 *
 * Returns { errors: string[], warnings: string[] }. Unknown files (anything not
 * listed below) get no structural rules — syntax validation still applies.
 */

// Files that are a JSON object wrapping a single array of objects. The key is the
// array the firmware reads; `required` controls warn-vs-silent when it's absent.
const COLLECTION_FILES = {
  'outputs.json': { key: 'outputs', required: true },
  'sensors.json': { key: 'sensors', required: true },
  'actions.json': { key: 'actions', required: true },
  'macros.json': { key: 'macros', required: true },
  'scenes.json': { key: 'scenes', required: true },
  'variables.json': { key: 'variables', required: true },
  'displays.json': { key: 'displays', required: true },
}

// inputs.json holds several optional arrays (any subset is valid).
const INPUT_ARRAY_KEYS = ['digital', 'analog', 'encoders', 'keypads']

function basename(path) {
  return String(path || '').split('/').filter(Boolean).pop() || ''
}

function isObject(v) {
  return v !== null && typeof v === 'object' && !Array.isArray(v)
}

// Display page definitions live under /pages/<id>.json.
function isPagePath(path) {
  return /(^|\/)pages\/[^/]+\.json$/.test(String(path || ''))
}

/** True when this file has structural rules below (so the editor can label it). */
export function hasSchema(path) {
  const name = basename(path)
  return (
    name in COLLECTION_FILES ||
    name === 'inputs.json' ||
    name === 'config.json' ||
    isPagePath(path)
  )
}

export function validateConfigFile(path, value) {
  const errors = []
  const warnings = []
  if (!hasSchema(path)) return { errors, warnings }

  // Every config file the firmware reads is a JSON object at the root.
  if (!isObject(value)) {
    errors.push('The root of this config file must be a JSON object ({ … }).')
    return { errors, warnings }
  }

  const name = basename(path)

  const collection = COLLECTION_FILES[name]
  if (collection) {
    const { key, required } = collection
    if (!(key in value)) {
      if (required) {
        warnings.push(`Expected a top-level "${key}" array — the device will treat this file as empty.`)
      }
    } else if (!Array.isArray(value[key])) {
      errors.push(`"${key}" must be an array.`)
    } else {
      value[key].forEach((el, i) => {
        if (!isObject(el)) errors.push(`"${key}[${i}]" must be an object.`)
      })
    }
  }

  if (name === 'inputs.json') {
    for (const key of INPUT_ARRAY_KEYS) {
      if (key in value && !Array.isArray(value[key])) {
        errors.push(`"${key}" must be an array.`)
      }
    }
  }

  if (isPagePath(path)) {
    if ('widgets' in value && !Array.isArray(value.widgets)) {
      errors.push('"widgets" must be an array.')
    }
  }

  return { errors, warnings }
}
