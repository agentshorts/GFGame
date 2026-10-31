import type { TraceDecision } from "./traceTypes";

/**
 * Same replacement as FGFGiantRules::FillTarget in Source/GFGiant/Private/GFGiantTypes.cpp.
 * A name replaces {Target}. With no name, the token and its lead-in are dropped, and a bare token becomes "you".
 */
const TOKEN = "{Target}";

function replaceAllCi(input: string, needle: string, replacement: string): string {
  const escaped = needle.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return input.replace(new RegExp(escaped, "gi"), replacement);
}

export function fillTarget(line: string, name: string): string {
  if (name.length > 0) {
    return replaceAllCi(line, TOKEN, name);
  }
  let out = line;
  out = replaceAllCi(out, `, ${TOKEN}`, "");
  out = replaceAllCi(out, ` ${TOKEN}.`, "");
  out = replaceAllCi(out, ` ${TOKEN}`, "");
  out = replaceAllCi(out, TOKEN, "you");
  return out;
}

export function fillTargetDecision(before: string, after: string, name: string): TraceDecision {
  if (before === after) {
    return {
      stage: "fillTarget",
      chosen: after,
      reason: "No {Target} token in the line.",
      detail: { name, changed: false },
    };
  }
  return {
    stage: "fillTarget",
    chosen: after,
    reason: name.length > 0
      ? `Replaced {Target} with ${name}.`
      : "No player name. {Target} was dropped or became you.",
    detail: { name, changed: true },
  };
}
