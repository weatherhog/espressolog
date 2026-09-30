import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// The default is the scrubbed placeholder — the real host was removed from
// this repo's history when it went public. Pass the target in:
//   ESPRESSOLOG_API=http://<host>:8080 npm run dev
const API = process.env.ESPRESSOLOG_API || "http://espressolog.lan:8080";

export default defineConfig({
  plugins: [react()],
  server: {
    // dev only: the Go service (or the LXC) owns /api.
    proxy: {
      // ws: true is not optional — Pull opens /api/v1/live as a WebSocket,
      // and a string-shorthand proxy entry does not handle the upgrade, so
      // the live view would sit at "server link down" all through dev.
      "/api": { target: API, ws: true },
      // Without this, Vite's SPA fallback answers /healthz with index.html
      // and a 200, so the header would claim the server is up when it is not.
      "/healthz": { target: API },
    },
  },
  build: { outDir: "dist" },
});
