export interface PrebuiltModel {
  model_id: string;
  model?: string;
  vram_required_MB?: number;
  buffer_size_required_bytes?: number;
  low_resource_required?: boolean;
}

export interface SmallModelChoice {
  label: string;
  modelId: string;
  vramMb?: number;
  bufferMb?: number;
  lowResource: boolean;
}

export interface SmallModelList {
  choices: SmallModelChoice[];
  missing: string[];
}

export const SMALL_MODEL_FAMILIES: { label: string; test: RegExp }[] = [
  { label: "Qwen2.5 0.5B Instruct", test: /^Qwen2\.5-0\.5B-Instruct\b/i },
  { label: "Qwen2.5 1.5B Instruct", test: /^Qwen2\.5-1\.5B-Instruct\b/i },
  { label: "Llama 3.2 1B Instruct", test: /^Llama-3\.2-1B-Instruct\b/i },
  { label: "Llama 3.2 3B Instruct", test: /^Llama-3\.2-3B-Instruct\b/i },
  { label: "SmolLM2 135M Instruct", test: /^SmolLM2-135M-Instruct\b/i },
  { label: "SmolLM2 360M Instruct", test: /^SmolLM2-360M-Instruct\b/i },
  { label: "SmolLM2 1.7B Instruct", test: /^SmolLM2-1\.7B-Instruct\b/i },
  { label: "Phi-3.5-mini Instruct", test: /^Phi-3\.5-mini-instruct\b/i },
];

const QUANT_RANK = ["q4f16_1-MLC", "q0f16-MLC", "q4f32_1-MLC", "q0f32-MLC"];

function quantScore(modelId: string): number {
  const index = QUANT_RANK.findIndex((suffix) => modelId.endsWith(suffix));
  return index === -1 ? QUANT_RANK.length : index;
}

function pickQuant(models: PrebuiltModel[]): PrebuiltModel | undefined {
  return [...models].sort((a, b) => {
    const score = quantScore(a.model_id) - quantScore(b.model_id);
    if (score !== 0) return score;
    return (a.vram_required_MB ?? Number.MAX_SAFE_INTEGER) - (b.vram_required_MB ?? Number.MAX_SAFE_INTEGER);
  })[0];
}

export function listSmallModels(records: PrebuiltModel[]): SmallModelList {
  const choices: SmallModelChoice[] = [];
  const missing: string[] = [];
  for (const family of SMALL_MODEL_FAMILIES) {
    const matches = records.filter((record) => family.test.test(record.model_id));
    const picked = pickQuant(matches);
    if (!picked) {
      missing.push(family.label);
      continue;
    }
    choices.push({
      label: family.label,
      modelId: picked.model_id,
      vramMb: picked.vram_required_MB,
      bufferMb: picked.buffer_size_required_bytes
        ? picked.buffer_size_required_bytes / (1024 * 1024)
        : undefined,
      lowResource: Boolean(picked.low_resource_required),
    });
  }
  return { choices, missing };
}

export function defaultModelId(choices: SmallModelChoice[]): string | null {
  return choices.find((choice) => choice.modelId.startsWith("Qwen2.5-0.5B"))?.modelId
    ?? choices[0]?.modelId
    ?? null;
}
