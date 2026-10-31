import {
  bindStorage,
  LocalStorageAdapter,
  StorageConflictError,
  type DecisionLogEntry,
  type DinoMemory,
  type DinoStorage,
  type DinoWork,
  type KeyValueStore,
  type PersonaSave,
  type PlayerStatsRow,
  type RelationshipMemory,
  type StatEvent,
  type StatEventDraft,
  type StoredPersona,
} from "../core";

export class StoreError extends Error {
  readonly status: number;

  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

export interface StorageOptions {
  apiBase: string | null;
  kv: KeyValueStore;
  owner?: string;
  fetchImpl?: typeof fetch;
}

/** Async face of `DinoStorage`, so the browser can also talk to PersonaHub. */
export interface ManagerStorage {
  listPersonas(): Promise<StoredPersona[]>;
  savePersona(input: PersonaSave): Promise<StoredPersona>;
  deletePersona(id: string): Promise<void>;
  getMemory(dinoId: string): Promise<DinoMemory | null>;
  putMemory(memory: DinoMemory): Promise<void>;
  getRelationship(dinoId: string, subjectKey: string): Promise<RelationshipMemory | null>;
  putRelationship(memory: RelationshipMemory): Promise<void>;
  appendLog(entry: DecisionLogEntry): Promise<void>;
  listLog(dinoId: string, limit: number): Promise<DecisionLogEntry[]>;
  getStats(playerKey: string): Promise<PlayerStatsRow[]>;
  bumpStats(event: StatEventDraft): Promise<StatEvent>;
  getWork(dinoId: string): Promise<DinoWork | null>;
  creditWages(dinoId: string, amount: number): Promise<DinoWork>;
}

function wrapLocal(storage: DinoStorage): ManagerStorage {
  return {
    async listPersonas() {
      return storage.listPersonas();
    },
    async savePersona(input) {
      return storage.savePersona(input);
    },
    async deletePersona(id) {
      storage.deletePersona(id);
    },
    async getMemory(dinoId) {
      return storage.getMemory(dinoId);
    },
    async putMemory(memory) {
      storage.putMemory(memory);
    },
    async getRelationship(dinoId, subjectKey) {
      return storage.getRelationship(dinoId, subjectKey);
    },
    async putRelationship(memory) {
      storage.putRelationship(memory);
    },
    async appendLog(entry) {
      storage.appendLog(entry);
    },
    async listLog(dinoId, limit) {
      return storage.listLog(dinoId, limit);
    },
    async getStats(playerKey) {
      return storage.getStats(playerKey);
    },
    async bumpStats(event) {
      return storage.bumpStats(event);
    },
    async getWork(dinoId) {
      return storage.getWork(dinoId);
    },
    async creditWages(dinoId, amount) {
      return storage.creditWages(dinoId, amount);
    },
  };
}

export function createLocalStorage(kv: KeyValueStore): ManagerStorage {
  const adapter = new LocalStorageAdapter(kv);
  bindStorage(adapter);
  return wrapLocal(adapter);
}

/** Point the persona manager at an adapter the lab already opened. */
export function adoptAdapter(adapter: DinoStorage): ManagerStorage {
  bindStorage(adapter);
  return wrapLocal(adapter);
}

async function parseError(response: Response): Promise<string> {
  try {
    const body = await response.json() as { error?: string };
    return body.error || `Request failed (${response.status}).`;
  } catch {
    return `Request failed (${response.status}).`;
  }
}

export function createRemoteStorage(apiBase: string, fetchImpl: typeof fetch = fetch): ManagerStorage {
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
    if (!response.ok) throw new StoreError(await parseError(response), response.status);
    return response;
  }

  return {
    async listPersonas() {
      const body = await (await send("/personas")).json() as { personas: StoredPersona[] };
      return body.personas;
    },
    async savePersona(input) {
      const record = JSON.parse(input.json) as unknown;
      return await (await send(`/personas/${encodeURIComponent(input.id)}`, {
        method: "PUT",
        body: JSON.stringify({ version: input.version, record }),
      })).json() as StoredPersona;
    },
    async deletePersona(id) {
      await send(`/personas/${encodeURIComponent(id)}`, { method: "DELETE" });
    },
    async getMemory(dinoId) {
      const body = await (await send(`/memory?dinoId=${encodeURIComponent(dinoId)}`)).json() as { memory: DinoMemory | null };
      return body.memory;
    },
    async putMemory(memory) {
      await send("/memory", { method: "PUT", body: JSON.stringify(memory) });
    },
    async getRelationship(dinoId, subjectKey) {
      const query = `dinoId=${encodeURIComponent(dinoId)}&subjectKey=${encodeURIComponent(subjectKey)}`;
      const body = await (await send(`/relationships?${query}`)).json() as { relationship: RelationshipMemory | null };
      return body.relationship;
    },
    async putRelationship(memory) {
      await send("/relationships", { method: "PUT", body: JSON.stringify(memory) });
    },
    async appendLog(entry) {
      await send("/decision-log", { method: "POST", body: JSON.stringify(entry) });
    },
    async listLog(dinoId, limit) {
      const body = await (await send(`/decision-log?dinoId=${encodeURIComponent(dinoId)}&limit=${limit}`)).json() as { rows: DecisionLogEntry[] };
      return body.rows.slice().reverse();
    },
    async getStats(playerKey) {
      const body = await (await send(`/stats?playerKey=${encodeURIComponent(playerKey)}`)).json() as { stats: PlayerStatsRow[] };
      return body.stats;
    },
    async bumpStats(event) {
      const body = await (await send("/stats/events", { method: "POST", body: JSON.stringify(event) })).json() as { event: StatEvent };
      return body.event;
    },
    async getWork(dinoId) {
      const body = await (await send(`/work?dinoId=${encodeURIComponent(dinoId)}`)).json() as { work: DinoWork | null };
      return body.work;
    },
    async creditWages(dinoId, amount) {
      const body = await (await send("/work/wages", { method: "POST", body: JSON.stringify({ dinoId, amount }) })).json() as { work: DinoWork };
      return body.work;
    },
  };
}

export function createStorage(options: StorageOptions): ManagerStorage {
  if (options.apiBase) return createRemoteStorage(options.apiBase, options.fetchImpl);
  return createLocalStorage(options.kv);
}

export function apiBaseFromEnv(value: string | undefined): string | null {
  if (typeof value !== "string") return null;
  const trimmed = value.trim();
  return trimmed ? trimmed.replace(/\/$/, "") : null;
}

export { StorageConflictError };
