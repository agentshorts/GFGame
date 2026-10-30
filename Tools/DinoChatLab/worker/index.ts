import { DurableObject } from "cloudflare:workers";
import type { DecisionLogEntry, DinoMemory, DinoWork, RelationshipMemory, StatEventDraft } from "../src/core/types";
import { HubStore, migrateHub, type DeleteResult, type LogWriteResult, type SavePersonaResult } from "./lib/hub";
import { handleApi } from "./lib/http";
import { migrateSignups, reserveSignup, type ReserveResult } from "./lib/signup";
import { sqlFromDo, type DoSql } from "./lib/sql";

export class SignupLimiter extends DurableObject<Env> {
  private readonly sql;

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    this.sql = sqlFromDo(ctx.storage.sql as unknown as DoSql);
    ctx.blockConcurrencyWhile(async () => {
      migrateSignups(this.sql);
    });
  }

  reserve(day: string): ReserveResult {
    return reserveSignup(this.sql, day);
  }
}

export class PersonaHub extends DurableObject<Env> {
  private readonly store: HubStore;

  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    this.store = new HubStore(sqlFromDo(ctx.storage.sql as unknown as DoSql));
    ctx.blockConcurrencyWhile(async () => {
      migrateHub(this.storeSql());
    });
  }

  private storeSql() {
    return sqlFromDo(this.ctx.storage.sql as unknown as DoSql);
  }

  findUser(username: string): boolean {
    return this.store.findUser(username);
  }

  insertUser(username: string, tokenHash: string, now: string): { created: boolean } {
    return this.store.insertUser(username, tokenHash, now);
  }

  deleteUser(username: string): void {
    this.store.deleteUser(username);
  }

  usernameForToken(tokenHash: string): string | null {
    return this.store.usernameForToken(tokenHash);
  }

  touch(username: string, now: string): void {
    this.store.touch(username, now);
  }

  listPersonas() {
    return this.store.listPersonas();
  }

  savePersona(owner: string, id: string, record: unknown, version: number, now: string): SavePersonaResult {
    return this.store.savePersona(owner, id, record, version, now);
  }

  deletePersona(owner: string, id: string): DeleteResult {
    return this.store.deletePersona(owner, id);
  }

  getMemory(owner: string, dinoId: string): DinoMemory | null {
    return this.store.getMemory(owner, dinoId);
  }

  putMemory(owner: string, memory: DinoMemory): void {
    this.store.putMemory(owner, memory);
  }

  getRelationship(owner: string, dinoId: string, subjectKey: string): RelationshipMemory | null {
    return this.store.getRelationship(owner, dinoId, subjectKey);
  }

  putRelationship(owner: string, rel: RelationshipMemory): void {
    this.store.putRelationship(owner, rel);
  }

  appendLog(owner: string, row: DecisionLogEntry, now: string): LogWriteResult {
    return this.store.appendLog(owner, row, now);
  }

  tailLog(owner: string, dinoId: string, limit: number): DecisionLogEntry[] {
    return this.store.tailLog(owner, dinoId, limit);
  }

  getStats(playerKey: string, familyId: string) {
    return this.store.getStats(playerKey, familyId);
  }

  listStats(playerKey: string) {
    return this.store.listStats(playerKey);
  }

  bumpStats(owner: string, event: StatEventDraft, now: string) {
    return this.store.bumpStats(owner, event, now);
  }

  getWork(dinoId: string): DinoWork | null {
    return this.store.getWork(dinoId);
  }

  creditWages(dinoId: string, amount: number): DinoWork {
    return this.store.creditWages(dinoId, amount);
  }
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    if (url.pathname.startsWith("/api/")) {
      try {
        return await handleApi(request, {
          hub: env.PERSONA_HUB.getByName("global"),
          limiter: env.SIGNUP_LIMITER.getByName("global"),
        });
      } catch (error) {
        console.error(JSON.stringify({
          event: "api_error",
          message: error instanceof Error ? error.message : "error",
        }));
        return Response.json({ error: "The persona service failed." }, { status: 500 });
      }
    }
    if (env.ASSETS) return env.ASSETS.fetch(request);
    return new Response("Not found", { status: 404 });
  },
} satisfies ExportedHandler<Env>;
