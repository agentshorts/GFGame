import { computed, onMounted, ref, watch } from "vue";
import { loadBundledPersonas } from "../data/loadPersonas";
import { createEngines } from "../engines";
import { ENGINE_CHOICES } from "../engines/types";
import type { WebLlmCompletion } from "../engines/types";
import { createBrowserWebLlm, detectWebGpu, modelChoice } from "../engines/webllmBrowser";
import type { LoadProgress } from "../engines/webllmBrowser";
import { decide, DEFAULT_SMELL_FT, type Decision, type DecisionInput } from "../logic/alignment";
import {
  contextBlob,
  createContext,
  DEFAULT_CONTEXT_BUDGET,
  remember,
  setBudget,
  tokenTotal,
  type ContextState,
} from "../logic/context";
import { clampChatText, presentReply } from "../logic/present";
import { buildSystemPrompt } from "../logic/prompt";
import {
  activePersonas,
  nextMessage,
  peerNames,
  resetSeq,
  transcriptDocument,
  transcriptToJson,
} from "../logic/session";
import {
  buildRephraseMessages,
  buildSteerMessages,
  describeWorld,
  parseModelSteer,
  pickSteer,
} from "../logic/steer";
import {
  ALIGNMENTS,
  type Alignment,
  type ChatMode,
  type ItemStack,
  type LineEngineId,
  type Persona,
  type ReplyStats,
  type TranscriptMessage,
} from "../logic/types";

const MANGO: ItemStack = { id: "mango-block", name: "Mango block", count: 1 };

export function useLab() {
  const personas = loadBundledPersonas();
  const browser = createBrowserWebLlm();
  const engines = createEngines(() => (browser.loadedModelId ? browser : null));

  const mode = ref<ChatMode>("direct");
  const engineId = ref<LineEngineId>("eliza");
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

  const contextBudget = ref(DEFAULT_CONTEXT_BUDGET);
  const context = ref<ContextState>(createContext(DEFAULT_CONTEXT_BUDGET));

  const webGpu = ref({ ok: false, detail: "Checking WebGPU…" });
  const modelId = ref(browser.defaultModelId ?? "");
  const loadProgress = ref<LoadProgress>({ progress: 0, text: "" });
  const modelError = ref("");
  const modelLoading = ref(false);
  const loadedModelId = ref<string | null>(null);
  const fromCache = ref(false);
  const lastStats = ref<ReplyStats | null>(null);
  const steerWithModel = ref(true);
  const rephraseWithModel = ref(false);

  const lineEngines = ENGINE_CHOICES.filter((choice) => !choice.needsModel);
  const selected = computed(() => personas.find((persona) => persona.id === selectedId.value) ?? personas[0]!);
  const roster = computed(() => activePersonas(personas, mode.value, selectedId.value, checkedIds.value));

  function alignmentFor(persona: Persona): Alignment {
    if (persona.id === selected.value.id && alignmentPick.value !== "persona") return alignmentPick.value;
    return persona.alignment;
  }

  function inputFor(persona: Persona): DecisionInput {
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
  const contextTokens = computed(() => tokenTotal(context.value));
  const contextSummary = computed(() => context.value.summary);
  const contextWorld = computed(() => context.value.worldNote);
  const generatedPrompt = computed(() => buildSystemPrompt(
    selected.value,
    mode.value,
    peerNames(roster.value, selected.value.id),
    playerName.value.trim(),
    {
      alignment: alignmentFor(selected.value),
      intent: focusDecision.value.intent,
      summary: context.value.summary,
    },
  ));

  watch(generatedPrompt, (value) => {
    if (!promptDirty.value) systemPrompt.value = value;
  }, { immediate: true });

  onMounted(async () => {
    webGpu.value = await detectWebGpu();
  });

  function markPromptDirty() {
    promptDirty.value = true;
  }

  function rebuildPrompt() {
    promptDirty.value = false;
    systemPrompt.value = generatedPrompt.value;
  }

  function onBudget(value: number) {
    const next = setBudget(context.value, value);
    context.value = next;
    contextBudget.value = next.budget;
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
    return buildSystemPrompt(
      persona,
      mode.value,
      peerNames(roster.value, persona.id),
      playerName.value.trim(),
      { alignment: alignmentFor(persona), intent: decision.intent, summary: context.value.summary },
    );
  }

  function statsFrom(result: WebLlmCompletion): ReplyStats {
    return {
      modelId: browser.loadedModelId ?? undefined,
      tokensPerSec: result.tokensPerSec,
      prefillTokensPerSec: result.prefillTokensPerSec,
      promptTokens: result.promptTokens,
      completionTokens: result.completionTokens,
      vramMb: browser.vramMb,
      bufferMb: browser.bufferMb,
      runtimeStatsText: result.runtimeStatsText,
      fromCache: browser.fromCache,
    };
  }

  async function maybeSteer(persona: Persona, decision: Decision): Promise<{ intent: Decision["intent"]; note: string; stats?: ReplyStats }> {
    if (!steerWithModel.value || !browser.loadedModelId) {
      return { intent: decision.intent, note: "" };
    }
    try {
      const result = await browser.complete({
        messages: buildSteerMessages(context.value, decision, persona.name, context.value.budget),
        temperature: temperature.value,
        maxTokens: Math.max(16, Math.min(120, Math.floor(maxTokens.value))),
      });
      const parsed = parseModelSteer(result.text, decision.intent);
      if (parsed.summary) context.value = { ...context.value, summary: parsed.summary };
      const note = parsed.usedModel
        ? `Context model chose ${parsed.intent}.`
        : "Context model returned no intent. The alignment module stands.";
      return { intent: parsed.intent, note, stats: statsFrom(result) };
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      return { intent: decision.intent, note: `Context model failed (${message}). The alignment module stands.` };
    }
  }

  async function speak(persona: Persona, heard: { speaker: string; text: string }): Promise<void> {
    const decision = decide(inputFor(persona));
    const steered = await maybeSteer(persona, decision);
    const finalDecision = { ...decision, intent: steered.intent };
    const steer = pickSteer({
      mode: mode.value,
      speaker: heard.speaker,
      userText: heard.text,
      intent: steered.intent,
      cues: persona.cues,
    });
    const reply = await engines[engineId.value].reply({
      persona,
      history: messages.value.map((message) => ({
        speaker: message.speaker,
        role: message.role,
        text: message.text,
      })),
      utterance: steer.utterance,
      mode: mode.value,
      systemPrompt: promptFor(persona, finalDecision),
      temperature: temperature.value,
      maxTokens: maxTokens.value,
      peerNames: peerNames(roster.value, persona.id),
    });
    let line = reply.text;
    let rephrased = false;
    let stats = reply.stats ?? steered.stats;
    if (rephraseWithModel.value && browser.loadedModelId) {
      try {
        const result = await browser.complete({
          messages: buildRephraseMessages({
            systemPrompt: promptFor(persona, finalDecision),
            classicLine: reply.text,
            memory: contextBlob(context.value),
            budget: context.value.budget,
          }),
          temperature: temperature.value,
          maxTokens: maxTokens.value,
        });
        if (result.text.trim().length > 0) {
          line = result.text.trim();
          rephrased = true;
          stats = statsFrom(result);
        }
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        notice.value = `Rephrase failed (${message}). The classic line stands.`;
      }
    }
    const presented = presentReply(line, {
      playerName: playerName.value.trim(),
      maxReplyLength: persona.maxReplyLength,
      maxTokens: maxTokens.value,
    });
    context.value = remember(
      context.value,
      { speaker: persona.name, text: presented.text },
      describeWorld(inputFor(persona), steered.intent),
    );
    if (stats) lastStats.value = stats;
    messages.value.push(nextMessage({
      speaker: persona.name,
      role: "dino",
      text: presented.text,
      rawText: presented.rawText,
      filtered: presented.filtered,
      replacements: presented.replacements,
      engine: engineId.value,
      mode: mode.value,
      stats,
      intent: steered.intent,
      reasons: decision.reasons,
      speechHint: decision.speechHint,
      steer: [steer.reason, steered.note].filter((part) => part.length > 0).join(" "),
      contextTokens: tokenTotal(context.value),
      contextBudget: context.value.budget,
      rephrased,
    }));
  }

  async function send() {
    const text = clampChatText(draft.value);
    if (!text || busy.value) return;
    if (roster.value.length === 0) {
      notice.value = "Pick at least one dino for all-chat.";
      return;
    }
    busy.value = true;
    notice.value = "";
    const speaker = mode.value === "event" ? "Game" : (playerName.value.trim() || "You");
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
      context.value = remember(
        context.value,
        { speaker, text },
        describeWorld(inputFor(selected.value), focusDecision.value.intent),
      );
      for (const persona of roster.value) {
        await speak(persona, { speaker, text });
      }
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
    if (roster.value.length === 0) {
      notice.value = "Pick at least one dino for all-chat.";
      return;
    }
    busy.value = true;
    notice.value = "";
    try {
      for (const persona of roster.value) {
        await speak(persona, { speaker: last.speaker, text: last.text });
      }
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
    lastStats.value = null;
    resetSeq(0);
    context.value = createContext(contextBudget.value);
    rebuildPrompt();
    for (const engine of Object.values(engines)) engine.reset();
  }

  function exportTranscript() {
    const doc = transcriptDocument({
      exportedAt: new Date().toISOString(),
      mode: mode.value,
      engine: engineId.value,
      modelId: browser.loadedModelId,
      temperature: temperature.value,
      maxTokens: maxTokens.value,
      playerName: playerName.value.trim(),
      systemPrompt: systemPrompt.value,
      personas: roster.value,
      messages: messages.value,
      context: {
        budget: context.value.budget,
        tokens: tokenTotal(context.value),
        summary: context.value.summary,
        worldNote: context.value.worldNote,
      },
    });
    const blob = new Blob([transcriptToJson(doc)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "dino-chat-lab-transcript.json";
    link.click();
    URL.revokeObjectURL(url);
  }

  async function loadModel() {
    if (!modelId.value || modelLoading.value) return;
    modelLoading.value = true;
    modelError.value = "";
    loadProgress.value = { progress: 0, text: "Starting…" };
    try {
      await browser.load(modelId.value, (progress) => {
        loadProgress.value = progress;
      });
      loadedModelId.value = browser.loadedModelId;
      fromCache.value = browser.fromCache;
    } catch (error) {
      modelError.value = error instanceof Error ? error.message : String(error);
      loadedModelId.value = browser.loadedModelId;
    } finally {
      modelLoading.value = false;
    }
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
    contextBudget,
    contextTokens,
    contextSummary,
    contextWorld,
    decisions,
    focusDecision,
    previewSteer,
    selected,
    webGpu,
    modelChoices: browser.catalog.choices,
    modelMissing: browser.catalog.missing,
    modelId,
    currentModel: computed(() => modelChoice(browser.catalog, modelId.value || null)),
    loadProgress,
    modelError,
    modelLoading,
    loadModel,
    loadedModelId,
    fromCache,
    lastStats,
    steerWithModel,
    rephraseWithModel,
    send,
    anotherRound,
    resetChat,
    exportTranscript,
    addItem,
    addMango,
    removeItem,
    applyScene,
    toggleChecked,
    markPromptDirty,
    rebuildPrompt,
    onBudget,
    colorFor,
    usePreset,
  };
}
