import type { RepBand } from "../core/types";
import type { Alignment, Intent, ItemStack, Want } from "./types";

/** Lab rules. Alignment, Dinobook, and mango blocks are not in the game data. */
export const LAB_DECISION_TODO =
  "TODO: alignment, Dinobook, and mango blocks are lab inputs. They are not defined in the repo. Mango block is a sample material, not a taming food.";

export const BEG_MANGO = "yo I can smell those mango blocks you got, can you spare one";
export const EXTORT_LINE = "just drop some and we won't attack";

export const DEFAULT_SMELL_FT = 30;
export const CONFIDENT_ODDS = 0.5;

export interface DecisionInput {
  alignment: Alignment;
  inventory: ItemStack[];
  distanceFt: number;
  smellRangeFt: number;
  /** Honest estimate, 0 to 1, that this dino beats the player alone. */
  winOdds: number;
  /** Lab social net. True when this dino can call others. */
  dinobook: boolean;
  wants: Want[];
  /** Set when this player already has a stat row. Omitted means the old rules. */
  repBand?: RepBand;
}

export interface Decision {
  intent: Intent;
  reasons: string[];
  speechHint: string;
  todo: string;
  smelledWant: string[];
}

function held(inventory: ItemStack[]): ItemStack[] {
  return inventory.filter((stack) => stack.count > 0);
}

function wantedNames(input: DecisionInput): string[] {
  if (input.distanceFt > input.smellRangeFt) return [];
  const ids = new Set(input.wants.map((want) => want.id));
  return held(input.inventory).filter((stack) => ids.has(stack.id)).map((stack) => stack.name);
}

function otherNames(input: DecisionInput): string[] {
  if (input.distanceFt > input.smellRangeFt) return [];
  const ids = new Set(input.wants.map((want) => want.id));
  return held(input.inventory).filter((stack) => !ids.has(stack.id)).map((stack) => stack.name);
}

export function speechHint(intent: Intent, itemName: string): string {
  const item = itemName.trim();
  switch (intent) {
    case "beg":
      return item === "Mango block" || item.length === 0
        ? BEG_MANGO
        : `yo I can smell that ${item} you got, can you spare one`;
    case "extort":
      return EXTORT_LINE;
    case "protect":
      return "I'll watch your back if you spare a drop.";
    case "barter":
      return "Trade you something for what you're carrying.";
    case "attack":
      return "I want what you're carrying.";
    case "ambush":
      return "Hold still. Dinobook can bring the others.";
    default:
      return "Nothing here I need to act on.";
  }
}

function facts(input: DecisionInput, wanted: string[], others: string[]): string[] {
  const inRange = input.distanceFt <= input.smellRangeFt;
  const lines = [
    `Alignment ${input.alignment}.`,
    inRange
      ? `In smell range: ${input.distanceFt} ft of ${input.smellRangeFt} ft.`
      : `Out of smell range: ${input.distanceFt} ft is past ${input.smellRangeFt} ft.`,
    `Win odds ${input.winOdds.toFixed(2)}. ${input.winOdds >= CONFIDENT_ODDS ? "Thinks it can win alone." : "Does not think it can win alone."}`,
    input.dinobook ? "Dinobook is open, so it can rally help." : "Dinobook is quiet.",
  ];
  if (input.repBand) lines.push(`Rep band ${input.repBand}.`);
  if (wanted.length > 0) lines.push(`Smells a wanted material: ${wanted.join(", ")}.`);
  else if (others.length > 0) lines.push(`Smells other goods: ${others.join(", ")}. Not on this dino's want list.`);
  else lines.push("Smells nothing it can act on.");
  return lines;
}

const THREAT_ALIGNMENTS = new Set<Alignment>(["LE", "NE", "CE", "CN"]);
const THREAT_INTENTS = new Set<Intent>(["attack", "ambush", "extort"]);

function applyRep(
  input: DecisionInput,
  chosen: { intent: Intent; because: string },
): { intent: Intent; because: string } {
  if (input.repBand === "hostile" && chosen.intent === "ignore" && THREAT_ALIGNMENTS.has(input.alignment)) {
    if (input.alignment === "LE") {
      return { intent: "extort", because: "Hostile rep pushes a lawful-evil dino toward extortion." };
    }
    return { intent: "attack", because: "Hostile rep pushes toward attack." };
  }
  if (input.repBand === "friendly" && THREAT_INTENTS.has(chosen.intent)) {
    return { intent: "barter", because: "Friendly rep turns a threat into a trade." };
  }
  return chosen;
}

function choose(input: DecisionInput, wanted: string[], others: string[]): { intent: Intent; because: string } {
  const confident = input.winOdds >= CONFIDENT_ODDS;
  const book = input.dinobook;
  const canThreaten = confident || book;
  const hintItem = wanted[0] ?? others[0] ?? "";

  switch (input.alignment) {
    case "NE":
      if (wanted.length === 0) return { intent: "ignore", because: "NE ignores goods it does not want." };
      if (book) return { intent: "ambush", because: "NE smells a want and can rally help on Dinobook, so it sets an ambush." };
      if (confident) return { intent: "attack", because: "NE smells a want and thinks it can win alone, so it attacks." };
      return { intent: "beg", because: `NE smells a want but will not fight. Hint: ${speechHint("beg", hintItem)}` };
    case "CE":
      if (wanted.length > 0 && confident) return { intent: "attack", because: "CE smells a want and thinks it can win, so it attacks." };
      if (wanted.length > 0 && book) return { intent: "ambush", because: "CE cannot win alone and calls Dinobook for an ambush." };
      if (wanted.length > 0) return { intent: "beg", because: "CE smells a want but has no fight and no help, so it begs." };
      if (others.length > 0 && confident) return { intent: "attack", because: "CE smells something and thinks it can win, so it attacks anyway." };
      return { intent: "ignore", because: "CE has nothing it will act on." };
    case "LE":
      if (wanted.length > 0 && canThreaten) return { intent: "extort", because: `LE can press and smells a want. ${EXTORT_LINE}` };
      if (wanted.length > 0) return { intent: "barter", because: "LE wants it but cannot press, so it offers a deal." };
      if (others.length > 0 && canThreaten) return { intent: "extort", because: `LE can press and smells a drop. ${EXTORT_LINE}` };
      return { intent: "ignore", because: "LE has no leverage." };
    case "CN":
      if (wanted.length > 0 && canThreaten) return { intent: "ambush", because: "CN smells a want and will jump it if it can win or call Dinobook." };
      if (wanted.length > 0) return { intent: "beg", because: "CN smells a want and just asks." };
      return { intent: "ignore", because: "CN does not care about the other goods." };
    case "LN":
      if (wanted.length > 0 || others.length > 0) return { intent: "barter", because: "LN sticks to a trade when it smells anything." };
      return { intent: "ignore", because: "LN has nothing to trade for." };
    case "N":
      if (wanted.length > 0 && confident) return { intent: "barter", because: "N will deal for a want when it thinks it can win." };
      return { intent: "ignore", because: "N lets it go." };
    case "LG":
      if ((wanted.length > 0 || others.length > 0) && confident) return { intent: "protect", because: "LG can win a fight, so it offers protection for a drop instead of attacking." };
      if (wanted.length > 0) return { intent: "beg", because: `LG will not attack. ${speechHint("beg", hintItem)}` };
      return { intent: "ignore", because: "LG leaves them alone." };
    case "NG":
      if (wanted.length > 0) return { intent: "beg", because: `NG asks. ${speechHint("beg", hintItem)}` };
      if (others.length > 0) return { intent: "barter", because: "NG will trade for goods it did not come for." };
      return { intent: "ignore", because: "NG smells nothing to ask for." };
    case "CG":
      if (wanted.length > 0) return { intent: "beg", because: `CG asks outright. ${speechHint("beg", hintItem)}` };
      if (others.length > 0 && confident) return { intent: "protect", because: "CG will guard a loaded player when it can win." };
      return { intent: "ignore", because: "CG wanders off." };
    default:
      return { intent: "ignore", because: "No rule matched." };
  }
}

export function decide(input: DecisionInput): Decision {
  const wanted = wantedNames(input);
  const others = otherNames(input);
  const reasons = facts(input, wanted, others);
  const chosen = applyRep(input, choose(input, wanted, others));
  const item = wanted[0] ?? others[0] ?? "";
  return {
    intent: chosen.intent,
    reasons: [...reasons, chosen.because],
    speechHint: speechHint(chosen.intent, item),
    todo: LAB_DECISION_TODO,
    smelledWant: wanted,
  };
}
