import {
  clampBudget,
  DEFAULT_CONTEXT_BUDGET,
  estimateTokens,
  MAX_CONTEXT_BUDGET,
  MIN_CONTEXT_BUDGET,
} from "../core/types";

export {
  clampBudget,
  DEFAULT_CONTEXT_BUDGET,
  estimateTokens,
  MAX_CONTEXT_BUDGET,
  MIN_CONTEXT_BUDGET,
};

export interface ContextTurn {
  speaker: string;
  text: string;
}

export interface ContextState {
  budget: number;
  summary: string;
  worldNote: string;
  turns: ContextTurn[];
}

/** Rough English stand-in: four characters to a token. WebLLM's own usage is shown when a model runs. */

export function createContext(budget = DEFAULT_CONTEXT_BUDGET): ContextState {
  return { budget: clampBudget(budget), summary: "", worldNote: "", turns: [] };
}

export function tokenTotal(state: ContextState): number {
  const turns = state.turns.reduce((sum, turn) => sum + estimateTokens(`${turn.speaker}: ${turn.text}`), 0);
  return estimateTokens(state.summary) + estimateTokens(state.worldNote) + turns;
}

function pack(summary: string, worldNote: string, turns: ContextTurn[]): number {
  return tokenTotal({ budget: 0, summary, worldNote, turns });
}

function refit(state: ContextState): ContextState {
  let summary = state.summary.trim();
  let worldNote = state.worldNote.trim();
  const turns = state.turns.map((turn) => ({ ...turn }));
  const over = () => pack(summary, worldNote, turns) > state.budget;
  while (turns.length > 1 && over()) {
    const oldest = turns.shift();
    if (!oldest) break;
    const line = `${oldest.speaker}: ${oldest.text}`;
    summary = summary.length > 0 ? `${summary} ${line}` : line;
  }
  while (summary.length > 0 && over()) {
    summary = summary.slice(Math.min(48, summary.length)).trim();
  }
  while (worldNote.length > 0 && over()) {
    worldNote = worldNote.slice(0, Math.max(0, Math.floor(worldNote.length * 0.75))).trim();
  }
  const only = turns[0];
  if (turns.length === 1 && only) {
    let text = only.text;
    while (text.length > 0 && pack(summary, worldNote, [{ speaker: only.speaker, text }]) > state.budget) {
      text = text.slice(0, Math.max(0, Math.floor(text.length * 0.75))).trim();
    }
    turns[0] = { speaker: only.speaker, text };
  }
  return { budget: state.budget, summary, worldNote, turns };
}

export function remember(state: ContextState, turn: ContextTurn, worldNote: string): ContextState {
  return refit({
    budget: state.budget,
    summary: state.summary,
    worldNote,
    turns: [...state.turns, { speaker: turn.speaker, text: turn.text }],
  });
}

export function setBudget(state: ContextState, budget: number): ContextState {
  return refit({ ...state, budget: clampBudget(budget), turns: [...state.turns] });
}

export function contextBlob(state: ContextState): string {
  const parts = [
    state.summary ? `Summary: ${state.summary}` : "",
    state.worldNote ? `World: ${state.worldNote}` : "",
    ...state.turns.map((turn) => `${turn.speaker}: ${turn.text}`),
  ].filter((part) => part.length > 0);
  return parts.join("\n");
}
