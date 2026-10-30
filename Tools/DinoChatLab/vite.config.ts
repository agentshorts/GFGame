import { defineConfig } from "vitest/config";
import vue from "@vitejs/plugin-vue";

const isolation = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
};

export default defineConfig({
  base: "./",
  plugins: [vue()],
  server: {
    port: 5173,
    headers: isolation,
  },
  preview: {
    headers: isolation,
  },
  test: {
    environment: "node",
    include: ["tests/**/*.test.ts"],
  },
});
