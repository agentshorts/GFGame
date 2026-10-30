import type { Persona } from "./types";

function xml(text: string): string {
  return text
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

function aimlPatterns(keyword: string): string[] {
  const word = keyword.toUpperCase();
  return [word, `${word} *`, `* ${word}`, `* ${word} *`];
}

function rivePatterns(keyword: string): string[] {
  return [
    keyword,
    `${keyword} *`,
    `* ${keyword}`,
    `* ${keyword} *`,
  ];
}

export function compileElizaScript(persona: Persona): string {
  const lines = [
    `initial: ${persona.greeting}`,
    `final: ${persona.farewell}`,
    "quit: bye",
    "quit: goodbye",
  ];
  persona.cues.forEach((cue, index) => {
    const rank = 40 - index;
    for (const keyword of cue.keywords) {
      lines.push(`key: ${keyword} ${rank}`);
      lines.push("  decomp: *");
      for (const reply of cue.replies) {
        lines.push(`  reasmb: ${reply}`);
      }
    }
  });
  lines.push("key: xnone");
  lines.push("  decomp: *");
  for (const reply of persona.fallbackReplies) {
    lines.push(`  reasmb: ${reply}`);
  }
  return lines.join("\n");
}

export function compileAiml(persona: Persona): string {
  const categories: string[] = [];
  for (const cue of persona.cues) {
    const template = xml(cue.replies[0] ?? persona.fallbackReplies[0] ?? "...");
    for (const keyword of cue.keywords) {
      for (const pattern of aimlPatterns(keyword)) {
        categories.push(
          `  <category>\n    <pattern>${xml(pattern)}</pattern>\n    <template>${template}</template>\n  </category>`,
        );
      }
    }
  }
  const fallback = xml(persona.fallbackReplies[0] ?? "...");
  categories.push(
    `  <category>\n    <pattern>*</pattern>\n    <template>${fallback}</template>\n  </category>`,
  );
  return `<?xml version="1.0" encoding="UTF-8"?>\n<aiml version="1.0">\n${categories.join("\n")}\n</aiml>\n`;
}

export function compileRiveScript(persona: Persona): string {
  const lines = ["! version = 2.0", ""];
  for (const cue of persona.cues) {
    const reply = cue.replies[0] ?? persona.fallbackReplies[0] ?? "...";
    for (const keyword of cue.keywords) {
      for (const pattern of rivePatterns(keyword)) {
        lines.push(`+ ${pattern}`);
        lines.push(`- ${reply}`);
        lines.push("");
      }
    }
  }
  lines.push("+ *");
  lines.push(`- ${persona.fallbackReplies[0] ?? "..."}`);
  lines.push("");
  return lines.join("\n");
}

export function elizaSource(persona: Persona): string {
  const raw = persona.rawScripts?.eliza?.trim();
  return raw && raw.length > 0 ? raw : compileElizaScript(persona);
}

export function aimlSource(persona: Persona): string {
  const raw = persona.rawScripts?.aiml?.trim();
  return raw && raw.length > 0 ? raw : compileAiml(persona);
}

export function riveSource(persona: Persona): string {
  const raw = persona.rawScripts?.rivescript?.trim();
  return raw && raw.length > 0 ? raw : compileRiveScript(persona);
}

/** RiveScript treats {word} as a tag. The game token is restored after the reply. */
export function shieldTargetToken(script: string): string {
  return script.replaceAll("{Target}", "TARGETTOKEN");
}

export function restoreTargetToken(text: string): string {
  return text.replaceAll("TARGETTOKEN", "{Target}");
}
