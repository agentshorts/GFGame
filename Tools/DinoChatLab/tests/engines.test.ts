import { describe, expect, it } from "vitest";
import grumbleJson from "../personas/01-big-grumble.json";
import tusksawJson from "../personas/02-tusksaw.json";
import { createAimlEngine } from "../src/engines/aimlEngine";
import { createElizaEngine } from "../src/engines/elizaEngine";
import { createMegahalEngine } from "../src/engines/megahalEngine";
import { createRiveScriptEngine } from "../src/engines/rivescriptEngine";
import type { EngineRequest } from "../src/engines/types";
import { parsePersona } from "../src/logic/persona";
import { fillTarget } from "../src/logic/fillTarget";
import { presentReply } from "../src/logic/present";
import type { Persona } from "../src/logic/types";

const grumble = parsePersona(grumbleJson);
const tusksaw = parsePersona(tusksawJson);

function request(persona: Persona, utterance: string): EngineRequest {
  return {
    persona,
    history: [],
    utterance,
    mode: "direct",
    systemPrompt: "test",
    temperature: 0.7,
    maxTokens: 80,
    peerNames: [],
  };
}

describe("classic engines", () => {
  it("answers Big Grumble's chat and event cues from the sheet", async () => {
    const cases = [
      ["why you running", "Why you running, huh? Why you running?"],
      ["player ran away", "Where you going? Huh? Huh?"],
      ["player attacked Big Grumble", "Mmm, crunchy! Okay, I'm full. Bye!"],
      ["new player joined", "Ooh, fresh snacks! I mean... friends!"],
      ["beg", "Don't drop your rocks! Actually, do."],
    ] as const;
    const engines = [createElizaEngine(), createAimlEngine(), createRiveScriptEngine()];
    for (const engine of engines) {
      for (const [utterance, expected] of cases) {
        const reply = await engine.reply(request(grumble, utterance));
        expect(reply.text, `${engine.id} ${utterance}`).toBe(expected);
      }
    }
  });

  it("hands {Dragger} back so the line can name who set the target up", async () => {
    const custom: Persona = {
      ...grumble,
      id: "dragger-rive",
      cues: [{ keywords: ["handoff"], replies: ["Thanks, {Dragger}."] }],
    };
    const reply = await createRiveScriptEngine().reply(request(custom, "handoff"));
    expect(reply.text).toBe("Thanks, {Dragger}.");
    expect(fillTarget(reply.text, "Pip", "Rex")).toBe("Thanks, Rex.");
  });

  it("lets Tusksaw beg in the lab sentence", async () => {
    const reply = await createElizaEngine().reply(request(tusksaw, "beg"));
    expect(reply.text).toBe("yo I can smell those mango blocks you got, can you spare one");
  });

  it("honors a raw ELIZA script", async () => {
    const custom: Persona = {
      ...grumble,
      id: "raw-eliza",
      rawScripts: {
        eliza: ["initial: hi", "final: bye", "quit: bye", "key: xnone", "  decomp: *", "  reasmb: LAB OVERRIDE"].join("\n"),
      },
    };
    const reply = await createElizaEngine().reply(request(custom, "anything at all"));
    expect(reply.text).toBe("LAB OVERRIDE");
  });

  it("keeps MegaHAL inside the reply budget", async () => {
    const engine = createMegahalEngine({ randomRange: () => 0 });
    const reply = await engine.reply(request(grumble, "hello"));
    expect(reply.text.trim().length).toBeGreaterThan(0);
    const shown = presentReply(reply.text, {
      playerName: "",
      maxReplyLength: grumble.maxReplyLength,
      maxTokens: 80,
    });
    expect(shown.text.length).toBeLessThanOrEqual(grumble.maxReplyLength);
  });
});
