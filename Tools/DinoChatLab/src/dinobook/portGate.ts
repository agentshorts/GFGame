import { loadCorePort, type DinobookCorePort } from "./corePort";

export interface PortBinding {
  port: DinobookCorePort | null;
  ready: boolean;
}

/** Nothing is bound yet. Do not invent a local core while the real port is still loading. */
export function unboundPort(): PortBinding {
  return { port: null, ready: false };
}

export function portActionsOpen(binding: PortBinding): boolean {
  return binding.ready && binding.port !== null;
}

/**
 * Publish `load` only after it resolves. Until then the binding stays empty, so a click
 * during startup cannot land on a temporary local core.
 */
export async function bindPort(
  binding: PortBinding,
  load: () => Promise<DinobookCorePort> = loadCorePort,
): Promise<DinobookCorePort> {
  const port = await load();
  binding.port = port;
  binding.ready = true;
  return port;
}
