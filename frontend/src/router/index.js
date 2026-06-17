import { createRouter, createWebHashHistory } from 'vue-router'

const routes = [
  { path: '/',          component: () => import('../views/DashboardView.vue'),       name: 'dashboard'  },
  { path: '/layout',    component: () => import('../views/LayoutDesignerView.vue'),  name: 'layout'     },
  { path: '/screens',   component: () => import('../views/ScreenDesignerView.vue'),  name: 'screens'    },
  { path: '/sensors',   component: () => import('../views/SensorDashboardView.vue'), name: 'sensors'    },
  { path: '/outputs',   component: () => import('../views/OutputsView.vue'),         name: 'outputs'    },
  { path: '/actions',   component: () => import('../views/ActionManagerView.vue'),   name: 'actions'    },
  { path: '/modules',   component: () => import('../views/ModuleManagerView.vue'),   name: 'modules'    },
  { path: '/ha',        component: () => import('../views/HaManagerView.vue'),       name: 'ha'         },
  { path: '/profiles',  component: () => import('../views/ProfilesView.vue'),        name: 'profiles'   },
  { path: '/logs',      component: () => import('../views/LogsView.vue'),            name: 'logs'       },
  { path: '/files',     component: () => import('../views/FilesystemView.vue'),      name: 'files'      },
  { path: '/settings',  component: () => import('../views/SettingsView.vue'),        name: 'settings'   },
  { path: '/setup',     component: () => import('../views/SetupWizardView.vue'),     name: 'setup'      },
]

const router = createRouter({
  history: createWebHashHistory(),
  routes,
})

// When a lazy-loaded view chunk fails to load (e.g. the device's LittleFS holds
// a stale /www that no longer has the hashed chunk this build references), Vue
// Router silently aborts the navigation and the click appears to do nothing.
// Force a single full reload so a fresh index.html with the correct chunk names
// is fetched; a sessionStorage guard prevents an infinite reload loop when the
// chunk is genuinely missing on the device.
const CHUNK_RELOAD_KEY = 'of-chunk-reload'

router.onError((error, to) => {
  const message = String(error?.message || '')
  const isChunkLoadError =
    /Loading( CSS)? chunk|dynamically imported module|Importing a module script failed/i.test(message)

  if (!isChunkLoadError) return

  if (sessionStorage.getItem(CHUNK_RELOAD_KEY)) {
    // Already reloaded once and it still failed — the asset is missing on the
    // device. Surface it instead of looping; re-upload the filesystem image.
    console.error('View assets are missing on the device. Re-upload the filesystem image (pio run -t uploadfs).', error)
    return
  }

  sessionStorage.setItem(CHUNK_RELOAD_KEY, '1')
  window.location.assign(to?.fullPath ? `/#${to.fullPath}` : window.location.href)
  window.location.reload()
})

router.afterEach(() => {
  sessionStorage.removeItem(CHUNK_RELOAD_KEY)
})

export default router
