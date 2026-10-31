import { defineConfig } from "vitest/config";
import vue from "@vitejs/plugin-vue";

const isolation = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
};

export default defineConfig({
  base: "./",
  plugins: [vue()],
  // RiveScript reads `process.browser` with no guard. The browser has no `process` binding.
  define: {
    "process.browser": "true",
  },
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
