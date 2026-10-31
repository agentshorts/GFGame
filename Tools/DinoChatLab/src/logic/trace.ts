import {
  CLASSIC_SOURCES,
  SOURCE_LABEL,
  type ClassicSource,
  type LlmTrace,
  type TraceCandidate,
  type TraceDecision,
  type TraceEvent,
  type TraceSource,
} from "./traceTypes";

export { CLASSIC_SOURCES, SOURCE_LABEL };
export type {
  ClassicSource,
  LlmTrace,
  TraceAttribution,
  TraceCandidate,
  TraceDecision,
  TraceEvent,
  TraceKind,
  TraceSource,
  TraceStage,
} from "./traceTypes";

const MAX_TRACES = 400;
const TRACE_KEY = "dcl:trace";

let events: TraceEvent[] = [];
let enabled = true;
let serial = 0;
const listeners = new Set<() => void>();

function emit(): void {
  for (const listener of listeners) listener();
}

try {
  const saved = globalThis.localStorage?.getItem(TRACE_KEY);
  if (saved === "0") enabled = false;
  if (saved === "1") enabled = true;
} catch {
  /* storage is optional */
}

export function roundMs(ms: number): number {
  return Math.round(ms * 10) / 10;
}

export function nextTraceId(): string {
  serial += 1;
  return `tr-${serial}`;
}

export function traceEnabled(): boolean {
  return enabled;
}

export function setTraceEnabled(on: boolean): void {
  enabled = on;
  try {
    globalThis.localStorage?.setItem(TRACE_KEY, on ? "1" : "0");
  } catch {
    /* storage is optional */
  }
  emit();
}

export function recordTrace(event: TraceEvent): TraceEvent {
  events.push(event);
  if (events.length > MAX_TRACES) events.splice(0, events.length - MAX_TRACES);
  emit();
  return event;
}

export function recordTraces(rows: readonly TraceEvent[]): void {
  if (rows.length === 0) return;
  events.push(...rows);
  if (events.length > MAX_TRACES) events.splice(0, events.length - MAX_TRACES);
  emit();
}

export function listTraces(): readonly TraceEvent[] {
  return events;
}

export function findTrace(id: string): TraceEvent | null {
  for (let index = events.length - 1; index >= 0; index -= 1) {
    const event = events[index];
    if (event?.id === id) return event;
  }
  return null;
}

export function clearTraces(): void {
  events = [];
  emit();
}

export function subscribeTraces(listener: () => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function tracesToJson(rows: readonly TraceEvent[], exportedAt: string): string {
  return JSON.stringify({
    kind: "dino-chat-traces",
    version: 1,
    exportedAt,
    events: rows,
  }, null, 2);
}

export function filterTraces(
  rows: readonly TraceEvent[],
  filter: { dinoId?: string; source?: string },
): TraceEvent[] {
  return rows.filter((event) => {
    if (filter.dinoId && filter.dinoId !== "all" && event.dinoId !== filter.dinoId) return false;
    if (filter.source && filter.source !== "all") {
      const source = filter.source;
      const chosen = event.chosenSource === source;
      const produced = event.candidates.some((candidate) => candidate.source === source && candidate.text);
      if (!chosen && !produced) return false;
    }
    return true;
  });
}

export function keywordOverlap(utterance: string, text: string | null): number {
  if (!text) return 0;
  const words = utterance.toLowerCase().replace(/[^a-z0-9\s]/g, " ").split(/\s+/).filter((word) => word.length > 2);
  if (words.length === 0) return 0;
  const seen = new Set(text.toLowerCase().replace(/[^a-z0-9\s]/g, " ").split(/\s+/));
  let hits = 0;
  for (const word of words) {
    if (seen.has(word)) hits += 1;
  }
  return hits;
}

export function scoreCandidates(utterance: string, candidates: readonly TraceCandidate[]): TraceCandidate[] {
  return candidates.map((candidate) => ({
    ...candidate,
    score: keywordOverlap(utterance, candidate.text),
  }));
}

const CANDIDATE_ORDER: TraceSource[] = [
  ...CLASSIC_SOURCES,
  "webllm",
  "canned",
  "fallback",
  "stats",
  "alignment",
  "none",
];

export function mergeCandidates(base: readonly TraceCandidate[], extra: readonly TraceCandidate[]): TraceCandidate[] {
  const map = new Map<string, TraceCandidate>();
  for (const candidate of base) map.set(candidate.source, candidate);
  for (const candidate of extra) {
    const prior = map.get(candidate.source);
    if (!prior || prior.text == null) map.set(candidate.source, { ...prior, ...candidate });
  }
  return [...map.values()].sort((a, b) => CANDIDATE_ORDER.indexOf(a.source) - CANDIDATE_ORDER.indexOf(b.source));
}

export function templateCandidates(text: string | null, ms: number): TraceCandidate[] {
  return [
    ...CLASSIC_SOURCES.map((source) => ({
      source,
      text: null,
      ms: 0,
      note: "not run",
    })),
    { source: "webllm", text: null, ms: 0, note: "model not asked" },
    { source: "canned", text, ms, note: "template" },
  ];
}

export interface ProbeReply {
  text: string;
  fellBack?: boolean;
}

/** Ask each classic engine once. A throw or an empty string is recorded as none. */
export async function probeClassic(
  reply: (source: ClassicSource) => Promise<ProbeReply | null>,
  sources: readonly ClassicSource[] = CLASSIC_SOURCES,
): Promise<TraceCandidate[]> {
  const out: TraceCandidate[] = [];
  for (const source of sources) {
    const started = performance.now();
    try {
      const result = await reply(source);
      const ms = roundMs(performance.now() - started);
      const text = result?.text.trim() ?? "";
      if (!result || text.length === 0) {
        out.push({ source, text: null, ms, note: "none" });
      } else {
        out.push({
          source,
          text,
          ms,
          fellBack: result.fellBack === true,
          note: result.fellBack ? "fallback line" : undefined,
        });
      }
    } catch (error) {
      out.push({
        source,
        text: null,
        ms: roundMs(performance.now() - started),
        note: error instanceof Error ? error.message : "failed",
      });
    }
  }
  return out;
}

export function renderPrompt(messages: readonly { role: string; content: string }[]): string {
  return messages.map((message) => `[${message.role}]\n${message.content}`).join("\n\n");
}

export function idleLlm(input: { persona: string; memory: string; context: string; prompt?: string }): LlmTrace {
  const memory = input.memory.trim();
  const prompt = input.prompt ?? [
    "[system]",
    input.persona,
    memory ? `[memory]\n${memory}` : "[memory]\n(empty)",
    `[context]\n${input.context}`,
  ].join("\n");
  return {
    text: null,
    ms: 0,
    persona: input.persona,
    memory,
    context: input.context,
    prompt,
    loaded: false,
  };
}

export interface ChatTraceInput {
  id: string;
  at: string;
  dinoId: string;
  dinoName: string;
  inputText: string;
  candidates: TraceCandidate[];
  chosenSource: TraceSource;
  finalText: string;
  presentedDecisions?: TraceDecision[];
  steer?: TraceDecision;
  alignment?: TraceDecision;
  interest?: number;
  interestReason?: string;
  llm: LlmTrace | null;
  fallbackReason?: string;
  pickReason?: string;
  timings?: Record<string, number>;
}

export function buildChatTrace(input: ChatTraceInput): TraceEvent {
  const candidates = scoreCandidates(input.inputText, input.candidates);
  const scoreBits = candidates
    .filter((candidate) => candidate.text)
    .map((candidate) => `${SOURCE_LABEL[candidate.source]} ${candidate.score ?? 0}`)
    .join(", ");
  const decisions: TraceDecision[] = [];
  if (input.interest != null) {
    decisions.push({
      stage: "score",
      chosen: String(input.interest),
      reason: input.interestReason
        ? `Interest ${input.interest}. ${input.interestReason}`
        : `Interest ${input.interest}.`,
      detail: { interest: input.interest },
    });
  }
  if (scoreBits) {
    decisions.push({
      stage: "score",
      reason: `Keyword overlap is a reading aid. The picker does not use it. ${scoreBits}.`,
      detail: Object.fromEntries(candidates.map((candidate) => [candidate.source, candidate.score ?? 0])),
    });
  }
  if (input.alignment) decisions.push(input.alignment);
  if (input.steer) decisions.push(input.steer);
  if (input.presentedDecisions) decisions.push(...input.presentedDecisions);
  if (input.fallbackReason) {
    decisions.push({ stage: "fallback", reason: input.fallbackReason, chosen: input.finalText });
  }
  const pickReason = input.pickReason
    ?? (input.chosenSource === "webllm"
      ? "WebLLM rephrased the classic line, so the spoken line is the model."
      : input.chosenSource === "stats"
        ? "This elder quoted the player record instead of an engine."
        : input.chosenSource === "fallback"
          ? "The persona fallback stands."
          : input.chosenSource === "none"
            ? "No engine was picked."
            : `Picked ${SOURCE_LABEL[input.chosenSource]} because that line engine is selected.`);
  decisions.push({ stage: "pick", chosen: input.chosenSource, reason: pickReason });
  const timings: Record<string, number> = { ...(input.timings ?? {}) };
  for (const candidate of candidates) timings[candidate.source] = candidate.ms;
  if (input.llm && timings.webllm == null) timings.webllm = input.llm.ms;
  const reason = [pickReason, input.steer?.reason, input.alignment?.reason, input.fallbackReason]
    .filter((part): part is string => !!part && part.length > 0)
    .join(" ");
  return {
    id: input.id,
    at: input.at,
    kind: "chat",
    dinoId: input.dinoId,
    dinoName: input.dinoName,
    input: input.inputText,
    finalText: input.finalText,
    chosenSource: input.chosenSource,
    reason,
    candidates,
    llm: input.llm,
    decisions,
    timings,
  };
}
