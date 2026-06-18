import { createRouter, createWebHistory } from 'vue-router'
import FleetView from './views/FleetView.vue'

// The app is served under /app, so the router base matches.
const routes = [
  { path: '/', name: 'fleet', component: FleetView },
  { path: '/device/:id', name: 'device', component: () => import('./views/DeviceView.vue'), props: true },
]

export default createRouter({
  history: createWebHistory('/app/'),
  routes,
})
