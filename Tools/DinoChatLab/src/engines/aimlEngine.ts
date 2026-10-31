import { AIML1Bot } from "aiml.js";
import { aimlSource } from "../logic/scripts";
import type { Persona } from "../logic/types";
import type { ChatEngine, EngineReply, EngineRequest } from "./types";

export function createAimlEngine(): ChatEngine {
  const bots = new Map<string, AIML1Bot>();

  async function ready(persona: Persona): Promise<void> {
    if (bots.has(persona.id)) return;
    const bot = new AIML1Bot({ properties: { name: persona.name } });
    await bot.loadFile({ name: `${persona.id}.aiml`, content: aimlSource(persona) });
    bots.set(persona.id, bot);
  }

  return {
    id: "aiml",
    label: "AIML",
    blurb: "A.L.I.C.E.-style categories.",
    needsModel: false,
    ready,
    async reply(request: EngineRequest): Promise<EngineReply> {
      await ready(request.persona);
      const bot = bots.get(request.persona.id);
      if (!bot) throw new Error(`AIML was not ready for ${request.persona.name}.`);
      const result = await bot.talk(request.utterance, request.persona.id);
      const text = result.response.trim();
      if (text.length > 0) return { engine: "aiml", text };
      return { engine: "aiml", text: request.persona.fallbackReplies[0] ?? "...", fellBack: true };
    },
    reset(personaId?: string) {
      if (personaId) bots.delete(personaId);
      else bots.clear();
    },
  };
}
