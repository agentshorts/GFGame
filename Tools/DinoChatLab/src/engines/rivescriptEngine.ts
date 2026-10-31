import RiveScript from "rivescript";
import { restoreTargetToken, riveSource, shieldTargetToken } from "../logic/scripts";
import type { Persona } from "../logic/types";
import type { ChatEngine, EngineReply, EngineRequest } from "./types";

export function createRiveScriptEngine(): ChatEngine {
  const bots = new Map<string, RiveScript>();

  async function ready(persona: Persona): Promise<void> {
    if (bots.has(persona.id)) return;
    const bot = new RiveScript({ utf8: true });
    await bot.stream(shieldTargetToken(riveSource(persona)));
    bot.sortReplies();
    bots.set(persona.id, bot);
  }

  return {
    id: "rivescript",
    label: "RiveScript",
    blurb: "Trigger and reply script.",
    needsModel: false,
    ready,
    async reply(request: EngineRequest): Promise<EngineReply> {
      await ready(request.persona);
      const bot = bots.get(request.persona.id);
      if (!bot) throw new Error(`RiveScript was not ready for ${request.persona.name}.`);
      const raw = await bot.reply(request.persona.id, request.utterance);
      const text = restoreTargetToken(raw).trim();
      const missed = text.length === 0 || text.startsWith("ERR:");
      if (!missed) return { engine: "rivescript", text };
      return { engine: "rivescript", text: request.persona.fallbackReplies[0] ?? "...", fellBack: true };
    },
    reset(personaId?: string) {
      if (personaId) bots.delete(personaId);
      else bots.clear();
    },
  };
}
