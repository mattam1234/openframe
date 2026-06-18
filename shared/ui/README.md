# @openframe/ui — shared UI components

Vue 3 components shared between the **device web UI** (`frontend/`) and the
**fleet CMS** (`cms/`) so both render from one component set (backlog #45).

Components here must stay app-agnostic: no app-specific stores, routers, or API
clients — take data via props and emit events. Vuetify components may be used
(both consumers register Vuetify), but keep dependencies minimal.

## Consuming

Each app maps `@shared` to this folder with a Vite alias and reads it as source
(no separate build step):

```js
// vite.config.js
resolve: { alias: { '@shared': fileURLToPath(new URL('../shared/ui', import.meta.url)) } }
server: { fs: { allow: ['..'] } }  // allow reading this dir outside the app root
```

```vue
import { Sparkline } from '@shared'
```

## Migration status (#45)

- [x] `Sparkline` — used by the device sensor charts; CMS to adopt next.
