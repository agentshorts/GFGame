import type { ChatMode, EngineId, Persona, TranscriptExport, TranscriptMessage } from "./types";
import { CHAT_CHANNEL } from "./types";

export function activePersonas(personas: Persona[], mode: ChatMode, selectedId: string, checkedIds: string[]): Persona[] {
  if (mode === "all-chat") {
    const picked = new Set(checkedIds);
    return personas.filter((persona) => picked.has(persona.id));
  }
  return personas.filter((persona) => persona.id === selectedId);
}

/** Rule engines only see the latest line. In all-chat the speaker's name is included so name keywords can match. */
export function engineUtterance(mode: ChatMode, speaker: string, text: string): string {
  if (mode === "all-chat") return `${speaker} ${text}`.trim();
  return text;
}

export function peerNames(personas: Persona[], selfId: string): string[] {
  return personas.filter((persona) => persona.id !== selfId).map((persona) => persona.name);
}

let seqCounter = 0;

export function resetSeq(start = 0): void {
  seqCounter = start;
}

export function nextMessage(partial: Omit<TranscriptMessage, "seq" | "channel" | "at"> & { at?: string }): TranscriptMessage {
  seqCounter += 1;
  return {
    seq: seqCounter,
    at: partial.at ?? new Date().toISOString(),
    channel: CHAT_CHANNEL,
    speaker: partial.speaker,
    role: partial.role,
    text: partial.text,
    rawText: partial.rawText,
    filtered: partial.filtered,
    replacements: partial.replacements,
    engine: partial.engine,
    mode: partial.mode,
    stats: partial.stats,
    intent: partial.intent,
    reasons: partial.reasons,
    speechHint: partial.speechHint,
    steer: partial.steer,
    contextTokens: partial.contextTokens,
    contextBudget: partial.contextBudget,
    rephrased: partial.rephrased,
    traceId: partial.traceId,
  };
}

export function transcriptDocument(options: {
  exportedAt: string;
  mode: ChatMode;
  engine: EngineId;
  modelId: string | null;
  temperature: number;
  maxTokens: number;
  playerName: string;
  systemPrompt: string;
  personas: Persona[];
  messages: TranscriptMessage[];
  context: TranscriptExport["context"];
}): TranscriptExport {
  return {
    kind: "dino-chat-lab",
    version: 2,
    exportedAt: options.exportedAt,
    mode: options.mode,
    engine: options.engine,
    modelId: options.modelId,
    temperature: options.temperature,
    maxTokens: options.maxTokens,
    playerName: options.playerName,
    systemPrompt: options.systemPrompt,
    personas: options.personas.map((persona) => ({
      id: persona.id,
      name: persona.name,
      alignment: persona.alignment,
    })),
    context: options.context,
    messages: options.messages,
  };
}

export function transcriptToJson(doc: TranscriptExport): string {
  return JSON.stringify(doc, null, 2);
}
