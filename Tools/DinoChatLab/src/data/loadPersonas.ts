import { parsePersona } from "../logic/persona";
import type { Persona } from "../logic/types";

export function loadBundledPersonas(): Persona[] {
  const modules = import.meta.glob("../../personas/*.json", { eager: true }) as Record<string, { default: unknown }>;
  return Object.entries(modules)
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([, mod]) => parsePersona(mod.default));
}
