import { buildWebLlmMessages } from "../logic/prompt";
import type { ChatEngine, EngineReply, EngineRequest, WebLlmClient } from "./types";

export function createWebLlmEngine(getClient: () => WebLlmClient | null): ChatEngine {
  return {
    id: "webllm",
    label: "WebLLM",
    blurb: "Context layer in front of the classic engines. Needs WebGPU.",
    needsModel: true,
    ready() {
      return Promise.resolve();
    },
    async reply(request: EngineRequest): Promise<EngineReply> {
      const client = getClient();
      if (!client || !client.loadedModelId) {
        throw new Error("Load a WebLLM model first. ELIZA and the other script engines need no download.");
      }
      const messages = buildWebLlmMessages(request.systemPrompt, request.history, request.persona.name);
      const result = await client.complete({
        messages,
        temperature: request.temperature,
        maxTokens: request.maxTokens,
      });
      const text = result.text.trim();
      return {
        engine: "webllm",
        text: text.length > 0 ? text : (request.persona.fallbackReplies[0] ?? "..."),
        stats: {
          modelId: client.loadedModelId,
          tokensPerSec: result.tokensPerSec,
          prefillTokensPerSec: result.prefillTokensPerSec,
          promptTokens: result.promptTokens,
          completionTokens: result.completionTokens,
          vramMb: client.vramMb,
          bufferMb: client.bufferMb,
          runtimeStatsText: result.runtimeStatsText,
          fromCache: client.fromCache,
        },
      };
    },
    reset() {
      /* The browser keeps the downloaded weights. Reset only clears the transcript. */
    },
  };
}
