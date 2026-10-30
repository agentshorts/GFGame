import { applyStat, type DecisionLogRow, type DinoMemory, type PlayerStats, type RelationshipMemory, type StatEvent, type Storage, type StoredPersona } from "./shim/untilCore";

export const PERSONA_KEY = "dcl:personas";
export const LOG_KEY = "dcl:log";
export const LOCAL_LOG_KEEP = 200;

export class StoreError extends Error {
  readonly status: number;

  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

export interface KeyValue {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem(key: string): void;
}

export interface StorageOptions {
  apiBase: string | null;
  kv: KeyValue;
  owner?: string;
  fetchImpl?: typeof fetch;
}

function memoryKey(dinoId: string): string {
  return `dcl:memory:${dinoId}`;
}

function relKey(dinoId: string, subjectKey: string): string {
  return `dcl:rel:${dinoId}:${encodeURIComponent(subjectKey)}`;
}

function statsKey(playerKey: string): string {
  return `dcl:stats:${encodeURIComponent(playerKey)}`;
}

function readJson<T>(kv: KeyValue, key: string, fallback: T): T {
  const raw = kv.getItem(key);
  if (!raw) return fallback;
  try {
    return JSON.parse(raw) as T;
  } catch {
    return fallback;
  }
}

export function createLocalStorage(kv: KeyValue, owner = "local"): Storage {
  return {
    async listPersonas() {
      return readJson<StoredPersona[]>(kv, PERSONA_KEY, []);
    },
    async savePersona(input) {
      const all = readJson<StoredPersona[]>(kv, PERSONA_KEY, []);
      const index = all.findIndex((row) => row.id === input.id);
      const now = new Date().toISOString();
      if (index < 0) {
        if (input.version !== 0) throw new StoreError("someone else edited this, reload", 409);
        const created: StoredPersona = {
          id: input.id,
          owner,
          version: 1,
          updatedAt: now,
          record: input.record,
        };
        all.push(created);
        kv.setItem(PERSONA_KEY, JSON.stringify(all));
        return created;
      }
      const current = all[index]!;
      if (current.owner !== owner) throw new StoreError("Only the owner can edit this dino.", 403);
      if (current.version !== input.version) throw new StoreError("someone else edited this, reload", 409);
      const next: StoredPersona = {
        ...current,
        version: current.version + 1,
        updatedAt: now,
        record: input.record,
      };
      all[index] = next;
      kv.setItem(PERSONA_KEY, JSON.stringify(all));
      return next;
    },
    async deletePersona(id) {
      const all = readJson<StoredPersona[]>(kv, PERSONA_KEY, []);
      const row = all.find((item) => item.id === id);
      if (row && row.owner !== owner) throw new StoreError("Only the owner can delete this dino.", 403);
      kv.setItem(PERSONA_KEY, JSON.stringify(all.filter((item) => item.id !== id)));
    },
    async getMemory(dinoId) {
      return readJson<DinoMemory | null>(kv, memoryKey(dinoId), null);
    },
    async putMemory(memory) {
      kv.setItem(memoryKey(memory.dinoId), JSON.stringify(memory));
    },
    async getRelationship(dinoId, subjectKey) {
      return readJson<RelationshipMemory | null>(kv, relKey(dinoId, subjectKey), null);
    },
    async putRelationship(memory) {
      kv.setItem(relKey(memory.dinoId, memory.subjectKey), JSON.stringify(memory));
    },
    async appendLog(row) {
      const all = readJson<DecisionLogRow[]>(kv, LOG_KEY, []);
      all.push(row);
      kv.setItem(LOG_KEY, JSON.stringify(all.slice(-LOCAL_LOG_KEEP)));
    },
    async tailLog(dinoId, limit) {
      const all = readJson<DecisionLogRow[]>(kv, LOG_KEY, []);
      return all.filter((row) => row.dinoId === dinoId).slice(-limit).reverse();
    },
    async getStats(playerKey, familyId) {
      const bag = readJson<{ rows: PlayerStats[] }>(kv, statsKey(playerKey), { rows: [] });
      return bag.rows.find((row) => row.familyId === familyId) ?? null;
    },
    async bumpStats(event) {
      const bag = readJson<{ rows: PlayerStats[]; events: StatEvent[] }>(kv, statsKey(event.playerKey), { rows: [], events: [] });
      bag.events.push(event);
      const stats = applyStat(bag.rows, event, event.at);
      kv.setItem(statsKey(event.playerKey), JSON.stringify(bag));
      return stats;
    },
  };
}

async function parseError(response: Response): Promise<string> {
  try {
    const body = await response.json() as { error?: string };
    return body.error || `Request failed (${response.status}).`;
  } catch {
    return `Request failed (${response.status}).`;
  }
}

export function createRemoteStorage(apiBase: string, fetchImpl: typeof fetch = fetch): Storage {
  const base = apiBase.replace(/\/$/, "");
  async function send(path: string, init?: RequestInit): Promise<Response> {
    const response = await fetchImpl(`${base}${path}`, {
      ...init,
      credentials: "include",
      headers: {
        "content-type": "application/json",
        ...(init?.headers ?? {}),
      },
    });
    return response;
  }

  return {
    async listPersonas() {
      const response = await send("/personas");
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
      const body = await response.json() as { personas: StoredPersona[] };
      return body.personas;
    },
    async savePersona(input) {
      const response = await send(`/personas/${encodeURIComponent(input.id)}`, {
        method: "PUT",
        body: JSON.stringify({ version: input.version, record: input.record }),
      });
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
      return await response.json() as StoredPersona;
    },
    async deletePersona(id) {
      const response = await send(`/personas/${encodeURIComponent(id)}`, { method: "DELETE" });
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
    },
    async getMemory(dinoId) {
      const response = await send(`/memory?dinoId=${encodeURIComponent(dinoId)}`);
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
      const body = await response.json() as { memory: DinoMemory | null };
      return body.memory;
    },
    async putMemory(memory) {
      const response = await send("/memory", { method: "PUT", body: JSON.stringify(memory) });
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
    },
    async getRelationship(dinoId, subjectKey) {
      const query = `dinoId=${encodeURIComponent(dinoId)}&subjectKey=${encodeURIComponent(subjectKey)}`;
      const response = await send(`/relationships?${query}`);
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
      const body = await response.json() as { relationship: RelationshipMemory | null };
      return body.relationship;
    },
    async putRelationship(memory) {
      const response = await send("/relationships", { method: "PUT", body: JSON.stringify(memory) });
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
    },
    async appendLog(row) {
      const response = await send("/decision-log", { method: "POST", body: JSON.stringify(row) });
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
    },
    async tailLog(dinoId, limit) {
      const response = await send(`/decision-log?dinoId=${encodeURIComponent(dinoId)}&limit=${limit}`);
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
      const body = await response.json() as { rows: DecisionLogRow[] };
      return body.rows;
    },
    async getStats(playerKey, familyId) {
      const query = `playerKey=${encodeURIComponent(playerKey)}&familyId=${encodeURIComponent(familyId)}`;
      const response = await send(`/stats?${query}`);
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
      const body = await response.json() as { stats: PlayerStats | null };
      return body.stats;
    },
    async bumpStats(event) {
      const response = await send("/stats/events", { method: "POST", body: JSON.stringify(event) });
      if (!response.ok) throw new StoreError(await parseError(response), response.status);
      const body = await response.json() as { stats: PlayerStats };
      return body.stats;
    },
  };
}

export function createStorage(options: StorageOptions): Storage {
  if (options.apiBase) return createRemoteStorage(options.apiBase, options.fetchImpl);
  return createLocalStorage(options.kv, options.owner ?? "local");
}

export function apiBaseFromEnv(value: string | undefined): string | null {
  if (typeof value !== "string") return null;
  const trimmed = value.trim();
  return trimmed ? trimmed.replace(/\/$/, "") : null;
}
