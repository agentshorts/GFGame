/**
 * Dismissal and step for the lab guides.
 * Stored in localStorage under `dcl:wizards`. Missing storage is a no-op.
 */

export const WIZARD_IDS = ["crew", "events", "traces"] as const;

export type WizardId = (typeof WIZARD_IDS)[number];

export const WIZARD_STORAGE_KEY = "dcl:wizards";

/** Kept in step with `WIZARDS` in copy.ts. The wizard tests check both. */
export const WIZARD_STEP_COUNTS: Record<WizardId, number> = {
  crew: 4,
  events: 7,
  traces: 4,
};

export interface WizardRecord {
  dismissed: boolean;
  step: number;
}

export type WizardStore = Record<WizardId, WizardRecord>;

export interface GuideStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

export function wizardStepCount(id: WizardId): number {
  return WIZARD_STEP_COUNTS[id];
}

export function emptyWizardRecord(): WizardRecord {
  return { dismissed: false, step: 0 };
}

export function emptyWizardStore(): WizardStore {
  return {
    crew: emptyWizardRecord(),
    events: emptyWizardRecord(),
    traces: emptyWizardRecord(),
  };
}

export function clampWizardStep(step: unknown, count: number): number {
  const max = Math.max(0, count - 1);
  if (typeof step !== "number" || !Number.isFinite(step)) return 0;
  return Math.min(max, Math.max(0, Math.floor(step)));
}

function resolveStorage(storage?: GuideStorage | null): GuideStorage | null {
  if (storage) return storage;
  if (storage === null) return null;
  try {
    const candidate = globalThis.localStorage;
    if (!candidate) return null;
    return candidate;
  } catch {
    return null;
  }
}

function readText(storage: GuideStorage | null, key: string): string | null {
  if (!storage) return null;
  try {
    return storage.getItem(key);
  } catch {
    return null;
  }
}

function writeText(storage: GuideStorage | null, key: string, value: string): void {
  if (!storage) return;
  try {
    storage.setItem(key, value);
  } catch {
    /* storage is optional */
  }
}

function recordFrom(value: unknown, count: number): WizardRecord {
  if (!value || typeof value !== "object") return emptyWizardRecord();
  const row = value as Record<string, unknown>;
  return {
    dismissed: row.dismissed === true,
    step: clampWizardStep(row.step, count),
  };
}

export function readWizardStore(storage?: GuideStorage | null): WizardStore {
  const store = emptyWizardStore();
  const text = readText(resolveStorage(storage), WIZARD_STORAGE_KEY);
  if (!text) return store;
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    return store;
  }
  if (!parsed || typeof parsed !== "object") return store;
  const raw = parsed as Record<string, unknown>;
  for (const id of WIZARD_IDS) {
    store[id] = recordFrom(raw[id], wizardStepCount(id));
  }
  return store;
}

export function writeWizardStore(store: WizardStore, storage?: GuideStorage | null): void {
  writeText(resolveStorage(storage), WIZARD_STORAGE_KEY, JSON.stringify(store));
}

export function readWizardRecord(id: WizardId, storage?: GuideStorage | null): WizardRecord {
  return readWizardStore(storage)[id];
}

function update(id: WizardId, next: WizardRecord, storage?: GuideStorage | null): WizardStore {
  const store = readWizardStore(storage);
  store[id] = next;
  writeWizardStore(store, storage);
  return store;
}

/** Skip and Done both dismiss the guide and leave it reopenable. */
export function dismissWizard(id: WizardId, storage?: GuideStorage | null): WizardStore {
  const current = readWizardRecord(id, storage);
  return update(id, { ...current, dismissed: true }, storage);
}

export function skipWizard(id: WizardId, storage?: GuideStorage | null): WizardStore {
  return dismissWizard(id, storage);
}

export function reopenWizard(id: WizardId, storage?: GuideStorage | null): WizardStore {
  return update(id, { dismissed: false, step: 0 }, storage);
}

export function setWizardStep(id: WizardId, step: number, storage?: GuideStorage | null): WizardStore {
  const current = readWizardRecord(id, storage);
  return update(id, { ...current, step: clampWizardStep(step, wizardStepCount(id)) }, storage);
}

const PANEL_INFO_KEY = "dcl:panel-info";

function readPanelMap(storage?: GuideStorage | null): Record<string, boolean> {
  const text = readText(resolveStorage(storage), PANEL_INFO_KEY);
  if (!text) return {};
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    return {};
  }
  if (!parsed || typeof parsed !== "object") return {};
  const map: Record<string, boolean> = {};
  for (const [key, value] of Object.entries(parsed as Record<string, unknown>)) {
    if (value === true) map[key] = true;
  }
  return map;
}

export function readPanelOpen(id: string, storage?: GuideStorage | null): boolean {
  return readPanelMap(storage)[id] === true;
}

export function writePanelOpen(id: string, open: boolean, storage?: GuideStorage | null): void {
  const map = readPanelMap(storage);
  if (open) map[id] = true;
  else delete map[id];
  writeText(resolveStorage(storage), PANEL_INFO_KEY, JSON.stringify(map));
}

export { PANEL_INFO_KEY };
