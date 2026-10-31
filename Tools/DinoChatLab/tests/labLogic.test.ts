import { readFileSync } from "node:fs";
import { transform } from "esbuild";
import { runInNewContext } from "node:vm";
import { afterEach, describe, expect, it, vi } from "vitest";
import config from "../vite.config";
import { DOWNLOAD_URL_HOLD_MS, revokeObjectUrlLater } from "../src/logic/download";

const riveSource = readFileSync(new URL("../node_modules/rivescript/src/rivescript.js", import.meta.url), "utf8");

function runtimeSlice(source: string): string {
  const start = source.indexOf("runtime() {");
  const end = source.indexOf("say(message)", start);
  if (start < 0 || end < 0) throw new Error("RiveScript runtime() was not found.");
  return source.slice(start, end).replace("runtime()", "function runtime()");
}

function runRuntime(source: string): { out: string; fs: unknown } {
  const script = `
    ${runtimeSlice(source)}
    const self = { _node: {} };
    const out = runtime.call(self);
    ({ out, fs: self._node.fs });
  `;
  return runInNewContext(script, {
    require(name: string) {
      throw new Error(`require ${name}`);
    },
  }) as { out: string; fs: unknown };
}

describe("transcript download", () => {
  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  it("keeps the blob url until the browser can start the file", () => {
    vi.useFakeTimers();
    const revoked: string[] = [];
    vi.spyOn(URL, "revokeObjectURL").mockImplementation((url) => {
      revoked.push(String(url));
    });
    revokeObjectUrlLater("blob:lab");
    expect(revoked).toEqual([]);
    vi.advanceTimersByTime(DOWNLOAD_URL_HOLD_MS - 1);
    expect(revoked).toEqual([]);
    vi.advanceTimersByTime(1);
    expect(revoked).toEqual(["blob:lab"]);
  });
});

describe("lab steering default", () => {
  it("leaves the context model off and follows the steered intent", () => {
    const source = readFileSync(new URL("../src/composables/useLab.ts", import.meta.url), "utf8");
    expect(source).toMatch(/const steerWithModel = ref\(false\)/);
    expect(source).toContain("reconcileSteeredDecision(");
    expect(source).toContain("speechHint: finalDecision.speechHint");
    expect(source).toContain("reasons: [plan.reason, ...finalDecision.reasons]");
    expect(source).toContain("revokeObjectUrlLater(url)");
    expect(source).not.toContain("URL.revokeObjectURL");
  });
});

describe("rivescript browser process", () => {
  it("throws when the browser has no process binding", () => {
    expect(() => runRuntime(riveSource)).toThrow(/process is not defined/);
  });

  it("defines process.browser so the runtime check never reads process", async () => {
    const define = config.define as Record<string, string> | undefined;
    expect(define?.["process.browser"]).toBe("true");
    const transformed = await transform(riveSource, {
      loader: "js",
      define,
    });
    const result = runRuntime(transformed.code);
    expect(result.out).toBe("web");
    expect(result.fs).toBeUndefined();
  });
});
