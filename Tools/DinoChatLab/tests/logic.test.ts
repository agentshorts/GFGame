import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import grumbleJson from "../personas/01-big-grumble.json";
import tusksawJson from "../personas/02-tusksaw.json";
import ripwingJson from "../personas/03-ripwing.json";
import scythepawJson from "../personas/04-scythepaw.json";
import lanternJson from "../personas/05-lanternmaw.json";
import birdJson from "../personas/06-chatty-bird.json";
import {
  BEG_MANGO,
  CONFIDENT_ODDS,
  decide,
  DEFAULT_SMELL_FT,
  EXTORT_LINE,
  speechHint,
  type DecisionInput,
} from "../src/logic/alignment";
import {
  clampBudget,
  createContext,
  DEFAULT_CONTEXT_BUDGET,
  estimateTokens,
  remember,
  setBudget,
  tokenTotal,
} from "../src/logic/context";
import { draggerName, fillTarget } from "../src/logic/fillTarget";
import { filterOutput } from "../src/logic/filter";
import { parsePersona } from "../src/logic/persona";
import { presentReply, replyCharBudget } from "../src/logic/present";
import { buildSystemPrompt } from "../src/logic/prompt";
import {
  activePersonas,
  engineUtterance,
  nextMessage,
  resetSeq,
  transcriptDocument,
  transcriptToJson,
} from "../src/logic/session";
import { restoreTargetToken, shieldTargetToken } from "../src/logic/scripts";
import { pickSteer } from "../src/logic/steer";
import { ALIGNMENTS, type Alignment, type Persona } from "../src/logic/types";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "../../..");
const linesCsv = readFileSync(resolve(root, "Data/CSV/DT_GiantDinoLines.csv"), "utf8");
const itemsCsv = readFileSync(resolve(root, "Data/CSV/DT_Items.csv"), "utf8");
const giantHeader = readFileSync(resolve(root, "Source/GFGiant/Public/GFGiantTypes.h"), "utf8");
const giantCsv = readFileSync(resolve(root, "Data/CSV/DT_GiantDino.csv"), "utf8");
const newDinos = readFileSync(resolve(root, "docs/NewDinos_v1.md"), "utf8");

const personas = [grumbleJson, tusksawJson, ripwingJson, scythepawJson, lanternJson, birdJson].map(parsePersona);
const grumble = personas[0]!;
const bird = personas[5]!;
const mango = { id: "mango-block", name: "Mango block" };

function input(partial: Partial<DecisionInput> & { alignment: Alignment }): DecisionInput {
  return {
    inventory: [],
    distanceFt: 40,
    smellRangeFt: DEFAULT_SMELL_FT,
    winOdds: 0.4,
    dinobook: false,
    wants: [mango],
    ...partial,
  };
}

describe("fillTarget", () => {
  it("matches the giant-line name cases", () => {
    expect(fillTarget("Hey {Target}! Where you going?", "Pip")).toBe("Hey Pip! Where you going?");
    expect(fillTarget("Why you running?", "Pip")).toBe("Why you running?");
    expect(fillTarget("Where you going, {Target}? Huh? Where you going?", "Pip"))
      .toBe("Where you going, Pip? Huh? Where you going?");
    expect(fillTarget("Where you going, {Target}? Huh? Where you going?", ""))
      .toBe("Where you going? Huh? Where you going?");
    expect(fillTarget("Hey. Hey. {Target}. Where you going?", "")).toBe("Hey. Hey. Where you going?");
    expect(fillTarget("Hey {Target}! Where you going?", "")).toBe("Hey! Where you going?");
    expect(fillTarget("Hey {target}!", "Pip")).toBe("Hey Pip!");
  });

  it("fills {Dragger} from the revoke name, then the holder", () => {
    expect(draggerName("Rex", "Sam")).toBe("Rex");
    expect(draggerName("  ", "Sam")).toBe("Sam");
    expect(draggerName("", "")).toBe("");
    const both = "{Dragger}, you get a gold star. {Target}, you get eaten.";
    expect(fillTarget(both, "Pip", draggerName("Rex", "Sam"))).toBe("Rex, you get a gold star. Pip, you get eaten.");
    expect(fillTarget("Thanks! You're a real pal, {Dragger}.", "Pip", "Rex")).toBe("Thanks! You're a real pal, Rex.");
    expect(fillTarget("{Target}, your friends don't like you much, huh?", "Pip", "Rex"))
      .toBe("Pip, your friends don't like you much, huh?");
    expect(fillTarget("Thanks! You're a real pal, {Dragger}.", "Pip", draggerName("", ""))).toBe("Thanks! You're a real pal.");
    expect(fillTarget("Thank {dragger}!", "", "Rex")).toBe("Thank Rex!");
    expect(fillTarget("Out you go, {Dragger}!", "", "")).toBe("Out you go!");
    expect(fillTarget("{Dragger} did this", "", "")).toBe("someone did this");
    expect(fillTarget("Left here by {Dragger}.", "", "")).toBe("Left here by someone.");
    expect(restoreTargetToken(shieldTargetToken("Thanks, {Dragger}. Hey {Target}."))).toBe("Thanks, {Dragger}. Hey {Target}.");
  });
});

describe("filter", () => {
  it("censors output and leaves the canned lines alone", () => {
    const hit = filterOutput("that is shit");
    expect(hit.filtered).toBe(true);
    expect(hit.text.toLowerCase()).not.toContain("shit");
    expect(hit.replacements.length).toBeGreaterThan(0);
    expect("that is shit").toContain(hit.replacements[0]?.original ?? "");
    for (const line of grumble.exampleLines) {
      expect(filterOutput(line).filtered).toBe(false);
    }
  });
});

describe("personas", () => {
  it("loads six lab personas with a mango want", () => {
    expect(personas.map((persona) => persona.id)).toEqual([
      "big-grumble",
      "tusksaw",
      "ripwing",
      "scythepaw",
      "lanternmaw",
      "chatty-bird",
    ]);
    expect(personas.map((persona) => persona.alignment)).toEqual(["CN", "NE", "CN", "CG", "LE", "NG"]);
    for (const persona of personas) {
      expect(persona.wants).toEqual([mango]);
      expect(persona.loreTodo.some((line) => line.includes("TODO"))).toBe(true);
      expect(persona.pgRule).toContain("PG");
    }
  });

  it("keeps Big Grumble's spoken lines inside the giant line sheet", () => {
    const spoken = [
      ...grumble.exampleLines,
      grumble.greeting,
      grumble.farewell,
      ...grumble.fallbackReplies,
      ...grumble.cues.flatMap((cue) => cue.replies),
    ];
    for (const line of spoken) expect(linesCsv).toContain(line);
    for (const note of grumble.loreFromRepo) {
      expect(linesCsv.includes(note) || giantHeader.includes(note) || giantCsv.includes(note)).toBe(true);
    }
  });

  it("keeps the other design-sheet notes inside NewDinos and leaves the bird empty", () => {
    for (const persona of personas.slice(1, 5)) {
      for (const note of persona.loreFromRepo) expect(newDinos).toContain(note);
    }
    expect(bird.loreFromRepo).toEqual([]);
    expect(bird.loreTodo.join(" ")).toContain("Ripwing");
    expect(itemsCsv.toLowerCase()).not.toContain("mango");
  });

  it("rejects a duplicate keyword", () => {
    const copy = structuredClone(grumbleJson) as Persona;
    copy.cues = [...copy.cues, { keywords: ["beg"], replies: ["nope"] }];
    expect(() => parsePersona(copy)).toThrow(/Duplicate keyword/);
  });
});

describe("prompt and present", () => {
  it("names the dino, the PG rule, and the TODO", () => {
    const prompt = buildSystemPrompt(grumble, "direct", [], "");
    expect(prompt).toContain("Big Grumble");
    expect(prompt).toContain(grumble.pgRule);
    expect(prompt).toContain("TODO");
    expect(prompt).toContain("Do not invent a backstory");
  });

  it("clips to the token budget after the filter", () => {
    expect(replyCharBudget(200, 80)).toBe(200);
    const shown = presentReply("Where you going, {Target}? Huh? Where you going?", {
      playerName: "Pip",
      maxReplyLength: 200,
      maxTokens: 5,
    });
    expect(shown.text.startsWith("Where you going")).toBe(true);
    expect(shown.text.endsWith("…")).toBe(true);
    expect(shown.text.length).toBeLessThanOrEqual(20);
    expect(shown.rawText).toBe("Where you going, Pip? Huh? Where you going?");
  });

  it("names both the dragger and the target before the clip", () => {
    const shown = presentReply("{Dragger}, you get a gold star. {Target}, you get eaten.", {
      playerName: "Pip",
      draggerName: "Rex",
      maxReplyLength: 200,
      maxTokens: 80,
    });
    expect(shown.rawText).toBe("Rex, you get a gold star. Pip, you get eaten.");
    expect(shown.decisions[0]?.reason).toContain("Rex");
    expect(shown.decisions[0]?.reason).toContain("Pip");
  });
});

describe("session", () => {
  it("keeps roster order and exports version 2", () => {
    expect(activePersonas(personas, "all-chat", "tusksaw", ["chatty-bird", "big-grumble"]).map((persona) => persona.id))
      .toEqual(["big-grumble", "chatty-bird"]);
    expect(engineUtterance("all-chat", "Pip", "hi")).toBe("Pip hi");
    resetSeq(0);
    const message = nextMessage({
      speaker: "You",
      role: "user",
      text: "hi",
      rawText: "hi",
      filtered: false,
      replacements: [],
      mode: "direct",
    });
    expect(message.seq).toBe(1);
    expect(message.channel).toBe("All");
    const doc = transcriptDocument({
      exportedAt: "2026-10-30T00:00:00.000Z",
      mode: "direct",
      engine: "eliza",
      modelId: null,
      temperature: 0.7,
      maxTokens: 80,
      playerName: "",
      systemPrompt: "prompt",
      personas: [grumble],
      messages: [message],
      context: { budget: 4096, tokens: 0, summary: "", worldNote: "" },
    });
    expect(doc.version).toBe(2);
    expect(JSON.parse(transcriptToJson(doc)).personas[0].alignment).toBe("CN");
  });
});

describe("alignment", () => {
  const carrying = [{ id: "mango-block", name: "Mango block", count: 3 }];

  it("has NE take mango when it can win, rally, or only beg", () => {
    expect(decide(input({ alignment: "NE", inventory: carrying, distanceFt: 12, winOdds: 0.8 })).intent).toBe("attack");
    expect(decide(input({ alignment: "NE", inventory: carrying, distanceFt: 12, winOdds: 0.2, dinobook: true })).intent).toBe("ambush");
    const beg = decide(input({ alignment: "NE", inventory: carrying, distanceFt: 12, winOdds: 0.2 }));
    expect(beg.intent).toBe("beg");
    expect(beg.speechHint).toBe(BEG_MANGO);
    expect(decide(input({ alignment: "NE", inventory: carrying, distanceFt: 30, winOdds: 0.8 })).intent).toBe("attack");
    expect(decide(input({ alignment: "NE", inventory: carrying, distanceFt: 31, winOdds: 0.8 })).intent).toBe("ignore");
    expect(decide(input({ alignment: "NE", inventory: [{ ...carrying[0]!, count: 0 }], distanceFt: 12, winOdds: 0.8 })).intent).toBe("ignore");
    expect(decide(input({
      alignment: "NE",
      inventory: [{ id: "wood", name: "Wood", count: 2 }],
      distanceFt: 12,
      winOdds: 0.9,
    })).intent).toBe("ignore");
  });

  it("lets LE extort and LG offer protection", () => {
    const extort = decide(input({ alignment: "LE", inventory: carrying, distanceFt: 10, winOdds: 0.6 }));
    expect(extort.intent).toBe("extort");
    expect(extort.speechHint).toBe(EXTORT_LINE);
    expect(decide(input({ alignment: "LG", inventory: carrying, distanceFt: 10, winOdds: 0.8 })).intent).toBe("protect");
    const polite = decide(input({ alignment: "LG", inventory: carrying, distanceFt: 10, winOdds: 0.2 }));
    expect(polite.intent).toBe("beg");
    expect(polite.speechHint).toBe(BEG_MANGO);
  });

  it("keeps good alignments off attack, ambush, and extort", () => {
    const goods: Alignment[] = ["LG", "NG", "CG"];
    for (const alignment of goods) {
      for (const winOdds of [0, CONFIDENT_ODDS, 1]) {
        for (const dinobook of [false, true]) {
          const result = decide(input({
            alignment,
            inventory: carrying,
            distanceFt: 5,
            winOdds,
            dinobook,
          }));
          expect(["attack", "ambush", "extort"]).not.toContain(result.intent);
        }
      }
    }
    expect(ALIGNMENTS).toHaveLength(9);
  });

  it("names a non-mango beg", () => {
    expect(speechHint("beg", "Berry")).toBe("yo I can smell that Berry you got, can you spare one");
  });
});

describe("context", () => {
  it("defaults to 4096 and folds until the budget holds", () => {
    expect(createContext().budget).toBe(DEFAULT_CONTEXT_BUDGET);
    expect(estimateTokens("abcd")).toBe(1);
    expect(clampBudget(10)).toBe(128);
    expect(clampBudget(90000)).toBe(8192);
    let state = createContext(256);
    for (let i = 0; i < 30; i += 1) {
      state = remember(state, { speaker: "You", text: `${"word ".repeat(80)}${i}` }, "world ".repeat(40));
    }
    state = remember(state, { speaker: "You", text: "last turn stays" }, "near");
    expect(tokenTotal(state)).toBeLessThanOrEqual(256);
    expect(state.turns.at(-1)?.text).toContain("last turn stays");
    state = setBudget(state, 128);
    expect(state.budget).toBe(128);
    expect(tokenTotal(state)).toBeLessThanOrEqual(128);
  });
});

describe("steer", () => {
  it("steers with the intent word and otherwise keeps the chat line", () => {
    const attack = pickSteer({ mode: "direct", speaker: "You", userText: "why you running", intent: "attack", cues: grumble.cues });
    expect(attack.utterance).toBe("attack");
    const chat = pickSteer({ mode: "direct", speaker: "You", userText: "why you running", intent: "ignore", cues: grumble.cues });
    expect(chat.keyword).toBe("running");
    expect(chat.utterance).toBe("why you running");
  });
});
