import { isStatKind, WageCreditError } from "../../src/core/stats";
import type { DecisionLogEntry, DinoMemory, DinoWork, RelationshipMemory, StatEventDraft } from "../../src/core/types";
import type { HubStore } from "./hub";
import { utcDay, type ReserveResult } from "./signup";
import { hashToken, newToken, readCookie, sessionCookie, USER_COOKIE, validateUsername } from "./username";

type Maybe<T> = T | Promise<T>;

export interface HubApi {
  findUser(username: string): Maybe<boolean>;
  insertUser(username: string, tokenHash: string, now: string): Maybe<{ created: boolean }>;
  deleteUser(username: string): Maybe<void>;
  usernameForToken(tokenHash: string): Maybe<string | null>;
  touch(username: string, now: string): Maybe<void>;
  listPersonas(): Maybe<Awaited<ReturnType<HubStore["listPersonas"]>>>;
  savePersona(owner: string, id: string, record: unknown, version: number, now: string): Maybe<Awaited<ReturnType<HubStore["savePersona"]>>>;
  deletePersona(owner: string, id: string): Maybe<Awaited<ReturnType<HubStore["deletePersona"]>>>;
  getMemory(owner: string, dinoId: string): Maybe<DinoMemory | null>;
  putMemory(owner: string, memory: DinoMemory): Maybe<void>;
  getRelationship(owner: string, dinoId: string, subjectKey: string): Maybe<RelationshipMemory | null>;
  putRelationship(owner: string, rel: RelationshipMemory): Maybe<void>;
  appendLog(owner: string, row: DecisionLogEntry, now: string): Maybe<Awaited<ReturnType<HubStore["appendLog"]>>>;
  tailLog(owner: string, dinoId: string, limit: number): Maybe<DecisionLogEntry[]>;
  getStats(playerKey: string, familyId: string): Maybe<Awaited<ReturnType<HubStore["getStats"]>>>;
  listStats(playerKey: string): Maybe<Awaited<ReturnType<HubStore["listStats"]>>>;
  bumpStats(owner: string, event: StatEventDraft, now: string): Maybe<Awaited<ReturnType<HubStore["bumpStats"]>>>;
  getWork(dinoId: string): Maybe<DinoWork | null>;
  creditWages(dinoId: string, amount: number): Maybe<DinoWork>;
}

export interface LimiterApi {
  reserve(day: string): Maybe<ReserveResult>;
}

export interface ApiDeps {
  hub: HubApi;
  limiter: LimiterApi;
  now?: Date;
}

const WRITE_BODY_MAX = 40 * 1024;

function json(body: unknown, status = 200, extra?: HeadersInit): Response {
  const headers = new Headers(extra);
  headers.set("content-type", "application/json; charset=utf-8");
  return new Response(JSON.stringify(body), { status, headers });
}

async function readLimited(request: Request, maxBytes: number): Promise<{ ok: true; text: string } | { ok: false; response: Response }> {
  const declared = Number(request.headers.get("content-length") ?? "");
  if (Number.isFinite(declared) && declared > maxBytes) {
    return { ok: false, response: json({ error: "Body is too large." }, 413) };
  }
  if (!request.body) return { ok: true, text: "" };
  const reader = request.body.getReader();
  const chunks: Uint8Array[] = [];
  let total = 0;
  for (;;) {
    const step = await reader.read();
    if (step.done) break;
    total += step.value.byteLength;
    if (total > maxBytes) {
      await reader.cancel();
      return { ok: false, response: json({ error: "Body is too large." }, 413) };
    }
    chunks.push(step.value);
  }
  const merged = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    merged.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return { ok: true, text: new TextDecoder().decode(merged) };
}

async function readJson(request: Request, maxBytes: number): Promise<{ ok: true; value: unknown } | { ok: false; response: Response }> {
  const body = await readLimited(request, maxBytes);
  if (!body.ok) return body;
  if (!body.text) return { ok: false, response: json({ error: "Body must be JSON." }, 400) };
  try {
    return { ok: true, value: JSON.parse(body.text) as unknown };
  } catch {
    return { ok: false, response: json({ error: "Body must be JSON." }, 400) };
  }
}

async function caller(request: Request, hub: HubApi, now: string): Promise<{ username: string } | Response> {
  const token = readCookie(request.headers.get("cookie"), USER_COOKIE);
  if (!token) return json({ error: "Pick a username first." }, 401);
  const username = await hub.usernameForToken(await hashToken(token));
  if (!username) return json({ error: "That cookie is not a known username. Pick a new name." }, 401);
  await hub.touch(username, now);
  return { username };
}

function asRecord(value: unknown): Record<string, unknown> | null {
  return value && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : null;
}

export async function handleApi(request: Request, deps: ApiDeps): Promise<Response> {
  const url = new URL(request.url);
  const nowDate = deps.now ?? new Date();
  const now = nowDate.toISOString();
  const { hub, limiter } = deps;
  const path = url.pathname;

  if (request.method === "GET" && path === "/api/health") {
    return json({ ok: true, service: "dino-chat-lab" });
  }

  if (path === "/api/users" && request.method === "POST") {
    const body = await readJson(request, 1024);
    if (!body.ok) return body.response;
    const record = asRecord(body.value);
    const check = validateUsername(typeof record?.username === "string" ? record.username : "");
    if (!check.ok) return json({ error: check.error }, 400);
    if (await hub.findUser(check.username)) return json({ error: "That name is taken." }, 409);
    const reserved = await limiter.reserve(utcDay(nowDate));
    if (!reserved.ok) {
      console.log(JSON.stringify({ event: "signup_limited", day: utcDay(nowDate), count: reserved.count }));
      return json({ error: reserved.message }, 429);
    }
    const token = newToken();
    const created = await hub.insertUser(check.username, await hashToken(token), now);
    if (!created.created) return json({ error: "That name is taken." }, 409);
    const secure = url.protocol === "https:";
    console.log(JSON.stringify({ event: "signup", username: check.username, day: utcDay(nowDate) }));
    return json({ username: check.username }, 201, { "set-cookie": sessionCookie(token, secure) });
  }

  if (path === "/api/me" && request.method === "GET") {
    const who = await caller(request, hub, now);
    if (who instanceof Response) return who;
    return json({ username: who.username });
  }

  if (path === "/api/personas" && request.method === "GET") {
    return json({ personas: await hub.listPersonas() });
  }

  const personaPath = path.match(/^\/api\/personas\/([a-z0-9-]+)$/);
  if (personaPath) {
    const id = personaPath[1]!;
    const who = await caller(request, hub, now);
    if (who instanceof Response) return who;
    if (request.method === "PUT") {
      const body = await readJson(request, WRITE_BODY_MAX);
      if (!body.ok) return body.response;
      const record = asRecord(body.value);
      const version = typeof record?.version === "number" ? record.version : NaN;
      if (!Number.isInteger(version) || version < 0) return json({ error: "Version has to be a whole number." }, 400);
      const saved = await hub.savePersona(who.username, id, record?.record, version, now);
      if (!saved.ok) return json({ error: saved.error }, saved.status);
      return json(saved.persona, saved.status);
    }
    if (request.method === "DELETE") {
      const removed = await hub.deletePersona(who.username, id);
      if (!removed.ok) return json({ error: removed.error }, removed.status);
      return json({ ok: true });
    }
  }

  if (path === "/api/memory" && (request.method === "GET" || request.method === "PUT")) {
    const who = await caller(request, hub, now);
    if (who instanceof Response) return who;
    const dinoId = request.method === "GET"
      ? url.searchParams.get("dinoId") ?? ""
      : "";
    if (request.method === "GET") {
      return json({ memory: await hub.getMemory(who.username, dinoId) });
    }
    const body = await readJson(request, 8000);
    if (!body.ok) return body.response;
    const memory = asRecord(body.value);
    if (!memory || typeof memory.dinoId !== "string" || typeof memory.summary !== "string") {
      return json({ error: "Memory JSON is not valid." }, 400);
    }
    await hub.putMemory(who.username, {
      dinoId: memory.dinoId,
      summary: memory.summary,
      updatedAt: typeof memory.updatedAt === "string" ? memory.updatedAt : now,
      turnsSinceSummary: typeof memory.turnsSinceSummary === "number" ? memory.turnsSinceSummary : 0,
    });
    return json({ ok: true });
  }

  if (path === "/api/relationships" && (request.method === "GET" || request.method === "PUT")) {
    const who = await caller(request, hub, now);
    if (who instanceof Response) return who;
    if (request.method === "GET") {
      const dinoId = url.searchParams.get("dinoId") ?? "";
      const subjectKey = url.searchParams.get("subjectKey") ?? "";
      return json({ relationship: await hub.getRelationship(who.username, dinoId, subjectKey) });
    }
    const body = await readJson(request, 8000);
    if (!body.ok) return body.response;
    const rel = asRecord(body.value);
    if (!rel || typeof rel.dinoId !== "string" || typeof rel.subjectKey !== "string" || typeof rel.kind !== "string") {
      return json({ error: "Relationship JSON is not valid." }, 400);
    }
    await hub.putRelationship(who.username, {
      dinoId: rel.dinoId,
      subjectKey: rel.subjectKey,
      kind: rel.kind as RelationshipMemory["kind"],
      affinity: typeof rel.affinity === "number" ? rel.affinity : 0,
      summary: typeof rel.summary === "string" ? rel.summary : "",
      lastSeenAt: typeof rel.lastSeenAt === "string" ? rel.lastSeenAt : now,
      interactions: typeof rel.interactions === "number" ? rel.interactions : 0,
    });
    return json({ ok: true });
  }

  if (path === "/api/decision-log" && request.method === "GET") {
    const who = await caller(request, hub, now);
    if (who instanceof Response) return who;
    const limit = Number(url.searchParams.get("limit") ?? "20");
    const rows = await hub.tailLog(who.username, url.searchParams.get("dinoId") ?? "", limit);
    return json({ rows });
  }

  if (path === "/api/decision-log" && request.method === "POST") {
    const who = await caller(request, hub, now);
    if (who instanceof Response) return who;
    const body = await readJson(request, 12000);
    if (!body.ok) return body.response;
    const row = asRecord(body.value);
    if (!row || typeof row.id !== "string" || typeof row.dinoId !== "string" || typeof row.action !== "string") {
      return json({ error: "Decision log JSON is not valid." }, 400);
    }
    const metadata = asRecord(row.metadata) ?? {};
    const written = await hub.appendLog(who.username, {
      id: row.id,
      at: typeof row.at === "string" ? row.at : now,
      cycleId: typeof row.cycleId === "string" ? row.cycleId : "cycle",
      dinoId: row.dinoId,
      subjectKey: typeof row.subjectKey === "string" ? row.subjectKey : who.username,
      action: row.action,
      detail: typeof row.detail === "string" ? row.detail : "",
      metadata,
      engine: typeof row.engine === "string" && row.engine.length > 0 ? row.engine : "none",
      modelId: typeof row.modelId === "string" ? row.modelId : null,
      promptTokens: typeof row.promptTokens === "number" ? row.promptTokens : null,
      completionTokens: typeof row.completionTokens === "number" ? row.completionTokens : null,
      durationMs: typeof row.durationMs === "number" ? row.durationMs : null,
    }, now);
    if (!written.ok) return json({ error: written.error }, written.status);
    return json({ ok: true }, written.status);
  }

  if (path === "/api/stats" && request.method === "GET") {
    const playerKey = url.searchParams.get("playerKey") ?? "";
    const familyId = url.searchParams.get("familyId");
    if (familyId) return json({ stats: await hub.getStats(playerKey, familyId) });
    return json({ stats: await hub.listStats(playerKey) });
  }

  if (path === "/api/stats/events" && request.method === "POST") {
    const who = await caller(request, hub, now);
    if (who instanceof Response) return who;
    const body = await readJson(request, 4000);
    if (!body.ok) return body.response;
    const event = asRecord(body.value);
    if (!event || typeof event.playerKey !== "string" || typeof event.kind !== "string" || typeof event.dinoId !== "string" || !isStatKind(event.kind)) {
      return json({ error: "Stat event JSON is not valid." }, 400);
    }
    const bumped = await hub.bumpStats(who.username, {
      at: typeof event.at === "string" ? event.at : now,
      playerKey: event.playerKey,
      familyId: typeof event.familyId === "string" ? event.familyId : "*",
      dinoId: event.dinoId,
      kind: event.kind,
      amount: typeof event.amount === "number" ? event.amount : 1,
      note: typeof event.note === "string" ? event.note : "",
    }, now);
    if (!bumped.ok) return json({ error: bumped.error }, bumped.status);
    return json({ event: bumped.event, stats: bumped.stats }, 201);
  }

  if (path === "/api/work" && request.method === "GET") {
    const who = await caller(request, hub, now);
    if (who instanceof Response) return who;
    return json({ work: await hub.getWork(url.searchParams.get("dinoId") ?? "") });
  }

  if (path === "/api/work/wages" && request.method === "POST") {
    const who = await caller(request, hub, now);
    if (who instanceof Response) return who;
    const body = await readJson(request, 1000);
    if (!body.ok) return body.response;
    const record = asRecord(body.value);
    if (!record || typeof record.dinoId !== "string" || typeof record.amount !== "number") {
      return json({ error: "Wage credit JSON is not valid." }, 400);
    }
    try {
      const work = await hub.creditWages(record.dinoId, record.amount);
      return json({ work });
    } catch (error) {
      const message = error instanceof WageCreditError || error instanceof Error ? error.message : "Could not credit wages.";
      return json({ error: message }, 400);
    }
  }

  return json({ error: "No route for that." }, 404);
}
