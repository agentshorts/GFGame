import { BUDGET_CAPS, canSpeak, defaultKnobs, fallbackSummarize, type ContextTurn, type DecisionLogEntry, type RelationshipKind } from "../core";
import { createEngines } from "../engines";
import { decide, DEFAULT_SMELL_FT } from "../logic/alignment";
import { presentReply } from "../logic/present";
import type { Persona } from "../logic/types";
import { scoreInterest } from "./interest";
import { personaForTest, type ManagedDino } from "./profile";

export interface TestLine {
  speaker: string;
  role: "user" | "dino" | "note";
  text: string;
}

export interface TurnInput {
  dino: ManagedDino;
  playerName: string;
  text: string;
  dinoSummary: string;
  relationshipSummary: string;
  turnsSinceSummary: number;
  interactions: number;
  cycleId: string;
  rep?: number;
  now?: Date;
  roll?: number;
}

export interface TurnResult {
  lines: TestLine[];
  log: DecisionLogEntry;
  dinoSummary: string;
  relationshipSummary: string;
  turnsSinceSummary: number;
  interactions: number;
  kind: RelationshipKind;
  affinity: number;
}

let enginePromise: ReturnType<typeof createEngines> | null = null;

function engines() {
  enginePromise ??= createEngines(() => null);
  return enginePromise;
}

async function classicReply(persona: Persona, utterance: string, playerName: string): Promise<string> {
  const engine = engines().eliza;
  engine.reset(persona.id);
  await engine.ready(persona);
  const reply = await engine.reply({
    persona,
    history: [],
    utterance,
    mode: "direct",
    systemPrompt: "",
    temperature: 0.7,
    maxTokens: 80,
    peerNames: [],
  });
  return presentReply(reply.text, {
    playerName,
    maxReplyLength: persona.maxReplyLength,
    maxTokens: 80,
  }).text;
}

export async function runManagerTurn(input: TurnInput): Promise<TurnResult> {
  const started = input.now ?? new Date();
  const player = input.playerName.trim() || "you";
  const quota = canSpeak({
    dinoId: input.dino.persona.id,
    subjectKey: player,
    log: [],
    now: started.toISOString(),
    quota: defaultKnobs().quota,
    random: () => input.roll ?? 0,
  });
  const interest = scoreInterest(input.text, input.dino.profile, player, input.rep ?? 0);
  const decision = decide({
    alignment: input.dino.persona.alignment,
    inventory: [],
    distanceFt: 40,
    smellRangeFt: DEFAULT_SMELL_FT,
    winOdds: 0.4,
    dinobook: false,
    wants: input.dino.persona.wants,
  });
  const authored = input.dino.profile.relationships.find((row) => row.subjectKey.toLowerCase() === player.toLowerCase());
  const kind: RelationshipKind = authored?.kind ?? "neutral";
  const affinity = authored?.affinity ?? 0;
  let action = quota.ok ? interest.action : "skip_quota";
  let replyText = "";
  const lines: TestLine[] = [{ speaker: player, role: "user", text: input.text }];
  if (action === "reply") {
    const persona = personaForTest(input.dino);
    replyText = await classicReply(persona, input.text, player);
    lines.push({ speaker: input.dino.persona.name, role: "dino", text: replyText });
  } else if (action === "like") {
    lines.push({ speaker: input.dino.persona.name, role: "note", text: "Liked. No reply." });
  } else if (action === "ignore") {
    lines.push({ speaker: input.dino.persona.name, role: "note", text: "Ignored. Interest is under the comment minimum." });
  } else {
    lines.push({ speaker: input.dino.persona.name, role: "note", text: "Stayed quiet. Quota said no." });
  }

  const turns: ContextTurn[] = [{ speaker: player, text: input.text, role: "user" }];
  if (replyText) turns.push({ speaker: input.dino.persona.name, text: replyText, role: "dino" });
  const dinoSummary = fallbackSummarize(input.dinoSummary, turns, BUDGET_CAPS.dinoSummary);
  const relationshipSummary = fallbackSummarize(input.relationshipSummary, turns, BUDGET_CAPS.relationship);
  const memoryDelta = [
    input.dinoSummary === dinoSummary ? "dino summary unchanged" : "dino summary updated",
    input.relationshipSummary === relationshipSummary ? "relationship summary unchanged" : "relationship summary updated",
  ].join("; ");
  const finished = Date.now();
  const log: DecisionLogEntry = {
    id: crypto.randomUUID(),
    at: started.toISOString(),
    cycleId: input.cycleId,
    dinoId: input.dino.persona.id,
    subjectKey: player,
    action,
    detail: `${action} interest ${interest.interest}`,
    metadata: {
      interest: interest.interest,
      intent: decision.intent,
      reason: decision.reasons.join(" "),
      quota: quota.reason,
      memoryDelta,
      hits: interest.hits,
      mins: input.dino.profile.interest,
    },
    engine: action === "reply" ? "eliza" : "none",
    modelId: null,
    promptTokens: null,
    completionTokens: null,
    durationMs: Math.max(0, finished - started.getTime()),
  };
  return {
    lines,
    log,
    dinoSummary,
    relationshipSummary,
    turnsSinceSummary: input.turnsSinceSummary + 1,
    interactions: input.interactions + 1,
    kind,
    affinity,
  };
}
