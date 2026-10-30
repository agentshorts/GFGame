import type { EngineId, Persona, ReplyStats } from "../logic/types";
import type { ChatMode } from "../logic/types";
import type { PromptMessage } from "../logic/prompt";

export interface EngineRequest {
  persona: Persona;
  history: { speaker: string; role: "user" | "dino" | "system"; text: string }[];
  utterance: string;
  mode: ChatMode;
  systemPrompt: string;
  temperature: number;
  maxTokens: number;
  peerNames: string[];
}

export interface EngineReply {
  text: string;
  engine: EngineId;
  stats?: ReplyStats;
}

export interface ChatEngine {
  id: EngineId;
  label: string;
  blurb: string;
  needsModel: boolean;
  ready(persona: Persona): Promise<void>;
  reply(request: EngineRequest): Promise<EngineReply>;
  reset(personaId?: string): void;
}

export interface WebLlmCompletion {
  text: string;
  promptTokens?: number;
  completionTokens?: number;
  tokensPerSec?: number;
  prefillTokensPerSec?: number;
  runtimeStatsText?: string;
}

export interface WebLlmClient {
  loadedModelId: string | null;
  vramMb?: number;
  bufferMb?: number;
  fromCache: boolean;
  complete(args: {
    messages: PromptMessage[];
    temperature: number;
    maxTokens: number;
  }): Promise<WebLlmCompletion>;
}

export const ENGINE_CHOICES: { id: EngineId; label: string; blurb: string; needsModel: boolean }[] = [
  {
    id: "eliza",
    label: "ELIZA",
    blurb: "Keyword script. Works with no download.",
    needsModel: false,
  },
  {
    id: "webllm",
    label: "WebLLM",
    blurb: "Context layer in front of the classic engines. Needs WebGPU.",
    needsModel: true,
  },
  {
    id: "aiml",
    label: "AIML",
    blurb: "A.L.I.C.E.-style categories.",
    needsModel: false,
  },
  {
    id: "rivescript",
    label: "RiveScript",
    blurb: "Trigger and reply script.",
    needsModel: false,
  },
  {
    id: "megahal",
    label: "MegaHAL",
    blurb: "Markov chatter trained on this dino's lines.",
    needsModel: false,
  },
];
