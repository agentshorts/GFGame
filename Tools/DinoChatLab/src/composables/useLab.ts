import { computed, ref, watch } from "vue";
import { CREATURE_MOODS, CREATURE_SPECIES, type CreatureMood, type CreatureSpecies } from "../data/creature/types";
import { loadBundledPersonas } from "../data/loadPersonas";
import { createEngines } from "../engines";
import { ENGINE_CHOICES } from "../engines/types";
import { speciesForPersona } from "../engines/creatureEliza";
import { alignmentDecision, decide, DEFAULT_SMELL_FT, type Decision, type DecisionInput } from "../logic/alignment";
import { contextBlob, type ContextState } from "../logic/context";
import {
  applyAffinity,
  blankRelationship,
  budgetSplit,
  canSpeak,
  createDinoWindow,
  decideInterest,
  foldSummary,
  overBudgetFraction,
  planSpeak,
  prioritize,
  pushTurn,
  resizeWindow,
  scoreInterest,
  shouldSummarize,
  windowTokens,
  type DinoWindow,
} from "../core/memory";
import { affinityDelta, asksForPlayerRecord, statsDigest } from "../core/stats";
import { bindStorage, type KeyValueStore } from "../core/storage";
import { disableSqlite, enableSqlite, liveStorage, notifyStorageSwap, openPreferred } from "../core/storageSession";
import { exportBehaviorLog, readStorageBackend, type SqlitePlace } from "../core/sqliteStorage";
import {
  playerSubjectKey,
  RELATIONSHIP_KINDS,
  STAT_KINDS,
  normalizeKnobs,
  repBand,
  type MemoryKnobs,
  type QuotaLogEntry,
  type RelationshipKind,
  type SpeechQuota,
  type StatKind,
  type InterestThresholds,
} from "../core/types";
import { revokeObjectUrlLater } from "../logic/download";
import { clampChatText, presentReply } from "../logic/present";
import { buildSystemPrompt, buildContextMessages } from "../logic/prompt";
import {
  activePersonas,
  nextMessage,
  peerNames,
  resetSeq,
  transcriptDocument,
  transcriptToJson,
} from "../logic/session";
import {
  describeWorld,
  pickSteer,
  steerDecision,
} from "../logic/steer";
import {
  buildChatTrace,
  CLASSIC_SOURCES,
  idleLlm,
  nextTraceId,
  probeClassic,
  recordTrace,
  renderPrompt,
  roundMs,
  traceEnabled,
  type TraceCandidate,
  type TraceSource,
} from "../logic/trace";
import {
  ALIGNMENTS,
  type Alignment,
  type ChatMode,
  type ItemStack,
  type LineEngineId,
  type Persona,
  type TranscriptMessage,
} from "../logic/types";

const MANGO: ItemStack = { id: "mango-block", name: "Mango block", count: 1 };

function pageStore(): KeyValueStore | null {
  if (typeof localStorage === "undefined") return null;
  return localStorage;
}

function placeLabel(place: SqlitePlace): string {
  return place === "opfs" ? "OPFS" : "IndexedDB";
}

export function useLab() {
  let storage = liveStorage();
  bindStorage(storage);
  const personas = loadBundledPersonas();
  const engines = createEngines();

  const mode = ref<ChatMode>("direct");
  const engineId = ref<LineEngineId>("creature");
  const mood = ref<CreatureMood>("Friendly");
  const species = ref<CreatureSpecies>(speciesForPersona(personas[0]?.id ?? ""));
  const selectedId = ref(personas[0]?.id ?? "big-grumble");
  const checkedIds = ref(personas.map((persona) => persona.id));
  const playerName = ref("");
  const temperature = ref(0.7);
  const maxTokens = ref(80);
  const systemPrompt = ref("");
  const promptDirty = ref(false);
  const showFilter = ref(false);
  const messages = ref<TranscriptMessage[]>([]);
  const draft = ref("");
  const busy = ref(false);
  const notice = ref("");

  const inventory = ref<ItemStack[]>([]);
  const distanceFt = ref(40);
  const smellRangeFt = ref(DEFAULT_SMELL_FT);
  const winOdds = ref(0.4);
  const dinobook = ref(false);
  const alignmentPick = ref<Alignment | "persona">("persona");

  const knobs = ref<MemoryKnobs>(storage.getKnobs());
  const pageKv = pageStore();
  const wantsSqlite = pageKv ? readStorageBackend(pageKv) === "sqlite" : false;
  const sqliteOn = ref(false);
  const storageBusy = ref(wantsSqlite);
  const storageNote = ref(wantsSqlite
    ? "Opening the SQLite notebook…"
    : "localStorage is the notebook. SQLite keeps the same rows in this browser.");
  const windows = ref<Record<string, DinoWindow>>({});
  const statsTick = ref(0);
  const relTick = ref(0);
  const contextBudget = computed(() => knobs.value.contextBudget);
  const allChatBudget = computed(() => knobs.value.allChatBudget);
  const activeBudget = computed(() => (mode.value === "all-chat" ? knobs.value.allChatBudget : knobs.value.contextBudget));

  const lineEngines = ENGINE_CHOICES.filter((choice) => !choice.needsModel);
  const selected = computed(() => personas.find((persona) => persona.id === selectedId.value) ?? personas[0]!);
  const roster = computed(() => activePersonas(personas, mode.value, selectedId.value, checkedIds.value));
  watch(selectedId, (id) => {
    species.value = speciesForPersona(id);
  });

  function alignmentFor(persona: Persona): Alignment {
    if (persona.id === selected.value.id && alignmentPick.value !== "persona") return alignmentPick.value;
    return persona.alignment;
  }

  function subjectKey(): string {
    return playerSubjectKey(playerName.value);
  }

  function isElder(dinoId: string): boolean {
    return knobs.value.elderIds.includes(dinoId);
  }

  function personaByName(name: string): Persona | undefined {
    return personas.find((persona) => persona.name === name);
  }

  function relationshipFor(dinoId: string, key: string) {
    return storage.getRelationship(dinoId, key) ?? blankRelationship(dinoId, key, new Date().toISOString());
  }

  function digestFor(elder: boolean): string {
    const key = subjectKey();
    const rows = storage.getStats(key);
    if (!elder && rows.length === 0) return "";
    return statsDigest({
      rows,
      playerKey: key,
      familyId: knobs.value.familyId,
      elder,
      tokenCap: knobs.value.elderDigestCap,
      bands: knobs.value.repBands,
    });
  }

  function inputFor(persona: Persona): DecisionInput {
    statsTick.value;
    const row = storage.getStats(subjectKey()).find((item) => item.familyId === knobs.value.familyId);
    return {
      alignment: alignmentFor(persona),
      inventory: inventory.value.map((stack) => ({
        id: stack.id.trim(),
        name: stack.name.trim() || stack.id.trim(),
        count: Number.isFinite(stack.count) ? stack.count : 0,
      })),
      distanceFt: Number.isFinite(distanceFt.value) ? distanceFt.value : 0,
      smellRangeFt: Number.isFinite(smellRangeFt.value) ? smellRangeFt.value : DEFAULT_SMELL_FT,
      winOdds: Math.min(1, Math.max(0, Number.isFinite(winOdds.value) ? winOdds.value : 0)),
      dinobook: dinobook.value,
      wants: persona.wants,
      repBand: row ? repBand(row.rep, knobs.value.repBands) : undefined,
    };
  }

  const decisions = computed(() => roster.value.map((persona) => ({
    persona,
    decision: decide(inputFor(persona)),
  })));
  const focusDecision = computed(() => decide(inputFor(selected.value)));
  const previewSteer = computed(() => pickSteer({
    mode: mode.value,
    speaker: mode.value === "event" ? "Game" : (playerName.value.trim() || "You"),
    userText: draft.value,
    intent: focusDecision.value.intent,
    cues: selected.value.cues,
  }));
  const playerRecord = computed(() => {
    statsTick.value;
    return digestFor(isElder(selected.value.id));
  });
  const relationshipSummary = computed(() => {
    relTick.value;
    return storage.getRelationship(selected.value.id, subjectKey())?.summary ?? "";
  });
  const relationshipKind = computed(() => {
    relTick.value;
    return relationshipFor(selected.value.id, subjectKey()).kind;
  });
  const contextTokens = computed(() => {
    statsTick.value;
    relTick.value;
    const window = windows.value[selected.value.id];
    const blank = createDinoWindow(selected.value.id, activeBudget.value, "");
    const rel = storage.getRelationship(selected.value.id, subjectKey());
    return windowTokens(window ?? blank, rel, isElder(selected.value.id) ? playerRecord.value : "");
  });
  const contextSummary = computed(() => {
    const window = windows.value[selected.value.id];
    if (window) return window.memory.summary;
    return storage.getMemory(selected.value.id)?.summary ?? "";
  });
  const contextWorld = computed(() => windows.value[selected.value.id]?.worldNote ?? "");
  const generatedPrompt = computed(() => buildSystemPrompt(
    selected.value,
    mode.value,
    peerNames(roster.value, selected.value.id),
    playerName.value.trim(),
    {
      alignment: alignmentFor(selected.value),
      intent: focusDecision.value.intent,
      summary: contextSummary.value,
      relationship: relationshipSummary.value,
      playerRecord: playerRecord.value,
      elder: isElder(selected.value.id),
    },
  ));

  watch(generatedPrompt, (value) => {
    if (!promptDirty.value) systemPrompt.value = value;
  }, { immediate: true });

  function reloadNotebook() {
    knobs.value = storage.getKnobs();
    windows.value = {};
    relTick.value += 1;
    statsTick.value += 1;
  }

  async function onStorageToggle(on: boolean) {
    const kv = pageStore();
    if (!kv) {
      sqliteOn.value = false;
      storageNote.value = "This browser has no localStorage, so the notebook cannot move.";
      return;
    }
    storageBusy.value = true;
    try {
      if (on) {
        const opened = await enableSqlite(kv);
        storage = opened.storage;
        sqliteOn.value = true;
        storageNote.value = opened.copied
          ? `Copied memory, stats, and work into SQLite (${placeLabel(opened.place)}). Switching off reads localStorage again and leaves the file.`
          : `Using the SQLite notebook (${placeLabel(opened.place)}).`;
      } else {
        storage = disableSqlite(kv);
        sqliteOn.value = false;
        storageNote.value = "Back on localStorage. The SQLite file is still in this browser.";
      }
      reloadNotebook();
      notifyStorageSwap();
    } catch (error) {
      sqliteOn.value = false;
      storageNote.value = error instanceof Error ? error.message : String(error);
    } finally {
      storageBusy.value = false;
    }
  }

  const notebookKv = pageStore();
  if (notebookKv && wantsSqlite) {
    void openPreferred(notebookKv).then((opened) => {
      if (!opened) return;
      storage = opened.storage;
      sqliteOn.value = true;
      storageNote.value = opened.copied
        ? `Copied memory, stats, and work into SQLite (${placeLabel(opened.place)}). Switching off reads localStorage again and leaves the file.`
        : `Using the SQLite notebook (${placeLabel(opened.place)}).`;
      reloadNotebook();
    }).catch((error: unknown) => {
      sqliteOn.value = false;
      storageNote.value = error instanceof Error ? error.message : String(error);
    }).finally(() => {
      storageBusy.value = false;
    });
  }

  function exportBehavior() {
    const json = exportBehaviorLog(storage, new Date().toISOString());
    const blob = new Blob([json], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "dino-behavior-log.json";
    link.click();
    revokeObjectUrlLater(url);
  }

  function markPromptDirty() {
    promptDirty.value = true;
  }

  function rebuildPrompt() {
    promptDirty.value = false;
    systemPrompt.value = generatedPrompt.value;
  }

  function resizeAll(budget: number) {
    const next: Record<string, DinoWindow> = {};
    for (const [id, window] of Object.entries(windows.value)) {
      const resized = resizeWindow(window, budget, knobs.value.replyReserve, isElder(id));
      next[id] = resized;
      storage.putMemory(resized.memory);
    }
    windows.value = next;
  }

  function patchKnobs(partial: Partial<MemoryKnobs>) {
    knobs.value = normalizeKnobs({ ...knobs.value, ...partial });
    storage.putKnobs(knobs.value);
    resizeAll(activeBudget.value);
  }

  watch(mode, () => {
    resizeAll(activeBudget.value);
  });

  function onBudget(value: number) {
    patchKnobs({ contextBudget: value });
  }

  function onAllChatBudget(value: number) {
    patchKnobs({ allChatBudget: value });
  }

  function patchQuota(partial: Partial<SpeechQuota>) {
    patchKnobs({ quota: { ...knobs.value.quota, ...partial } });
  }

  function patchThresholds(partial: Partial<InterestThresholds>) {
    patchKnobs({ thresholds: { ...knobs.value.thresholds, ...partial } });
  }

  function setSummarizeEvery(value: number) {
    patchKnobs({ summarizeEvery: value });
  }

  function setFamily(familyId: string) {
    patchKnobs({ familyId });
  }

  function setRepDelta(kind: StatKind, value: number) {
    patchKnobs({ repDeltas: { ...knobs.value.repDeltas, [kind]: value } });
  }

  function toggleElder(on: boolean) {
    const ids = new Set(knobs.value.elderIds);
    if (on) ids.add(selected.value.id);
    else ids.delete(selected.value.id);
    patchKnobs({ elderIds: [...ids] });
  }

  function setRelationshipKind(kind: RelationshipKind) {
    const at = new Date().toISOString();
    const key = subjectKey();
    const rel = storage.getRelationship(selected.value.id, key) ?? blankRelationship(selected.value.id, key, at, kind);
    storage.putRelationship({ ...rel, kind });
    relTick.value += 1;
  }

  function recordWorld(kind: StatKind) {
    const at = new Date().toISOString();
    const key = subjectKey();
    storage.bumpStats({
      at,
      playerKey: key,
      familyId: knobs.value.familyId,
      dinoId: selected.value.id,
      kind,
      amount: 1,
      note: "lab",
    }, knobs.value.repDeltas);
    storage.appendBehavior({
      dinoId: selected.value.id,
      at,
      kind,
      payload: { playerKey: key, familyId: knobs.value.familyId, amount: 1, note: "lab" },
    });
    const delta = affinityDelta(kind);
    for (const persona of personas) {
      const rel = storage.getRelationship(persona.id, key) ?? blankRelationship(persona.id, key, at);
      storage.putRelationship(applyAffinity(rel, delta, at));
    }
    statsTick.value += 1;
    relTick.value += 1;
  }

  function addItem() {
    inventory.value.push({ id: "item", name: "Item", count: 1 });
  }

  function addMango() {
    const found = inventory.value.find((stack) => stack.id.trim() === MANGO.id);
    if (found) found.count += 1;
    else inventory.value.push({ ...MANGO });
  }

  function removeItem(index: number) {
    inventory.value.splice(index, 1);
  }

  function applyScene(scene: "attack" | "ambush" | "clear") {
    if (scene === "clear") {
      inventory.value = [];
      distanceFt.value = 40;
      smellRangeFt.value = DEFAULT_SMELL_FT;
      winOdds.value = 0.4;
      dinobook.value = false;
      alignmentPick.value = "persona";
      selectedId.value = personas[0]?.id ?? selectedId.value;
      mode.value = "direct";
      return;
    }
    selectedId.value = "tusksaw";
    mode.value = "direct";
    inventory.value = [{ id: "mango-block", name: "Mango block", count: 3 }];
    distanceFt.value = 12;
    smellRangeFt.value = DEFAULT_SMELL_FT;
    alignmentPick.value = "persona";
    winOdds.value = scene === "attack" ? 0.8 : 0.2;
    dinobook.value = scene === "ambush";
  }

  function toggleChecked(id: string) {
    const picked = new Set(checkedIds.value);
    if (picked.has(id)) picked.delete(id);
    else picked.add(id);
    checkedIds.value = personas.map((persona) => persona.id).filter((personaId) => picked.has(personaId));
  }

  function promptFor(persona: Persona, decision: Decision): string {
    if (promptDirty.value && persona.id === selected.value.id) return systemPrompt.value;
    const rel = relationshipFor(persona.id, subjectKey());
    return buildSystemPrompt(
      persona,
      mode.value,
      peerNames(roster.value, persona.id),
      playerName.value.trim(),
      {
        alignment: alignmentFor(persona),
        intent: decision.intent,
        summary: windows.value[persona.id]?.memory.summary ?? storage.getMemory(persona.id)?.summary ?? "",
        relationship: rel.summary,
        playerRecord: digestFor(isElder(persona.id)),
        elder: isElder(persona.id),
      },
    );
  }

  function quotaEntries(): QuotaLogEntry[] {
    return storage.listLog().map((entry) => {
      const action: QuotaLogEntry["action"] = entry.action === "reply" || entry.action === "post"
        ? entry.action
        : entry.action === "like" || entry.action === "skip_reply_to_reply"
          ? "like"
          : entry.action === "ignore"
            ? "ignore"
            : "skip";
      return { at: entry.at, dinoId: entry.dinoId, subjectKey: entry.subjectKey, action };
    });
  }

  function ensureWindow(dinoId: string): DinoWindow {
    const budget = activeBudget.value;
    const existing = windows.value[dinoId];
    if (existing) {
      return existing.budget === budget
        ? existing
        : resizeWindow(existing, budget, knobs.value.replyReserve, isElder(dinoId));
    }
    const created = createDinoWindow(dinoId, budget, new Date().toISOString());
    const saved = storage.getMemory(dinoId);
    if (saved) created.memory = { ...created.memory, ...saved, dinoId };
    return created;
  }

  function commitWindow(window: DinoWindow) {
    windows.value = { ...windows.value, [window.dinoId]: window };
    storage.putMemory(window.memory);
  }

  function steerState(window: DinoWindow, relationshipText: string): ContextState {
    return {
      budget: window.budget,
      summary: [window.memory.summary, relationshipText].filter((part) => part.length > 0).join("\n"),
      worldNote: window.worldNote,
      turns: window.turns.map((turn) => ({ speaker: turn.speaker, text: turn.text })),
    };
  }

  function talkSpecies(persona: Persona): CreatureSpecies {
    if (persona.id === selected.value.id) return species.value;
    return speciesForPersona(persona.id);
  }

  function isClassic(id: LineEngineId): id is (typeof CLASSIC_SOURCES)[number] {
    return (CLASSIC_SOURCES as readonly string[]).includes(id);
  }

  async function speak(
    persona: Persona,
    heard: { speaker: string; text: string },
    cycleId: string,
  ): Promise<boolean> {
    const started = performance.now();
    const now = new Date().toISOString();
    const other = personaByName(heard.speaker);
    const key = other ? other.id : subjectKey();
    const speakerIsDino = Boolean(other);
    const speakerIsSelf = heard.speaker === persona.name;
    let rel = relationshipFor(persona.id, key);
    const quota = canSpeak({
      dinoId: persona.id,
      subjectKey: key,
      log: quotaEntries(),
      now,
      quota: knobs.value.quota,
    });
    const worldInput = inputFor(persona);
    const interest = scoreInterest({
      text: heard.text,
      relationshipKind: rel.kind,
      affinity: rel.affinity,
      repBand: worldInput.repBand,
    });
    const gate = decideInterest({
      interest,
      thresholds: knobs.value.thresholds,
      relationshipKind: rel.kind,
      speakerIsDino,
      speakerIsSelf,
    });
    const plan = planSpeak({
      mode: mode.value,
      quotaOk: quota.ok,
      quotaReason: quota.reason,
      decision: gate,
    });
    const decision = decide(worldInput);
    const elder = isElder(persona.id);
    const record = digestFor(elder);
    const metadata = {
      interest,
      intent: decision.intent,
      quota: quota.reason,
      kind: rel.kind,
      statsDigest: record,
    };
    if (plan.action !== "reply") {
      storage.appendLog({
        id: "",
        at: now,
        cycleId,
        dinoId: persona.id,
        subjectKey: key,
        action: plan.logAction,
        detail: plan.reason,
        metadata,
        engine: engineId.value,
        modelId: null,
        promptTokens: null,
        completionTokens: null,
        durationMs: Math.round(performance.now() - started),
      });
      storage.appendBehavior({
        dinoId: persona.id,
        at: now,
        kind: plan.logAction,
        payload: { ...metadata, subjectKey: key, detail: plan.reason },
      });
      const quietText = plan.action === "like" ? "likes that." : `stays quiet (${plan.reason})`;
      const quiet = recordTrace(buildChatTrace({
        id: nextTraceId(),
        at: now,
        dinoId: persona.id,
        dinoName: persona.name,
        inputText: heard.text,
        candidates: [],
        chosenSource: "none",
        finalText: quietText,
        alignment: alignmentDecision(decision),
        interest,
        interestReason: plan.reason,
        llm: {
          text: null,
          ms: 0,
          persona: promptFor(persona, decision),
          memory: rel.summary,
          context: `budget ${activeBudget.value}\n${describeWorld(worldInput, decision.intent)}`,
          prompt: "",
          loaded: false,
        },
        pickReason: plan.reason,
      }));
      messages.value.push(nextMessage({
        speaker: persona.name,
        role: "system",
        text: quietText,
        rawText: plan.reason,
        filtered: false,
        replacements: [],
        mode: mode.value,
        intent: decision.intent,
        reasons: [plan.reason, ...decision.reasons],
        speechHint: decision.speechHint,
        steer: plan.reason,
        traceId: quiet.id,
      }));
      return plan.action === "like";
    }

    let window = ensureWindow(persona.id);
    const split = budgetSplit(window.budget, Math.max(knobs.value.replyReserve, maxTokens.value), elder);
    const world = describeWorld(worldInput, decision.intent);
    const tail = window.turns.at(-1);
    if (!tail || tail.speaker !== heard.speaker || tail.text !== heard.text) {
      window = pushTurn(window, {
        speaker: heard.speaker,
        text: heard.text,
        role: speakerIsDino ? "dino" : "user",
      }, world, split).window;
    }
    const steer = pickSteer({
      mode: mode.value,
      speaker: heard.speaker,
      userText: heard.text,
      intent: decision.intent,
      cues: persona.cues,
    });
    const asking = elder && asksForPlayerRecord(heard.text);
    const personaPrompt = promptFor(persona, decision);
    const memoryText = [window.memory.summary, rel.summary].filter((part) => part.length > 0).join("\n");
    const contextText = `budget ${window.budget}\n${contextBlob(steerState(window, rel.summary))}`;
    const history = window.turns.map((turn) => ({
      speaker: turn.speaker,
      role: turn.role ?? "user" as const,
      text: turn.text,
    }));
    const lineMessages = buildContextMessages(
      personaPrompt,
      history,
      persona.name,
      window.budget,
      maxTokens.value,
    );
    const llm = idleLlm({
      persona: personaPrompt,
      memory: memoryText,
      context: contextText,
      prompt: renderPrompt(lineMessages),
    });
    let line = "";
    let chosenSource: TraceSource = asking ? "stats" : engineId.value;
    let fallbackReason: string | undefined;
    let candidates: TraceCandidate[] = [];
    if (!asking) {
      const request = {
        persona,
        history,
        utterance: steer.utterance,
        mode: mode.value,
        systemPrompt: personaPrompt,
        temperature: temperature.value,
        maxTokens: maxTokens.value,
        peerNames: peerNames(roster.value, persona.id),
        contextBudget: window.budget,
        species: talkSpecies(persona),
        mood: mood.value,
        playerName: playerName.value.trim(),
      };
      const sources = traceEnabled()
        ? CLASSIC_SOURCES
        : isClassic(engineId.value) ? [engineId.value] : [];
      candidates = await probeClassic(async (source) => {
        const reply = await engines[source].reply(request);
        if (source === engineId.value && !line.trim()) {
          line = reply.text;
          if (reply.fellBack) {
            fallbackReason = `${source} had no line of its own and used the persona fallback.`;
          }
        }
        return { text: reply.text, fellBack: reply.fellBack };
      }, sources);
      if (engineId.value === "creature") {
        const creatureStarted = performance.now();
        const reply = await engines.creature.reply(request);
        const creatureMs = roundMs(performance.now() - creatureStarted);
        line = reply.text;
        chosenSource = reply.text.trim() ? "creature" : "fallback";
        if (reply.fellBack) {
          fallbackReason = "Creature talk had no matching rule and used the mood fallback.";
        }
        candidates = [
          ...candidates,
          {
            source: "creature",
            text: reply.text,
            ms: creatureMs,
            fellBack: reply.fellBack,
            note: `${talkSpecies(persona)} ${mood.value}`,
          },
        ];
      }
      if (!line.trim()) {
        line = persona.fallbackReplies[0] ?? "...";
        chosenSource = "fallback";
        fallbackReason = fallbackReason ?? "The selected engine returned nothing. The persona fallback stands.";
      }
    } else {
      line = record;
      candidates = [{ source: "stats", text: record, ms: 0, note: "player record" }];
    }
    const presented = presentReply(line, {
      playerName: playerName.value.trim(),
      maxReplyLength: persona.maxReplyLength,
      maxTokens: maxTokens.value,
    });
    const pushed = pushTurn(window, { speaker: persona.name, text: presented.text, role: "dino" }, world, split);
    window = pushed.window;
    rel = { ...rel, lastSeenAt: now };
    const needsSummary = shouldSummarize(window.memory, knobs.value.summarizeEvery)
      || overBudgetFraction(windowTokens(window, rel, elder ? record : ""), window.budget, knobs.value.summarizeAtFraction);
    if (needsSummary) {
      const folded = foldSummary({
        memory: window.memory,
        relationship: rel,
        turns: window.pending,
        now,
        dinoCap: knobs.value.dinoSummaryCap,
        relationshipCap: knobs.value.relationshipSummaryCap,
        modelSummary: null,
      });
      window = { ...window, memory: folded.memory, pending: [] };
      rel = folded.relationship;
    }
    storage.putRelationship(rel);
    relTick.value += 1;
    commitWindow(window);
    const spent = Math.round(performance.now() - started);
    const trace = recordTrace(buildChatTrace({
      id: nextTraceId(),
      at: now,
      dinoId: persona.id,
      dinoName: persona.name,
      inputText: heard.text,
      candidates,
      chosenSource,
      finalText: presented.text,
      presentedDecisions: presented.decisions,
      steer: steerDecision(steer),
      alignment: alignmentDecision(decision),
      interest,
      interestReason: plan.reason,
      llm,
      fallbackReason,
      pickReason: asking
        ? "This elder quoted the player record instead of an engine."
        : undefined,
      timings: { total: spent },
    }));
    const replyDetail = [plan.reason, steer.reason].filter((part) => part.length > 0).join(" ");
    storage.appendLog({
      id: "",
      at: now,
      cycleId,
      dinoId: persona.id,
      subjectKey: key,
      action: "reply",
      detail: replyDetail,
      metadata,
      engine: asking ? "stats" : engineId.value,
      modelId: null,
      promptTokens: null,
      completionTokens: null,
      durationMs: spent,
    });
    storage.appendBehavior({
      dinoId: persona.id,
      at: now,
      kind: "reply",
      payload: { ...metadata, subjectKey: key, detail: replyDetail },
    });
    messages.value.push(nextMessage({
      speaker: persona.name,
      role: "dino",
      text: presented.text,
      rawText: presented.rawText,
      filtered: presented.filtered,
      replacements: presented.replacements,
      engine: engineId.value,
      mode: mode.value,
      intent: decision.intent,
      reasons: [plan.reason, ...decision.reasons],
      speechHint: decision.speechHint,
      steer: asking ? "Elder quotes the player record." : steer.reason,
      contextTokens: windowTokens(window, rel, elder ? record : ""),
      contextBudget: window.budget,
      traceId: trace.id,
    }));
    return true;
  }

  function orderedRoster(ownerKey: string): Persona[] {
    if (mode.value !== "all-chat") return roster.value;
    return prioritize(roster.value, (persona) => relationshipFor(persona.id, ownerKey).kind);
  }

  async function send() {
    const text = clampChatText(draft.value);
    if (!text || busy.value) return;
    if (roster.value.length === 0) {
      notice.value = "Pick at least one dino for all-chat.";
      return;
    }
    const ordered = orderedRoster(subjectKey());
    if (ordered.length === 0) {
      notice.value = "Every checked dino is ignoring this speaker.";
      return;
    }
    busy.value = true;
    notice.value = "";
    const speaker = mode.value === "event" ? "Game" : (playerName.value.trim() || "You");
    const cycleId = `cycle-${Date.now()}`;
    try {
      messages.value.push(nextMessage({
        speaker,
        role: "user",
        text,
        rawText: text,
        filtered: false,
        replacements: [],
        mode: mode.value,
      }));
      let spoke = false;
      for (const persona of ordered) {
        if (await speak(persona, { speaker, text }, cycleId)) spoke = true;
      }
      if (!spoke) notice.value = "Nobody spoke. Check the cooldowns, the interest minimum, or ignore.";
      draft.value = "";
    } catch (error) {
      notice.value = error instanceof Error ? error.message : String(error);
    } finally {
      busy.value = false;
    }
  }

  async function anotherRound() {
    if (busy.value || mode.value !== "all-chat") return;
    const last = [...messages.value].reverse().find((message) => message.role !== "system");
    if (!last) {
      notice.value = "Say something first, then ask for another round.";
      return;
    }
    const ownerKey = personaByName(last.speaker)?.id ?? subjectKey();
    const ordered = orderedRoster(ownerKey);
    if (ordered.length === 0) {
      notice.value = "Every checked dino is ignoring this speaker.";
      return;
    }
    busy.value = true;
    notice.value = "";
    const cycleId = `cycle-${Date.now()}`;
    try {
      let spoke = false;
      for (const persona of ordered) {
        if (await speak(persona, { speaker: last.speaker, text: last.text }, cycleId)) spoke = true;
      }
      if (!spoke) notice.value = "Nobody spoke. A dino will not answer the dino who just spoke unless they are rivals or interest is high.";
    } catch (error) {
      notice.value = error instanceof Error ? error.message : String(error);
    } finally {
      busy.value = false;
    }
  }

  function resetChat() {
    messages.value = [];
    draft.value = "";
    notice.value = "";
    resetSeq(0);
    for (const persona of personas) {
      storage.deleteMemory(persona.id);
      storage.deleteRelationships(persona.id);
    }
    windows.value = {};
    relTick.value += 1;
    rebuildPrompt();
    for (const engine of Object.values(engines)) engine.reset();
  }

  function exportTranscript() {
    const window = windows.value[selected.value.id];
    const doc = transcriptDocument({
      exportedAt: new Date().toISOString(),
      mode: mode.value,
      engine: engineId.value,
      modelId: null,
      temperature: temperature.value,
      maxTokens: maxTokens.value,
      playerName: playerName.value.trim(),
      systemPrompt: systemPrompt.value,
      personas: roster.value,
      messages: messages.value,
      context: {
        budget: activeBudget.value,
        tokens: contextTokens.value,
        summary: window?.memory.summary ?? "",
        worldNote: window?.worldNote ?? "",
      },
    });
    const blob = new Blob([transcriptToJson(doc)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "dino-chat-lab-transcript.json";
    link.click();
    revokeObjectUrlLater(url);
  }

  function colorFor(name: string): string {
    return personas.find((persona) => persona.name === name)?.color ?? "#8a7560";
  }

  function usePreset(text: string) {
    draft.value = text;
  }

  return {
    personas,
    alignments: ALIGNMENTS,
    lineEngines,
    mode,
    engineId,
    selectedId,
    checkedIds,
    playerName,
    temperature,
    maxTokens,
    systemPrompt,
    promptDirty,
    showFilter,
    messages,
    draft,
    busy,
    notice,
    inventory,
    distanceFt,
    smellRangeFt,
    winOdds,
    dinobook,
    alignmentPick,
    knobs,
    contextBudget,
    allChatBudget,
    activeBudget,
    contextTokens,
    contextSummary,
    contextWorld,
    playerRecord,
    relationshipSummary,
    relationshipKind,
    relationshipKinds: RELATIONSHIP_KINDS,
    statKinds: STAT_KINDS,
    elderOn: computed(() => isElder(selected.value.id)),
    familyId: computed(() => knobs.value.familyId),
    decisions,
    focusDecision,
    previewSteer,
    selected,
    mood,
    species,
    creatureMoods: CREATURE_MOODS,
    creatureSpecies: CREATURE_SPECIES,
    send,
    anotherRound,
    resetChat,
    exportTranscript,
    sqliteOn,
    storageBusy,
    storageNote,
    onStorageToggle,
    exportBehavior,
    addItem,
    addMango,
    removeItem,
    applyScene,
    toggleChecked,
    markPromptDirty,
    rebuildPrompt,
    onBudget,
    onAllChatBudget,
    patchQuota,
    patchThresholds,
    setSummarizeEvery,
    setFamily,
    setRepDelta,
    toggleElder,
    setRelationshipKind,
    recordWorld,
    colorFor,
    usePreset,
  };
}
