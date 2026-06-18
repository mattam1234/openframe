import { createRouter, createWebHistory } from 'vue-router'
import FleetView from './views/FleetView.vue'

// The app is served under /app, so the router base matches. Secondary pages are
// code-split so the initial fleet load stays small.
const routes = [
  { path: '/', name: 'fleet', component: FleetView },
  { path: '/device/:id', name: 'device', component: () => import('./views/DeviceView.vue'), props: true },
  { path: '/topology', name: 'topology', component: () => import('./views/TopologyView.vue') },
  { path: '/templates', name: 'templates', component: () => import('./views/TemplatesView.vue') },
  { path: '/jobs', name: 'jobs', component: () => import('./views/JobsView.vue') },
  { path: '/firmware', name: 'firmware', component: () => import('./views/FirmwareView.vue') },
  { path: '/alerts', name: 'alerts', component: () => import('./views/AlertsView.vue') },
  { path: '/provision', name: 'provision', component: () => import('./views/ProvisionView.vue') },
]

export default createRouter({
  history: createWebHistory('/app/'),
  routes,
})
