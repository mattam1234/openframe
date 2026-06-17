import { createApp, h } from 'vue'
import { createPinia } from 'pinia'
import { createVuetify } from 'vuetify'
import {
  mdiAccessPoint,
  mdiAccountBoxMultiple,
  mdiAlertCircle,
  mdiArrowUpLeft,
  mdiAxisXArrow,
  mdiAxisYArrow,
  mdiAxisZArrow,
  mdiBrightness5,
  mdiBroadcast,
  mdiChartLine,
  mdiCheck,
  mdiCheckCircle,
  mdiChevronLeft,
  mdiChevronRight,
  mdiChip,
  mdiCircleOutline,
  mdiClockOutline,
  mdiClose,
  mdiCodeJson,
  mdiCog,
  mdiContentSave,
  mdiCurrentAc,
  mdiDelete,
  mdiDeleteSweep,
  mdiDeveloperBoard,
  mdiDownload,
  mdiElectricSwitch,
  mdiExpansionCard,
  mdiExport,
  mdiEye,
  mdiFileDocumentOutline,
  mdiFileExport,
  mdiFilePlusOutline,
  mdiFlash,
  mdiFlashOutline,
  mdiFolder,
  mdiFolderCogOutline,
  mdiFolderNetwork,
  mdiFolderPlusOutline,
  mdiFormSelect,
  mdiFormTextbox,
  mdiGauge,
  mdiGestureTap,
  mdiGestureTapButton,
  mdiHarddisk,
  mdiHelpCircleOutline,
  mdiHomeAssistant,
  mdiIdentifier,
  mdiImageFilterHdr,
  mdiImport,
  mdiLayers,
  mdiLedOn,
  mdiLedStripVariant,
  mdiLightSwitch,
  mdiLightningBolt,
  mdiMagnify,
  mdiMemory,
  mdiMonitor,
  mdiMonitorEdit,
  mdiMusicNote,
  mdiNumeric,
  mdiPalette,
  mdiPencil,
  mdiPlay,
  mdiPlaylistPlay,
  mdiPlus,
  mdiPlusCircle,
  mdiPowerPlug,
  mdiRefresh,
  mdiRenameBox,
  mdiRotateRight,
  mdiRouterWireless,
  mdiSpeedometer,
  mdiStethoscope,
  mdiTelevision,
  mdiTextBoxOutline,
  mdiThermometer,
  mdiToggleSwitch,
  mdiTransitConnectionVariant,
  mdiUpload,
  mdiViewDashboard,
  mdiWaterPercent,
  mdiWifi,
  mdiWifiOff,
  mdiWifiRefresh,
} from '@mdi/js'
import { aliases, mdi as mdiSvg } from 'vuetify/iconsets/mdi-svg'
import 'vuetify/styles'

import App from './App.vue'
import router from './router'

const mdiIconMap = {
  'mdi-access-point': mdiAccessPoint,
  'mdi-account-box-multiple': mdiAccountBoxMultiple,
  'mdi-alert-circle': mdiAlertCircle,
  'mdi-arrow-up-left': mdiArrowUpLeft,
  'mdi-axis-x-arrow': mdiAxisXArrow,
  'mdi-axis-y-arrow': mdiAxisYArrow,
  'mdi-axis-z-arrow': mdiAxisZArrow,
  'mdi-brightness-5': mdiBrightness5,
  'mdi-broadcast': mdiBroadcast,
  'mdi-chart-line': mdiChartLine,
  'mdi-check': mdiCheck,
  'mdi-check-circle': mdiCheckCircle,
  'mdi-chevron-left': mdiChevronLeft,
  'mdi-chevron-right': mdiChevronRight,
  'mdi-chip': mdiChip,
  'mdi-circle-outline': mdiCircleOutline,
  'mdi-clock-outline': mdiClockOutline,
  'mdi-close': mdiClose,
  'mdi-code-json': mdiCodeJson,
  'mdi-cog': mdiCog,
  'mdi-content-save': mdiContentSave,
  'mdi-current-ac': mdiCurrentAc,
  'mdi-delete': mdiDelete,
  'mdi-delete-sweep': mdiDeleteSweep,
  'mdi-developer-board': mdiDeveloperBoard,
  'mdi-download': mdiDownload,
  'mdi-electric-switch': mdiElectricSwitch,
  'mdi-expansion-card': mdiExpansionCard,
  'mdi-export': mdiExport,
  'mdi-eye': mdiEye,
  'mdi-file-document-outline': mdiFileDocumentOutline,
  'mdi-file-export': mdiFileExport,
  'mdi-file-plus-outline': mdiFilePlusOutline,
  'mdi-flash': mdiFlash,
  'mdi-flash-outline': mdiFlashOutline,
  'mdi-folder': mdiFolder,
  'mdi-folder-cog-outline': mdiFolderCogOutline,
  'mdi-folder-network': mdiFolderNetwork,
  'mdi-folder-plus-outline': mdiFolderPlusOutline,
  'mdi-form-select': mdiFormSelect,
  'mdi-form-textbox': mdiFormTextbox,
  'mdi-gauge': mdiGauge,
  'mdi-gesture-tap': mdiGestureTap,
  'mdi-gesture-tap-button': mdiGestureTapButton,
  'mdi-harddisk': mdiHarddisk,
  'mdi-home-assistant': mdiHomeAssistant,
  'mdi-identifier': mdiIdentifier,
  'mdi-image-filter-hdr': mdiImageFilterHdr,
  'mdi-import': mdiImport,
  'mdi-layers': mdiLayers,
  'mdi-led-on': mdiLedOn,
  'mdi-led-strip-variant': mdiLedStripVariant,
  'mdi-light-switch': mdiLightSwitch,
  'mdi-lightning-bolt': mdiLightningBolt,
  'mdi-magnify': mdiMagnify,
  'mdi-memory': mdiMemory,
  'mdi-monitor': mdiMonitor,
  'mdi-monitor-edit': mdiMonitorEdit,
  'mdi-music-note': mdiMusicNote,
  'mdi-numeric': mdiNumeric,
  'mdi-palette': mdiPalette,
  'mdi-pencil': mdiPencil,
  'mdi-play': mdiPlay,
  'mdi-playlist-play': mdiPlaylistPlay,
  'mdi-plus': mdiPlus,
  'mdi-plus-circle': mdiPlusCircle,
  'mdi-power-plug': mdiPowerPlug,
  'mdi-refresh': mdiRefresh,
  'mdi-rename-box': mdiRenameBox,
  'mdi-rotate-right': mdiRotateRight,
  'mdi-router-wireless': mdiRouterWireless,
  'mdi-speedometer': mdiSpeedometer,
  'mdi-stethoscope': mdiStethoscope,
  'mdi-television': mdiTelevision,
  'mdi-text-box-outline': mdiTextBoxOutline,
  'mdi-thermometer': mdiThermometer,
  'mdi-toggle-switch': mdiToggleSwitch,
  'mdi-transit-connection-variant': mdiTransitConnectionVariant,
  'mdi-upload': mdiUpload,
  'mdi-view-dashboard': mdiViewDashboard,
  'mdi-water-percent': mdiWaterPercent,
  'mdi-wifi': mdiWifi,
  'mdi-wifi-off': mdiWifiOff,
  'mdi-wifi-refresh': mdiWifiRefresh,
}

function resolveMdiIcon(icon) {
  if (typeof icon !== 'string' || !icon.startsWith('mdi-')) {
    return icon
  }

  return mdiIconMap[icon] || mdiHelpCircleOutline
}

const mdi = {
  component: props => h(mdiSvg.component, {
    ...props,
    icon: resolveMdiIcon(props.icon),
  }),
}

// Theme (#46): saved choice wins; otherwise follow the OS preference (dark default).
const initialTheme = (() => {
  try {
    const saved = localStorage.getItem('of-theme')
    if (saved === 'light' || saved === 'dark') return saved
  } catch { /* private mode */ }
  const prefersLight = typeof window !== 'undefined' && window.matchMedia
    && window.matchMedia('(prefers-color-scheme: light)').matches
  return prefersLight ? 'light' : 'dark'
})()

const vuetify = createVuetify({
  icons: {
    defaultSet: 'mdi',
    aliases,
    sets: { mdi },
  },
  theme: {
    defaultTheme: initialTheme,
  },
})

const pinia = createPinia()

createApp(App)
  .use(pinia)
  .use(router)
  .use(vuetify)
  .mount('#app')
