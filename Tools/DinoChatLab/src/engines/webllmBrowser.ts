import { prebuiltAppConfig, CreateWebWorkerMLCEngine } from "@mlc-ai/web-llm";
import { defaultModelId, listSmallModels, type PrebuiltModel, type SmallModelChoice, type SmallModelList } from "../logic/models";
import type { PromptMessage } from "../logic/prompt";
import type { WebLlmClient, WebLlmCompletion } from "./types";

export interface LoadProgress {
  progress: number;
  text: string;
}

interface ProgressReport {
  progress: number;
  text: string;
}

interface UsageExtra {
  decode_tokens_per_s?: number;
  prefill_tokens_per_s?: number;
}

interface ChatResult {
  choices?: { message?: { content?: unknown } }[];
  usage?: {
    prompt_tokens?: number;
    completion_tokens?: number;
    extra?: UsageExtra;
  };
}

interface WorkerEngine {
  reload: (modelId: string) => Promise<void>;
  resetChat?: () => Promise<void>;
  runtimeStatsText: () => Promise<string>;
  chat: {
    completions: {
      create: (args: Record<string, unknown>) => Promise<ChatResult>;
    };
  };
}

export interface BrowserWebLlm extends WebLlmClient {
  catalog: SmallModelList;
  defaultModelId: string | null;
  load: (modelId: string, onProgress: (progress: LoadProgress) => void) => Promise<void>;
}

function asText(content: unknown): string {
  if (typeof content === "string") return content;
  if (!Array.isArray(content)) return "";
  return content.map((part) => {
    if (typeof part === "string") return part;
    if (part && typeof part === "object" && "text" in part) return String((part as { text: unknown }).text);
    return "";
  }).join("");
}

export function readPrebuiltModels(): PrebuiltModel[] {
  return prebuiltAppConfig.model_list.map((record) => ({
    model_id: record.model_id,
    model: record.model,
    vram_required_MB: record.vram_required_MB,
    buffer_size_required_bytes: record.buffer_size_required_bytes,
    low_resource_required: record.low_resource_required,
  }));
}

export function modelChoice(catalog: SmallModelList, modelId: string | null): SmallModelChoice | null {
  return catalog.choices.find((choice) => choice.modelId === modelId) ?? null;
}

export async function detectWebGpu(): Promise<{ ok: boolean; detail: string }> {
  if (typeof navigator === "undefined" || !("gpu" in navigator)) {
    return {
      ok: false,
      detail: "This browser has no WebGPU (navigator.gpu). Use ELIZA, AIML, RiveScript, or MegaHAL.",
    };
  }
  try {
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) {
      return {
        ok: false,
        detail: "WebGPU is present, but no adapter was returned. The script engines still run.",
      };
    }
    return { ok: true, detail: "WebGPU adapter ready. The first model download is cached by the browser." };
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return { ok: false, detail: `WebGPU request failed: ${message}` };
  }
}

export function createBrowserWebLlm(): BrowserWebLlm {
  const catalog = listSmallModels(readPrebuiltModels());
  let engine: WorkerEngine | null = null;
  let worker: Worker | null = null;
  let onProgress: (progress: LoadProgress) => void = () => {};
  const client: BrowserWebLlm = {
    catalog,
    defaultModelId: defaultModelId(catalog.choices),
    loadedModelId: null,
    vramMb: undefined,
    bufferMb: undefined,
    fromCache: false,
    async load(modelId, progress) {
      onProgress = progress;
      const choice = modelChoice(catalog, modelId);
      client.vramMb = choice?.vramMb;
      client.bufferMb = choice?.bufferMb;
      const initProgressCallback = (report: ProgressReport) => {
        if (/cache/i.test(report.text)) client.fromCache = true;
        onProgress({ progress: report.progress, text: report.text });
      };
      if (!worker) {
        worker = new Worker(new URL("../workers/webllm.worker.ts", import.meta.url), { type: "module" });
      }
      if (!engine) {
        client.fromCache = false;
        engine = await CreateWebWorkerMLCEngine(worker, modelId, { initProgressCallback }) as unknown as WorkerEngine;
      } else if (client.loadedModelId !== modelId) {
        client.fromCache = false;
        await engine.reload(modelId);
      } else {
        onProgress({ progress: 1, text: "Already loaded in this tab. Weights stay in the browser cache." });
        client.fromCache = true;
      }
      client.loadedModelId = modelId;
    },
    async complete(args): Promise<WebLlmCompletion> {
      if (!engine || !client.loadedModelId) {
        throw new Error("Load a WebLLM model first.");
      }
      if (engine.resetChat) await engine.resetChat();
      const result = await engine.chat.completions.create({
        messages: args.messages satisfies PromptMessage[],
        temperature: args.temperature,
        max_tokens: args.maxTokens,
        stream: false,
      });
      let runtimeStatsText: string | undefined;
      try {
        runtimeStatsText = await engine.runtimeStatsText();
      } catch {
        runtimeStatsText = undefined;
      }
      return {
        text: asText(result.choices?.[0]?.message?.content),
        promptTokens: result.usage?.prompt_tokens,
        completionTokens: result.usage?.completion_tokens,
        tokensPerSec: result.usage?.extra?.decode_tokens_per_s,
        prefillTokensPerSec: result.usage?.extra?.prefill_tokens_per_s,
        runtimeStatsText,
      };
    },
  };
  return client;
}
