import { computed, onMounted, ref, watch } from "vue";
import { loadBundledPersonas } from "../data/loadPersonas";
import type { Persona } from "../logic/types";
import { REP_BAND_THRESHOLDS, repBand, type DecisionLogEntry, type DinoMemory, type KeyValueStore, type RelationshipMemory, type StoredPersona } from "../core";
import {
  affinityFor,
  blankDino,
  exportDocument,
  importDocument,
  normalizeManaged,
  personaForTest,
  profileFromPersona,
  slugId,
  type AuthoredRelationship,
  type ManagedDino,
  type Sentiment,
  type SpeechPattern,
} from "./profile";
import { browserKv, claimUsername, readLocalUsername, remoteMe } from "./session";
import { readStorageBackend } from "../core/sqliteStorage";
import { enableSqlite } from "../core/storageSession";
import { adoptAdapter, apiBaseFromEnv, createStorage, StorageConflictError, StoreError, type ManagerStorage } from "./storage";
import { runManagerTurn, type TestLine } from "./testChat";

export interface EditorDino {
  managed: ManagedDino;
  owner: string;
  version: number;
  updatedAt: string;
  origin: "bundled" | "mine" | "shared";
  dirty: boolean;
}

function bundledRow(persona: Persona): EditorDino {
  return {
    managed: { persona, profile: profileFromPersona(persona) },
    owner: "bundled",
    version: 0,
    updatedAt: "",
    origin: "bundled",
    dirty: false,
  };
}

function storedRecord(row: StoredPersona): unknown {
  if (typeof row.json === "string" && row.json.length > 0) {
    try {
      return JSON.parse(row.json) as unknown;
    } catch {
      return null;
    }
  }
  const legacy = row as StoredPersona & { record?: unknown };
  return legacy.record ?? null;
}

function asManaged(record: unknown, fallbackId: string): ManagedDino | null {
  const normalized = normalizeManaged(fallbackId, record);
  return normalized.ok ? normalized.dino : null;
}

export function useManager(options?: { kv?: KeyValueStore; apiBase?: string | null; fetchImpl?: typeof fetch }) {
  const kv = options?.kv ?? browserKv();
  const apiBase = options?.apiBase === undefined ? apiBaseFromEnv(import.meta.env.VITE_DINO_API) : options.apiBase;
  const fetchImpl = options?.fetchImpl;
  const mode = apiBase ? "remote" : "local";

  const needsName = ref(true);
  const username = ref("");
  const nameDraft = ref("");
  const nameError = ref("");
  const nameBusy = ref(false);
  const dinos = ref<EditorDino[]>([]);
  const selectedId = ref("");
  const query = ref("");
  const patternFilter = ref<"all" | SpeechPattern>("all");
  const notice = ref("");
  const playerName = ref("");
  const draft = ref("");
  const chatBusy = ref(false);
  const transcripts = ref<Record<string, TestLine[]>>({});
  const logs = ref<DecisionLogEntry[]>([]);
  const dinoSummary = ref("");
  const relationshipSummary = ref("");
  const turnsSince = ref(0);
  const interactions = ref(0);
  const rep = ref(0);
  const earnings = ref(0);
  let storage: ManagerStorage | null = null;
  const cycles = new Map<string, string>();

  const selected = computed(() => dinos.value.find((row) => row.managed.persona.id === selectedId.value) ?? null);
  const editable = computed(() => selected.value?.origin === "mine");
  const visible = computed(() => {
    const q = query.value.trim().toLowerCase();
    return dinos.value.filter((row) => {
      if (patternFilter.value !== "all" && row.managed.profile.speechPattern !== patternFilter.value) return false;
      if (!q) return true;
      const persona = row.managed.persona;
      return persona.name.toLowerCase().includes(q) || persona.id.includes(q) || persona.personality.toLowerCase().includes(q);
    });
  });
  const groups = computed(() => ({
    bundled: visible.value.filter((row) => row.origin === "bundled"),
    mine: visible.value.filter((row) => row.origin === "mine"),
    shared: visible.value.filter((row) => row.origin === "shared"),
  }));
  const chatLines = computed(() => transcripts.value[selectedId.value] ?? []);

  function takenIds(): Set<string> {
    return new Set(dinos.value.map((row) => row.managed.persona.id));
  }

  function markDirty() {
    const row = selected.value;
    if (!row || row.origin !== "mine") return;
    row.dirty = true;
  }

  function replaceManaged(next: ManagedDino) {
    const row = selected.value;
    if (!row) return;
    row.managed = next;
    row.dirty = true;
  }

  async function refreshMemory() {
    const row = selected.value;
    if (!row || !storage) {
      dinoSummary.value = "";
      relationshipSummary.value = "";
      return;
    }
    const memory = await storage.getMemory(row.managed.persona.id);
    dinoSummary.value = memory?.summary ?? "";
    turnsSince.value = memory?.turnsSinceSummary ?? 0;
    const subject = playerName.value.trim() || username.value;
    const rel = subject ? await storage.getRelationship(row.managed.persona.id, subject) : null;
    relationshipSummary.value = rel?.summary ?? "";
    interactions.value = rel?.interactions ?? 0;
    logs.value = (await storage.listLog(row.managed.persona.id, 12)).slice().reverse();
    const stats = subject ? await storage.getStats(subject) : [];
    rep.value = stats.find((item) => item.familyId === "*")?.rep ?? stats[0]?.rep ?? 0;
    const work = await storage.getWork(row.managed.persona.id);
    earnings.value = work?.lifetimeEarnings ?? 0;
  }

  async function boot(name: string) {
    username.value = name;
    playerName.value = name;
    needsName.value = false;
    storage = !apiBase && readStorageBackend(kv) === "sqlite"
      ? adoptAdapter((await enableSqlite(kv)).storage)
      : createStorage({ apiBase, kv, owner: name, fetchImpl });
    const bundled = loadBundledPersonas().map(bundledRow);
    const bundledIds = new Set(bundled.map((row) => row.managed.persona.id));
    let stored: EditorDino[] = [];
    try {
      const rows = await storage.listPersonas();
      stored = rows.flatMap((row) => {
        if (bundledIds.has(row.id)) return [];
        const managed = asManaged(storedRecord(row), row.id);
        if (!managed) return [];
        return [{
          managed,
          owner: row.owner,
          version: row.version,
          updatedAt: row.updatedAt,
          origin: row.owner === name ? "mine" as const : "shared" as const,
          dirty: false,
        }];
      });
    } catch (error) {
      notice.value = error instanceof Error ? error.message : "Could not load saved dinos.";
    }
    dinos.value = [...bundled, ...stored];
    selectedId.value = dinos.value[0]?.managed.persona.id ?? "";
    await refreshMemory();
  }

  onMounted(async () => {
    if (apiBase) {
      const remote = await remoteMe(apiBase, fetchImpl);
      if (remote) {
        kv.setItem("dcl:username", remote);
        await boot(remote);
        return;
      }
      needsName.value = true;
      return;
    }
    const cookie = typeof document === "undefined" ? "" : document.cookie;
    const local = readLocalUsername(kv, cookie);
    if (local) await boot(local);
  });

  watch(selectedId, () => {
    void refreshMemory();
  });

  async function submitName() {
    nameBusy.value = true;
    nameError.value = "";
    try {
      const claimed = await claimUsername(nameDraft.value, apiBase, kv, fetchImpl);
      if (!claimed.ok) {
        nameError.value = claimed.error;
        return;
      }
      await boot(claimed.username);
    } finally {
      nameBusy.value = false;
    }
  }

  function select(id: string) {
    selectedId.value = id;
  }

  function addDino() {
    const managed = blankDino(takenIds(), dinos.value.length);
    dinos.value.push({
      managed,
      owner: username.value,
      version: 0,
      updatedAt: "",
      origin: "mine",
      dirty: true,
    });
    selectedId.value = managed.persona.id;
    notice.value = "Draft only. Save stores it in this browser.";
  }

  function duplicate(id = selectedId.value) {
    const source = dinos.value.find((row) => row.managed.persona.id === id);
    if (!source) return;
    const managed = JSON.parse(JSON.stringify(source.managed)) as ManagedDino;
    managed.persona.id = slugId(`${source.managed.persona.id}-copy`, takenIds());
    managed.persona.name = `Copy of ${source.managed.persona.name}`.slice(0, 40);
    managed.persona.exampleLinesCanon = false;
    dinos.value.push({
      managed,
      owner: username.value,
      version: 0,
      updatedAt: "",
      origin: "mine",
      dirty: true,
    });
    selectedId.value = managed.persona.id;
    notice.value = "Duplicated as a draft. Bundled dinos stay read-only.";
  }

  async function save() {
    const row = selected.value;
    if (!row || row.origin !== "mine" || !storage) return;
    const normalized = normalizeManaged(row.managed.persona.id, row.managed);
    if (!normalized.ok) {
      notice.value = normalized.error;
      return;
    }
    try {
      personaForTest(normalized.dino);
    } catch (error) {
      notice.value = error instanceof Error ? error.message : "That dino is not valid.";
      return;
    }
    try {
      const saved = await storage.savePersona({
        id: normalized.dino.persona.id,
        owner: username.value,
        version: row.version,
        json: JSON.stringify(normalized.dino),
      });
      row.managed = normalized.dino;
      row.version = saved.version;
      row.updatedAt = saved.updatedAt;
      row.owner = saved.owner;
      row.dirty = false;
      notice.value = `Saved ${normalized.dino.persona.name} (version ${saved.version}).`;
    } catch (error) {
      notice.value = error instanceof StoreError || error instanceof StorageConflictError
        ? error.message
        : "Could not save.";
    }
  }

  async function remove() {
    const row = selected.value;
    if (!row || row.origin !== "mine" || !storage) return;
    if (row.version > 0) {
      try {
        await storage.deletePersona(row.managed.persona.id);
      } catch (error) {
        notice.value = error instanceof Error ? error.message : "Could not delete.";
        return;
      }
    }
    dinos.value = dinos.value.filter((item) => item.managed.persona.id !== row.managed.persona.id);
    selectedId.value = dinos.value[0]?.managed.persona.id ?? "";
    notice.value = "Deleted.";
  }

  function setPattern(value: string) {
    if (value !== "CASUAL" && value !== "FORMAL" && value !== "EXPRESSIVE") return;
    const pattern: SpeechPattern = value;
    const row = selected.value;
    if (!row || row.origin !== "mine") return;
    row.managed.profile.speechPattern = pattern;
    const style = row.managed.persona.speechStyle.trim();
    if (style === "" || style === "CASUAL" || style === "FORMAL" || style === "EXPRESSIVE") {
      row.managed.persona.speechStyle = pattern;
    }
    row.dirty = true;
  }

  function setSpeechStyle(value: string) {
    const row = selected.value;
    if (!row || row.origin !== "mine") return;
    row.managed.persona.speechStyle = value.slice(0, 256);
    row.dirty = true;
  }

  function addTopic(name: string, sentiment: Sentiment) {
    const row = selected.value;
    if (!row || row.origin !== "mine") return;
    const clean = name.trim().slice(0, 32);
    if (clean.length < 2) {
      notice.value = "Topic names need at least 2 characters.";
      return;
    }
    if (row.managed.profile.topics.some((topic) => topic.name.toLowerCase() === clean.toLowerCase())) {
      notice.value = "That topic is already there.";
      return;
    }
    row.managed.profile.topics.push({ name: clean, sentiment });
    row.dirty = true;
  }

  function removeTopic(name: string) {
    const row = selected.value;
    if (!row || row.origin !== "mine") return;
    row.managed.profile.topics = row.managed.profile.topics.filter((topic) => topic.name !== name);
    row.dirty = true;
  }

  function addRelationship(subjectKey: string, kind: AuthoredRelationship["kind"]) {
    const row = selected.value;
    if (!row || row.origin !== "mine") return;
    const clean = subjectKey.trim().slice(0, 40);
    if (!clean) {
      notice.value = "Name the friend, rival, or family member.";
      return;
    }
    if (row.managed.profile.relationships.some((rel) => rel.subjectKey.toLowerCase() === clean.toLowerCase())) {
      notice.value = "That relationship is already there.";
      return;
    }
    row.managed.profile.relationships.push({ subjectKey: clean, kind, affinity: affinityFor(kind) });
    row.dirty = true;
  }

  function setRelationshipKind(subjectKey: string, kind: AuthoredRelationship["kind"]) {
    const row = selected.value;
    if (!row || row.origin !== "mine") return;
    const rel = row.managed.profile.relationships.find((item) => item.subjectKey === subjectKey);
    if (!rel) return;
    rel.kind = kind;
    rel.affinity = affinityFor(kind);
    row.dirty = true;
  }

  function setAffinity(subjectKey: string, affinity: number) {
    const row = selected.value;
    if (!row || row.origin !== "mine") return;
    const rel = row.managed.profile.relationships.find((item) => item.subjectKey === subjectKey);
    if (!rel) return;
    rel.affinity = Math.max(-10, Math.min(10, Math.round(affinity) || 0));
    row.dirty = true;
  }

  function removeRelationship(subjectKey: string) {
    const row = selected.value;
    if (!row || row.origin !== "mine") return;
    row.managed.profile.relationships = row.managed.profile.relationships.filter((rel) => rel.subjectKey !== subjectKey);
    row.dirty = true;
  }

  function setInterest(field: "engagement" | "post" | "comment" | "like", value: number) {
    const row = selected.value;
    if (!row || row.origin !== "mine") return;
    row.managed.profile.interest[field] = Math.max(1, Math.min(10, Math.round(value) || 1));
    row.dirty = true;
  }

  async function send() {
    const row = selected.value;
    const text = draft.value.trim();
    if (!row || !text || !storage || chatBusy.value) return;
    chatBusy.value = true;
    notice.value = "";
    const id = row.managed.persona.id;
    const cycleId = cycles.get(id) ?? `cycle-${crypto.randomUUID()}`;
    cycles.set(id, cycleId);
    try {
      const turn = await runManagerTurn({
        dino: row.managed,
        playerName: playerName.value.trim() || username.value,
        text,
        dinoSummary: dinoSummary.value,
        relationshipSummary: relationshipSummary.value,
        turnsSinceSummary: turnsSince.value,
        interactions: interactions.value,
        cycleId,
        rep: rep.value,
      });
      transcripts.value = { ...transcripts.value, [id]: [...(transcripts.value[id] ?? []), ...turn.lines] };
      draft.value = "";
      dinoSummary.value = turn.dinoSummary;
      relationshipSummary.value = turn.relationshipSummary;
      turnsSince.value = turn.turnsSinceSummary;
      interactions.value = turn.interactions;
      const subject = playerName.value.trim() || username.value;
      const now = turn.log.at;
      const memory: DinoMemory = {
        dinoId: id,
        summary: turn.dinoSummary,
        updatedAt: now,
        turnsSinceSummary: turn.turnsSinceSummary,
      };
      const relationship: RelationshipMemory = {
        dinoId: id,
        subjectKey: subject,
        kind: turn.kind,
        affinity: turn.affinity,
        summary: turn.relationshipSummary,
        lastSeenAt: now,
        interactions: turn.interactions,
      };
      await storage.putMemory(memory);
      await storage.putRelationship(relationship);
      await storage.appendLog(turn.log);
      logs.value = [turn.log, ...logs.value.filter((item) => item.id !== turn.log.id)].slice(0, 12);
    } catch (error) {
      notice.value = error instanceof Error ? error.message : "The test chat failed.";
    } finally {
      chatBusy.value = false;
    }
  }

  function resetChat() {
    const id = selectedId.value;
    draft.value = "";
    if (!id) return;
    transcripts.value = { ...transcripts.value, [id]: [] };
    cycles.set(id, `cycle-${crypto.randomUUID()}`);
  }

  async function resetMemory() {
    const row = selected.value;
    if (!row || !storage) return;
    const now = new Date().toISOString();
    const subject = playerName.value.trim() || username.value;
    dinoSummary.value = "";
    relationshipSummary.value = "";
    turnsSince.value = 0;
    interactions.value = 0;
    await storage.putMemory({ dinoId: row.managed.persona.id, summary: "", updatedAt: now, turnsSinceSummary: 0 });
    if (subject) {
      await storage.putRelationship({
        dinoId: row.managed.persona.id,
        subjectKey: subject,
        kind: "neutral",
        affinity: 0,
        summary: "",
        lastSeenAt: now,
        interactions: 0,
      });
    }
    notice.value = "Memory reset for this dino and player.";
  }

  function download() {
    const row = selected.value;
    if (!row) return;
    const blob = new Blob([exportDocument(row.managed)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = `${row.managed.persona.id}.json`;
    link.click();
    URL.revokeObjectURL(url);
  }

  async function onImport(file: File) {
    try {
      const data = JSON.parse(await file.text()) as unknown;
      const managed = importDocument(data, takenIds());
      dinos.value.push({
        managed,
        owner: username.value,
        version: 0,
        updatedAt: "",
        origin: "mine",
        dirty: true,
      });
      selectedId.value = managed.persona.id;
      notice.value = "Imported as a draft. Save to keep it.";
    } catch (error) {
      notice.value = error instanceof Error ? error.message : "Could not import that file.";
    }
  }

  return {
    mode,
    needsName,
    username,
    nameDraft,
    nameError,
    nameBusy,
    query,
    patternFilter,
    notice,
    playerName,
    draft,
    chatBusy,
    logs,
    dinoSummary,
    relationshipSummary,
    rep,
    earnings,
    repLabel: computed(() => repBand(rep.value, REP_BAND_THRESHOLDS)),
    repBands: REP_BAND_THRESHOLDS,
    selected,
    editable,
    groups,
    chatLines,
    refreshForPlayer: refreshMemory,
    submitName,
    select,
    addDino,
    duplicate,
    save,
    remove,
    markDirty,
    replaceManaged,
    setPattern,
    setSpeechStyle,
    addTopic,
    removeTopic,
    addRelationship,
    setRelationshipKind,
    setAffinity,
    removeRelationship,
    setInterest,
    send,
    resetChat,
    resetMemory,
    download,
    onImport,
  };
}
