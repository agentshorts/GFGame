export const CHAT_CHANNEL = "All" as const;

/** Player chat on the game server is trimmed to this many characters. */
export const GAME_CHAT_MAX_CHARS = 200;

export type ChatMode = "direct" | "all-chat" | "event";

export type EngineId = "eliza" | "aiml" | "rivescript" | "megahal" | "webllm";

export type LineEngineId = "eliza" | "aiml" | "rivescript" | "megahal";

export const ALIGNMENTS = ["LG", "NG", "CG", "LN", "N", "CN", "LE", "NE", "CE"] as const;

export type Alignment = (typeof ALIGNMENTS)[number];

export const INTENTS = ["attack", "ambush", "barter", "beg", "protect", "extort", "ignore"] as const;

export type Intent = (typeof INTENTS)[number];

export interface ItemStack {
  id: string;
  name: string;
  count: number;
}

export interface Want {
  id: string;
  name: string;
}

export interface Cue {
  keywords: string[];
  replies: string[];
}

export interface RawScripts {
  eliza?: string;
  aiml?: string;
  rivescript?: string;
}

export interface Persona {
  id: string;
  name: string;
  color: string;
  /** Lab alignment. Not stored on any dino in the game data. */
  alignment: Alignment;
  /** Lab wants. Not the taming favourite unless a row says so. */
  wants: Want[];
  personality: string;
  speechStyle: string;
  pgRule: string;
  maxReplyLength: number;
  exampleLinesCanon: boolean;
  exampleLines: string[];
  loreFromRepo: string[];
  loreTodo: string[];
  greeting: string;
  farewell: string;
  fallbackReplies: string[];
  cues: Cue[];
  rawScripts?: RawScripts;
}

export interface Replacement {
  original: string;
  replacement: string;
}

export interface ReplyStats {
  modelId?: string;
  tokensPerSec?: number;
  prefillTokensPerSec?: number;
  promptTokens?: number;
  completionTokens?: number;
  vramMb?: number;
  bufferMb?: number;
  runtimeStatsText?: string;
  fromCache?: boolean;
}

export interface TranscriptMessage {
  seq: number;
  at: string;
  channel: typeof CHAT_CHANNEL;
  speaker: string;
  role: "user" | "dino" | "system";
  text: string;
  rawText: string;
  filtered: boolean;
  replacements: Replacement[];
  engine?: EngineId;
  mode: ChatMode;
  stats?: ReplyStats;
  intent?: Intent;
  reasons?: string[];
  speechHint?: string;
  steer?: string;
  contextTokens?: number;
  contextBudget?: number;
  rephrased?: boolean;
}

export interface TranscriptExport {
  kind: "dino-chat-lab";
  version: 2;
  exportedAt: string;
  mode: ChatMode;
  engine: EngineId;
  modelId: string | null;
  temperature: number;
  maxTokens: number;
  playerName: string;
  systemPrompt: string;
  personas: { id: string; name: string; alignment: Alignment }[];
  context: { budget: number; tokens: number; summary: string; worldNote: string };
  messages: TranscriptMessage[];
}
