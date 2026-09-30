import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  server: {
    // dev only: the Go service (or the LXC) owns /api. The default host is a
    // placeholder — the real one was scrubbed out of this repo's history when
    // it went public, so `npm run dev` needs the target passing in:
    //   ESPRESSOLOG_API=http://<host>:8080 npm run dev
    proxy: {
      "/api": process.env.ESPRESSOLOG_API || "http://espressolog.lan:8080",
      "/healthz": process.env.ESPRESSOLOG_API || "http://espressolog.lan:8080",
    },
  },
  build: { outDir: "dist" },
});
