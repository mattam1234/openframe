import { createApp } from 'vue'
import { createVuetify } from 'vuetify'
import 'vuetify/styles'
import '@mdi/font/css/materialdesignicons.css'
import App from './App.vue'

// Dark theme to match the existing CMS look. The CMS isn't flash-constrained
// (it runs on a host, not the device), so the MDI webfont is fine here.
const vuetify = createVuetify({
  theme: { defaultTheme: 'dark' },
})

createApp(App).use(vuetify).mount('#app')
