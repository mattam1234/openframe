import { createApp, h } from 'vue'
import { createVuetify } from 'vuetify'
import { aliases, mdi as mdiSvg } from 'vuetify/iconsets/mdi-svg'
import 'vuetify/styles'
import {
  mdiArrowLeft,
  mdiBellOutline,
  mdiChip,
  mdiClockOutline,
  mdiFileDocumentMultipleOutline,
  mdiHelpCircleOutline,
  mdiLan,
  mdiLanConnect,
  mdiLanDisconnect,
  mdiLogout,
  mdiMagnify,
  mdiMapMarker,
  mdiQrcode,
  mdiViewGrid,
} from '@mdi/js'
import App from './App.vue'
import router from './router'

// Icons are tree-shaken SVG paths, the same approach as the device UI. The MDI
// webfont shipped ~350 kB of render-blocking CSS plus the font files for the
// handful of icons used here. Templates keep using 'mdi-…' names; add any new
// icon to this map (an unmapped name renders as a help circle so it's obvious).
const mdiIconMap = {
  'mdi-arrow-left': mdiArrowLeft,
  'mdi-bell-outline': mdiBellOutline,
  'mdi-chip': mdiChip,
  'mdi-clock-outline': mdiClockOutline,
  'mdi-file-document-multiple-outline': mdiFileDocumentMultipleOutline,
  'mdi-lan': mdiLan,
  'mdi-lan-connect': mdiLanConnect,
  'mdi-lan-disconnect': mdiLanDisconnect,
  'mdi-logout': mdiLogout,
  'mdi-magnify': mdiMagnify,
  'mdi-map-marker': mdiMapMarker,
  'mdi-qrcode': mdiQrcode,
  'mdi-view-grid': mdiViewGrid,
}

function resolveMdiIcon(icon) {
  if (typeof icon !== 'string' || !icon.startsWith('mdi-')) return icon
  return mdiIconMap[icon] || mdiHelpCircleOutline
}

const mdi = {
  component: (props) => h(mdiSvg.component, { ...props, icon: resolveMdiIcon(props.icon) }),
}

// Dark theme to match the existing CMS look.
const vuetify = createVuetify({
  icons: { defaultSet: 'mdi', aliases, sets: { mdi } },
  theme: { defaultTheme: 'dark' },
})

createApp(App).use(router).use(vuetify).mount('#app')
