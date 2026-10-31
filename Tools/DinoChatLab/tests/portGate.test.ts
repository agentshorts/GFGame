import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { createLocalCore, loadCorePort } from "../src/dinobook/corePort";
import { bindPort, portActionsOpen, unboundPort } from "../src/dinobook/portGate";

describe("dinobook port gate", () => {
  it("publishes nothing until the port resolves", async () => {
    const binding = unboundPort();
    expect(portActionsOpen(binding)).toBe(false);
    let release: (port: ReturnType<typeof createLocalCore>) => void = () => {};
    const pending = bindPort(binding, () => new Promise((resolve) => {
      release = resolve;
    }));
    expect(binding.port).toBeNull();
    expect(binding.ready).toBe(false);
    expect(portActionsOpen(binding)).toBe(false);
    const local = createLocalCore();
    release(local);
    await expect(pending).resolves.toBe(local);
    expect(binding.port).toBe(local);
    expect(portActionsOpen(binding)).toBe(true);
  });

  it("binds the tree core and does not start the panel on a local core", async () => {
    const binding = unboundPort();
    const port = await bindPort(binding);
    expect(port.source).toBe("src/core");
    expect(binding.port?.source).toBe("src/core");
    expect(portActionsOpen(binding)).toBe(true);

    const again = await loadCorePort();
    expect(again.source).toBe("src/core");

    const source = readFileSync(new URL("../src/dinobook/DinobookPanel.vue", import.meta.url), "utf8");
    expect(source).not.toMatch(/ref(?:<[^>]+>)?\(\s*createLocalCore\(\)/);
    expect(source).toContain("unboundPort()");
    expect(source).toContain("bindPort(binding)");
    expect(source).toContain("!portReady");
    expect(source).not.toContain('core.source === "src/core"');
  });
});
