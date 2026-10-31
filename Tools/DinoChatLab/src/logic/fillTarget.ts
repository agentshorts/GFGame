import type { TraceDecision } from "./traceTypes";

/**
 * {Target} matches FGFGiantRules::FillTarget. A name replaces the token. With no name, the token
 * and its lead-in are dropped, and a bare token becomes "you".
 * {Dragger} matches FGFLineTokens::Fill. The name is whoever turned protection off, or the holder
 * when that record is empty (draggerName). With no name, ", {Dragger}" is dropped and any other
 * token reads as "someone".
 */
const TARGET = "{Target}";
const DRAGGER = "{Dragger}";
const DRAGGER_FALLBACK = "someone";

function replaceAllCi(input: string, needle: string, replacement: string): string {
  const escaped = needle.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return input.replace(new RegExp(escaped, "gi"), replacement);
}

function hasToken(line: string, token: string): boolean {
  return line.toLowerCase().includes(token.toLowerCase());
}

function fillPlayerTarget(line: string, name: string): string {
  if (name.length > 0) {
    return replaceAllCi(line, TARGET, name);
  }
  let out = line;
  out = replaceAllCi(out, `, ${TARGET}`, "");
  out = replaceAllCi(out, ` ${TARGET}.`, "");
  out = replaceAllCi(out, ` ${TARGET}`, "");
  out = replaceAllCi(out, TARGET, "you");
  return out;
}

/** Prefer the player who turned protection off. Otherwise use the holder. */
export function draggerName(revokeName: string, holderName = ""): string {
  const revoke = revokeName.trim();
  if (revoke.length > 0) return revoke;
  return holderName.trim();
}

export function fillDragger(line: string, name: string): string {
  if (name.length > 0) return replaceAllCi(line, DRAGGER, name);
  let out = replaceAllCi(line, `, ${DRAGGER}`, "");
  out = replaceAllCi(out, DRAGGER, DRAGGER_FALLBACK);
  return out;
}

export function fillTarget(line: string, name: string, dragger = ""): string {
  return fillDragger(fillPlayerTarget(line, name), dragger);
}

export function fillTargetDecision(before: string, after: string, name: string, dragger = ""): TraceDecision {
  if (before === after) {
    return {
      stage: "fillTarget",
      chosen: after,
      reason: "No {Target} token in the line.",
      detail: { name, dragger, changed: false },
    };
  }
  const notes: string[] = [];
  if (hasToken(before, TARGET)) {
    notes.push(name.length > 0
      ? `Replaced {Target} with ${name}.`
      : "No player name. {Target} was dropped or became you.");
  }
  if (hasToken(before, DRAGGER)) {
    notes.push(dragger.length > 0
      ? `Replaced {Dragger} with ${dragger}.`
      : "No dragger name. {Dragger} was dropped or became someone.");
  }
  if (notes.length === 0) notes.push("Filled the line tokens.");
  return {
    stage: "fillTarget",
    chosen: after,
    reason: notes.join(" "),
    detail: { name, dragger, changed: true },
  };
}
