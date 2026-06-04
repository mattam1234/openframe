import { createApp, h } from 'vue'
import { createPinia } from 'pinia'
import { createVuetify } from 'vuetify'
import {
  mdiAccessPoint,
  mdiAccountBoxMultiple,
  mdiAlertCircle,
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
  mdiCog,
  mdiContentSave,
  mdiCurrentAc,
  mdiDelete,
  mdiDeleteSweep,
  mdiDeveloperBoard,
  mdiExpansionCard,
  mdiExport,
  mdiEye,
  mdiFileExport,
  mdiFlash,
  mdiFlashOutline,
  mdiFolderNetwork,
  mdiFormSelect,
  mdiFormTextbox,
  mdiGauge,
  mdiGestureTap,
  mdiGestureTapButton,
  mdiHelpCircleOutline,
  mdiHomeAssistant,
  mdiIdentifier,
  mdiImageFilterHdr,
  mdiImport,
  mdiLayers,
  mdiLedOn,
  mdiLightSwitch,
  mdiLightningBolt,
  mdiMagnify,
  mdiMemory,
  mdiMonitor,
  mdiMonitorEdit,
  mdiNumeric,
  mdiPencil,
  mdiPlay,
  mdiPlaylistPlay,
  mdiPlus,
  mdiRefresh,
  mdiRotateRight,
  mdiRouterWireless,
  mdiSpeedometer,
  mdiTelevision,
  mdiTextBoxOutline,
  mdiThermometer,
  mdiToggleSwitch,
  mdiTransitConnectionVariant,
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
  'mdi-cog': mdiCog,
  'mdi-content-save': mdiContentSave,
  'mdi-current-ac': mdiCurrentAc,
  'mdi-delete': mdiDelete,
  'mdi-delete-sweep': mdiDeleteSweep,
  'mdi-developer-board': mdiDeveloperBoard,
  'mdi-expansion-card': mdiExpansionCard,
  'mdi-export': mdiExport,
  'mdi-eye': mdiEye,
  'mdi-file-export': mdiFileExport,
  'mdi-flash': mdiFlash,
  'mdi-flash-outline': mdiFlashOutline,
  'mdi-folder-network': mdiFolderNetwork,
  'mdi-form-select': mdiFormSelect,
  'mdi-form-textbox': mdiFormTextbox,
  'mdi-gauge': mdiGauge,
  'mdi-gesture-tap': mdiGestureTap,
  'mdi-gesture-tap-button': mdiGestureTapButton,
  'mdi-home-assistant': mdiHomeAssistant,
  'mdi-identifier': mdiIdentifier,
  'mdi-image-filter-hdr': mdiImageFilterHdr,
  'mdi-import': mdiImport,
  'mdi-layers': mdiLayers,
  'mdi-led-on': mdiLedOn,
  'mdi-light-switch': mdiLightSwitch,
  'mdi-lightning-bolt': mdiLightningBolt,
  'mdi-magnify': mdiMagnify,
  'mdi-memory': mdiMemory,
  'mdi-monitor': mdiMonitor,
  'mdi-monitor-edit': mdiMonitorEdit,
  'mdi-numeric': mdiNumeric,
  'mdi-pencil': mdiPencil,
  'mdi-play': mdiPlay,
  'mdi-playlist-play': mdiPlaylistPlay,
  'mdi-plus': mdiPlus,
  'mdi-refresh': mdiRefresh,
  'mdi-rotate-right': mdiRotateRight,
  'mdi-router-wireless': mdiRouterWireless,
  'mdi-speedometer': mdiSpeedometer,
  'mdi-television': mdiTelevision,
  'mdi-text-box-outline': mdiTextBoxOutline,
  'mdi-thermometer': mdiThermometer,
  'mdi-toggle-switch': mdiToggleSwitch,
  'mdi-transit-connection-variant': mdiTransitConnectionVariant,
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

const vuetify = createVuetify({
  icons: {
    defaultSet: 'mdi',
    aliases,
    sets: { mdi },
  },
  theme: {
    defaultTheme: 'dark',
  },
})

const pinia = createPinia()

createApp(App)
  .use(pinia)
  .use(router)
  .use(vuetify)
  .mount('#app')
