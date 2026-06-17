import { createI18n } from 'vue-i18n'

// i18n scaffolding (#51). English is the source locale; add more under `messages`
// (e.g. import nl from './locales/nl.json') and the picker in Settings will list
// them. Components use `const { t } = useI18n()` / `$t('key')`. The active locale
// is persisted to localStorage.
export const SUPPORTED_LOCALES = [
  { value: 'en', label: 'English' },
]

const en = {
  nav: {
    dashboard: 'Dashboard',
    layout: 'Layout Designer',
    screens: 'Screen Designer',
    sensors: 'Sensors',
    outputs: 'Outputs',
    actions: 'Actions',
    modules: 'Modules',
    ha: 'Home Assistant',
    profiles: 'Profiles',
    logs: 'Logs',
    events: 'Event Inspector',
    files: 'Filesystem',
    settings: 'Settings',
    setup: 'Setup Wizard',
  },
  common: {
    connected: 'Connected',
    offline: 'Offline',
    save: 'Save',
    cancel: 'Cancel',
    delete: 'Delete',
    close: 'Close',
    language: 'Language',
  },
}

function initialLocale() {
  try {
    const saved = localStorage.getItem('of-locale')
    if (saved && SUPPORTED_LOCALES.some((l) => l.value === saved)) return saved
  } catch { /* private mode */ }
  return 'en'
}

export const i18n = createI18n({
  legacy: false,          // Composition API
  globalInjection: true,  // $t available in templates
  locale: initialLocale(),
  fallbackLocale: 'en',
  messages: { en },
})

export function setLocale(locale) {
  i18n.global.locale.value = locale
  try { localStorage.setItem('of-locale', locale) } catch { /* private mode */ }
}
