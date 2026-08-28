import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  server: {
    // dev only: the Go service (or the LXC) owns /api
    proxy: { "/api": "http://espressolog.lan:8080" },
  },
  build: { outDir: "dist" },
});
