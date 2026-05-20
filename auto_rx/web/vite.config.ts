import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import path from "node:path";

export default defineConfig({
  plugins: [react()],
  resolve: { alias: { "@": path.resolve(__dirname, "./src") } },
  build: {
    outDir: "../autorx/static/build",
    emptyOutDir: true,
    assetsDir: "assets",
    sourcemap: false,
    // Never inline JS assets as data: URLs — the Leaflet plugins under
    // src/lib/leaflet-plugins/ are imported via `?url` and need to load as
    // real <script src="..."> tags so `window.L` patches stay debuggable.
    // Returning undefined for other extensions falls back to the default
    // (4096 byte) inline threshold.
    assetsInlineLimit: (filePath: string) =>
      filePath.endsWith(".js") ? false : undefined,
  },
  base: "/static/build/",
});
