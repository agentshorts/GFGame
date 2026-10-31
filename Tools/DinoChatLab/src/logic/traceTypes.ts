export const CLASSIC_SOURCES = ["eliza", "aiml", "rivescript", "megahal"] as const;

export type ClassicSource = (typeof CLASSIC_SOURCES)[number];

export type TraceSource =
  | ClassicSource
  | "webllm"
  | "canned"
  | "fallback"
  | "stats"
  | "alignment"
  | "none";

export type TraceKind = "chat" | "dinobook" | "payday" | "elder";

export type TraceStage =
  | "score"
  | "filter"
  | "steer"
  | "alignment"
  | "fillTarget"
  | "fallback"
  | "present"
  | "pick"
  | "elder"
  | "payday";

export interface TraceCandidate {
  source: TraceSource;
  /** Null when that engine produced nothing. */
  text: string | null;
  ms: number;
  score?: number;
  note?: string;
  fellBack?: boolean;
}

export interface TraceDecision {
  stage: TraceStage;
  chosen?: string | null;
  reason: string;
  detail?: Record<string, string | number | boolean | null>;
}

/** What the context model was given, and what it sent back. Text is null when it did not run. */
export interface LlmTrace {
  text: string | null;
  ms: number;
  persona: string;
  memory: string;
  /** The context window that was packed for the model. Chat uses the 4k layer. */
  context: string;
  prompt: string;
  loaded: boolean;
  steerText?: string | null;
  steerMs?: number;
}

export interface TraceAttribution {
  name: string;
  dinoId: string | null;
  postId: string | null;
  because: "parent" | "lead" | "self" | "none";
}

export interface TraceEvent {
  id: string;
  at: string;
  kind: TraceKind;
  dinoId: string;
  dinoName: string;
  runId?: string;
  eventId?: string;
  eventLabel?: string;
  input?: string;
  finalText: string;
  chosenSource: TraceSource;
  reason: string;
  candidates: TraceCandidate[];
  llm: LlmTrace | null;
  decisions: TraceDecision[];
  timings: Record<string, number>;
  attribution?: TraceAttribution;
}

export const SOURCE_LABEL: Record<TraceSource, string> = {
  eliza: "ELIZA",
  aiml: "AIML",
  rivescript: "RiveScript",
  megahal: "MegaHAL",
  webllm: "WebLLM",
  canned: "Template",
  fallback: "Fallback",
  stats: "Record",
  alignment: "Alignment",
  none: "None",
};
