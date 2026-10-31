/**
 * Tool registry for the WebLLM helper. Decision #19.
 *
 * The player asks in plain words. The model answers with one tool call.
 * This module checks that call and names the scripted behavior to load.
 * It does not download a model and it does not run the behavior.
 * Water dinos float. Flyers carry. Everyone else keeps their feet on the ground.
 */

export const HELPER_TOOL_IDS = ["catch_player", "float_water_area", "fly_player_over"] as const;

export type HelperToolId = (typeof HELPER_TOOL_IDS)[number];

export type Locomotion = "land" | "water" | "flyer";

export type HelperBehaviorId = "catch-player" | "float-water-area" | "fly-player-over";

const SCAN_LIMIT = 20_000;
const ARG_LIMIT = 64;

interface ToolField {
  type: "string";
  description: string;
}

interface HelperToolDef {
  id: HelperToolId;
  offer: string;
  behaviorId: HelperBehaviorId;
  locomotion: "water" | "flyer";
  verb: string;
  summary: string;
  fields: Record<string, ToolField>;
  required: readonly string[];
}

const REGISTRY: readonly HelperToolDef[] = [
  {
    id: "catch_player",
    offer: "Want me to catch you?",
    behaviorId: "catch-player",
    locomotion: "flyer",
    verb: "catch you",
    summary: "Catch the player. A flyer can carry them.",
    fields: {
      where: { type: "string", description: "Where to catch them. Optional." },
    },
    required: [],
  },
  {
    id: "float_water_area",
    offer: "Want me to float the water area?",
    behaviorId: "float-water-area",
    locomotion: "water",
    verb: "float the water area",
    summary: "Float a water area so the player can cross. Water dinos do this.",
    fields: {
      area: { type: "string", description: "The water area to float." },
    },
    required: ["area"],
  },
  {
    id: "fly_player_over",
    offer: "Want me to just fly you over?",
    behaviorId: "fly-player-over",
    locomotion: "flyer",
    verb: "just fly you over",
    summary: "Carry the player over a gap. A flyer does this.",
    fields: {
      level: { type: "string", description: "Platform level to cross, such as X." },
    },
    required: ["level"],
  },
];

export interface HelperTool {
  id: HelperToolId;
  offer: string;
  behaviorId: HelperBehaviorId;
  locomotion: "water" | "flyer";
  verb: string;
  summary: string;
}

export interface HelperBearer {
  id: string;
  locomotion?: Locomotion | readonly Locomotion[];
}

/**
 * Lab roster only. Ripwing is the flyer on the design sheet. Lanternmaw swims.
 * Chatty Bird is a stand-in with no species, so it does not carry anyone.
 * An id that is not listed here stays on land until a caller says otherwise.
 */
export const LAB_LOCOMOTION: Readonly<Record<string, Locomotion>> = {
  lanternmaw: "water",
  ripwing: "flyer",
  tusksaw: "land",
  scythepaw: "land",
  "big-grumble": "land",
  "chatty-bird": "land",
};

export interface HelperFunctionTool {
  type: "function";
  function: {
    name: HelperToolId;
    description: string;
    parameters: {
      type: "object";
      properties: Record<string, ToolField>;
      required: string[];
      additionalProperties: false;
    };
  };
}

export interface LoadedBehavior {
  tool: HelperToolId;
  behaviorId: HelperBehaviorId;
  offer: string;
  args: Readonly<Record<string, string>>;
}

export type HelperFailure = "no-call" | "unknown-tool" | "not-offered" | "bad-args";

export type HelperResolution =
  | { ok: true; kind: "loaded"; behavior: LoadedBehavior }
  | { ok: true; kind: "decline" }
  | { ok: false; kind: HelperFailure; detail: string };

export interface ParsedHelperCall {
  name: string | null;
  args: Record<string, unknown> | null;
}

export type ReadHelperCall =
  | { found: false }
  | ({ found: true } & ParsedHelperCall);

export function isHelperToolId(value: string): value is HelperToolId {
  return (HELPER_TOOL_IDS as readonly string[]).includes(value);
}

export function readLocomotion(value: Locomotion | readonly Locomotion[] | undefined): Locomotion[] {
  const list = value === undefined ? [] : Array.isArray(value) ? [...value] : [value];
  const known = list.filter((item): item is Locomotion => item === "land" || item === "water" || item === "flyer");
  const unique = [...new Set(known)];
  return unique.length > 0 ? unique : ["land"];
}

export function bearerFor(dinoId: string, locomotion?: Locomotion | readonly Locomotion[]): HelperBearer {
  return {
    id: dinoId,
    locomotion: locomotion ?? LAB_LOCOMOTION[dinoId] ?? "land",
  };
}

function definition(id: HelperToolId): HelperToolDef {
  const found = REGISTRY.find((tool) => tool.id === id);
  if (!found) throw new Error(`Missing helper tool ${id}.`);
  return found;
}

export function helperCatalog(): readonly HelperTool[] {
  return REGISTRY.map((tool) => ({
    id: tool.id,
    offer: tool.offer,
    behaviorId: tool.behaviorId,
    locomotion: tool.locomotion,
    verb: tool.verb,
    summary: tool.summary,
  }));
}

export function helperToolsFor(bearer: HelperBearer): readonly HelperTool[] {
  const traits = new Set(readLocomotion(bearer.locomotion));
  return helperCatalog().filter((tool) => traits.has(tool.locomotion));
}

export function helperToolSchemas(bearer: HelperBearer): HelperFunctionTool[] {
  return helperToolsFor(bearer).map((tool) => {
    const def = definition(tool.id);
    return {
      type: "function",
      function: {
        name: tool.id,
        description: tool.summary,
        parameters: {
          type: "object",
          properties: { ...def.fields },
          required: [...def.required],
          additionalProperties: false,
        },
      },
    };
  });
}

function argHint(id: HelperToolId): string {
  if (id === "catch_player") return "catch_player may include where";
  if (id === "float_water_area") return "float_water_area needs area";
  return "fly_player_over needs level";
}

export function helperSystemLine(bearer: HelperBearer): string {
  const tools = helperToolsFor(bearer);
  if (tools.length === 0) {
    return "This dino cannot offer a helper tool. Reply {\"name\":null}.";
  }
  const names = tools.map((tool) => tool.id).join(", ");
  const hints = tools.map((tool) => argHint(tool.id)).join(". ");
  return [
    "The player asked for help in plain words.",
    "Reply with one JSON object only: {\"name\":\"tool_name\",\"arguments\":{}}.",
    `name must be one of: ${names}. Use null when none of them fit.`,
    `${hints}.`,
  ].join(" ");
}

function joinVerbs(verbs: readonly string[]): string {
  if (verbs.length <= 1) return verbs[0] ?? "";
  if (verbs.length === 2) return `${verbs[0]} or ${verbs[1]}`;
  return `${verbs.slice(0, -1).join(", ")}, or ${verbs[verbs.length - 1]}`;
}

function cleanPlace(value: string | undefined): string | null {
  if (typeof value !== "string") return null;
  const cleaned = value.replace(/[\u0000-\u001f]/g, "").trim().slice(0, ARG_LIMIT);
  return cleaned.length > 0 ? cleaned : null;
}

/** The dino offers help without being asked. Null when it has no tool. */
export function unpromptedOffer(bearer: HelperBearer, level?: string): string | null {
  const tools = helperToolsFor(bearer);
  if (tools.length === 0) return null;
  const place = cleanPlace(level) ?? "this level";
  return `I can see you're on ${place}, do you want me to ${joinVerbs(tools.map((tool) => tool.verb))}?`;
}

function asRecord(value: unknown): Record<string, unknown> | null {
  if (!value || typeof value !== "object" || Array.isArray(value)) return null;
  return value as Record<string, unknown>;
}

function matchBrace(text: string, open: number): number {
  let depth = 0;
  let inString = false;
  let escape = false;
  for (let i = open; i < text.length; i += 1) {
    const char = text[i];
    if (inString) {
      if (escape) {
        escape = false;
        continue;
      }
      if (char === "\\") {
        escape = true;
        continue;
      }
      if (char === "\"") inString = false;
      continue;
    }
    if (char === "\"") {
      inString = true;
      continue;
    }
    if (char === "{") depth += 1;
    else if (char === "}") {
      depth -= 1;
      if (depth === 0) return i;
    }
  }
  return -1;
}

function extractObjects(text: string): unknown[] {
  const window = text.length > SCAN_LIMIT ? text.slice(0, SCAN_LIMIT) : text;
  const found: unknown[] = [];
  for (let i = 0; i < window.length; i += 1) {
    if (window[i] !== "{") continue;
    const end = matchBrace(window, i);
    if (end < 0) continue;
    try {
      found.push(JSON.parse(window.slice(i, end + 1)));
    } catch {
      /* A brace pair that is not JSON is just the player talking. */
    }
  }
  return found;
}

function readArguments(raw: unknown): Record<string, unknown> | null {
  if (raw === undefined || raw === null) return {};
  if (typeof raw === "string") {
    const trimmed = raw.trim();
    if (!trimmed) return {};
    try {
      return asRecord(JSON.parse(trimmed));
    } catch {
      return null;
    }
  }
  return asRecord(raw);
}

function callsFrom(value: unknown): ParsedHelperCall[] {
  const record = asRecord(value);
  if (!record) return [];
  if (Array.isArray(record.tool_calls)) {
    const calls: ParsedHelperCall[] = [];
    for (const entry of record.tool_calls) {
      const call = asRecord(entry);
      if (!call) continue;
      const fn = asRecord(call.function) ?? call;
      if (!("name" in fn)) continue;
      const name = fn.name;
      if (name !== null && typeof name !== "string") continue;
      calls.push({ name: name === null ? null : name, args: readArguments(fn.arguments) });
    }
    return calls;
  }
  if ("name" in record) {
    const name = record.name;
    if (name === null || typeof name === "string") {
      return [{ name: name === null ? null : name, args: readArguments(record.arguments) }];
    }
  }
  return [];
}

/** First JSON tool call in the model text. A decline counts as found. */
export function readHelperCall(text: string): ReadHelperCall {
  if (typeof text !== "string" || text.trim().length === 0) return { found: false };
  for (const value of extractObjects(text)) {
    const calls = callsFrom(value);
    if (calls.length > 0) return { found: true, ...calls[0]! };
  }
  return { found: false };
}

function readArg(
  args: Record<string, unknown>,
  key: string,
  required: boolean,
): { ok: true; value?: string } | { ok: false; detail: string } {
  if (!(key in args) || args[key] === undefined || args[key] === null) {
    return required ? { ok: false, detail: `${key} is required` } : { ok: true };
  }
  const raw = args[key];
  const text = typeof raw === "number" && Number.isFinite(raw) ? String(raw) : raw;
  if (typeof text !== "string") return { ok: false, detail: `${key} must be a string` };
  const trimmed = text.trim();
  if (!trimmed) return required ? { ok: false, detail: `${key} is required` } : { ok: true };
  if (trimmed.length > ARG_LIMIT) return { ok: false, detail: `${key} is too long` };
  if (/[\u0000-\u001f]/.test(trimmed)) return { ok: false, detail: `${key} has control characters` };
  return { ok: true, value: trimmed };
}

/**
 * Parse a model reply and, when the dino may offer that tool, name the script.
 * The first call wins. A later call in the same reply is ignored.
 */
export function resolveHelperCall(bearer: HelperBearer, text: string): HelperResolution {
  const read = readHelperCall(text);
  if (!read.found) return { ok: false, kind: "no-call", detail: "no tool call" };
  if (read.name === null) return { ok: true, kind: "decline" };
  const name = read.name.trim();
  if (!isHelperToolId(name)) return { ok: false, kind: "unknown-tool", detail: name || "empty name" };
  const offered = helperToolsFor(bearer).some((tool) => tool.id === name);
  if (!offered) return { ok: false, kind: "not-offered", detail: `${bearer.id} cannot call ${name}` };
  if (!read.args) return { ok: false, kind: "bad-args", detail: "arguments are not an object" };
  const def = definition(name);
  const args: Record<string, string> = {};
  for (const key of Object.keys(def.fields)) {
    const parsed = readArg(read.args, key, def.required.includes(key));
    if (!parsed.ok) return { ok: false, kind: "bad-args", detail: parsed.detail };
    if (parsed.value !== undefined) args[key] = parsed.value;
  }
  return {
    ok: true,
    kind: "loaded",
    behavior: {
      tool: name,
      behaviorId: def.behaviorId,
      offer: def.offer,
      args,
    },
  };
}
