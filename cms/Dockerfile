# Built from the repo root so the web app can reach the shared component library
# (../shared). docker-compose sets `context: ..` + `dockerfile: cms/Dockerfile`.

# ── build ──────────────────────────────────────────────────────────────────
FROM node:22-alpine AS build
WORKDIR /repo

# Server (TypeScript → dist)
COPY cms/package.json cms/package-lock.json* ./cms/
RUN npm --prefix cms install
COPY cms/tsconfig.json ./cms/
COPY cms/src ./cms/src
RUN npm --prefix cms run build

# Web UI (Vue + Vuetify). Needs shared/ for the @shared alias at build time;
# the result is fully bundled, so shared/ isn't needed at runtime.
COPY shared ./shared
COPY cms/web ./cms/web
RUN npm --prefix cms/web install && npm --prefix cms/web run build

# ── runtime ────────────────────────────────────────────────────────────────
FROM node:22-alpine
WORKDIR /app
ENV NODE_ENV=production
COPY cms/package.json cms/package-lock.json* ./
RUN npm install --omit=dev
COPY --from=build /repo/cms/dist ./dist
COPY cms/public ./public
COPY --from=build /repo/cms/web/dist ./web/dist
EXPOSE 4000
VOLUME ["/app/data"]
CMD ["node", "dist/index.js"]
