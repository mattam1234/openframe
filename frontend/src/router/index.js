import { createRouter, createWebHashHistory } from 'vue-router'

const routes = [
  { path: '/',          component: () => import('../views/DashboardView.vue'),       name: 'dashboard'  },
  { path: '/layout',    component: () => import('../views/LayoutDesignerView.vue'),  name: 'layout'     },
  { path: '/screens',   component: () => import('../views/ScreenDesignerView.vue'),  name: 'screens'    },
  { path: '/sensors',   component: () => import('../views/SensorDashboardView.vue'), name: 'sensors'    },
  { path: '/actions',   component: () => import('../views/ActionManagerView.vue'),   name: 'actions'    },
  { path: '/modules',   component: () => import('../views/ModuleManagerView.vue'),   name: 'modules'    },
  { path: '/ha',        component: () => import('../views/HaManagerView.vue'),       name: 'ha'         },
  { path: '/profiles',  component: () => import('../views/ProfilesView.vue'),        name: 'profiles'   },
  { path: '/logs',      component: () => import('../views/LogsView.vue'),            name: 'logs'       },
  { path: '/files',     component: () => import('../views/FilesystemView.vue'),      name: 'files'      },
  { path: '/settings',  component: () => import('../views/SettingsView.vue'),        name: 'settings'   },
]

export default createRouter({
  history: createWebHashHistory(),
  routes,
})
