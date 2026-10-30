import type { Alignment, ChatMode, Intent, Persona } from "./types";

export function buildSystemPrompt(
  persona: Persona,
  mode: ChatMode,
  peerNames: string[],
  playerName: string,
  extra?: { alignment?: Alignment; intent?: Intent; summary?: string },
): string {
  const canon = persona.exampleLinesCanon
    ? "These example lines are copied from the game data."
    : "These example lines are lab stand-ins. They are not canon.";
  const lines = [
    `You are ${persona.name}, speaking in a PG game chat.`,
    `Lab alignment: ${extra?.alignment ?? persona.alignment}. This alignment is not from the game data.`,
    extra?.intent ? `Current intent: ${extra.intent}.` : "",
    extra?.summary ? `Memory summary:\n${extra.summary}` : "",
    `Personality: ${persona.personality}`,
    `Speech style: ${persona.speechStyle}`,
    `Content rule: ${persona.pgRule}`,
    `Keep the reply under ${persona.maxReplyLength} characters.`,
    playerName.length > 0
      ? `The player's name is ${playerName}.`
      : "The player has no name set. Do not invent one.",
    "Example lines:",
    ...persona.exampleLines.map((line) => `- ${line}`),
    canon,
  ];
  if (persona.loreFromRepo.length > 0) {
    lines.push("Notes copied from the repo:");
    lines.push(...persona.loreFromRepo.map((note) => `- ${note}`));
  }
  lines.push("Unknown:");
  lines.push(...persona.loreTodo.map((note) => `- ${note}`));
  lines.push("If a detail is marked TODO or is not in these notes, say you do not remember. Do not invent a backstory.");
  lines.push("The classic script produces the line. Stay inside the notes if you rephrase it.");
  if (mode === "direct") {
    lines.push("This is a 1:1 chat with the player. One short reply.");
  } else if (mode === "all-chat") {
    const peers = peerNames.length > 0 ? peerNames.join(", ") : "nobody else";
    lines.push(`This is all-chat. Others here: ${peers}. React in one short post. You may answer them by name.`);
  } else {
    lines.push("The user message is a game event. Answer that event in character. Do not narrate a new past.");
  }
  return lines.filter((line) => line.trim().length > 0).join("\n");
}

export interface PromptMessage {
  role: "system" | "user" | "assistant";
  content: string;
}

export function buildWebLlmMessages(
  systemPrompt: string,
  history: { speaker: string; role: "user" | "dino" | "system"; text: string }[],
  personaName: string,
): PromptMessage[] {
  const messages: PromptMessage[] = [{ role: "system", content: systemPrompt }];
  for (const message of history) {
    if (message.role === "system") continue;
    if (message.role === "dino" && message.speaker === personaName) {
      messages.push({ role: "assistant", content: message.text });
    } else if (message.role === "user") {
      messages.push({ role: "user", content: message.text });
    } else {
      messages.push({ role: "user", content: `${message.speaker}: ${message.text}` });
    }
  }
  return messages;
}
