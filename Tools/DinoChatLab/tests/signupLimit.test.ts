import { describe, expect, it } from "vitest";
import { blankDino } from "../src/manager/profile";
import { HubStore, migrateHub, STALE_VERSION } from "../worker/lib/hub";
import { handleApi } from "../worker/lib/http";
import { openMemorySql } from "../worker/lib/memorySql";
import { DAILY_SIGNUP_CAP, migrateSignups, reserveSignup, SIGNUP_LIMIT_MESSAGE, utcDay } from "../worker/lib/signup";
import { hashToken } from "../worker/lib/username";

function harness(now = new Date("2026-10-30T15:00:00.000Z")) {
  const signupSql = openMemorySql();
  const hubSql = openMemorySql();
  migrateSignups(signupSql);
  migrateHub(hubSql);
  const hub = new HubStore(hubSql);
  return {
    signupSql,
    hubSql,
    hub,
    now,
    api(date = now) {
      return handleApi as typeof handleApi;
    },
    async post(pathname: string, body: unknown, cookie?: string, date = now) {
      const headers = new Headers({ "content-type": "application/json" });
      if (cookie) headers.set("cookie", cookie);
      const request = new Request(`https://lab.example${pathname}`, {
        method: body === undefined ? "GET" : "POST",
        headers,
        body: body === undefined ? undefined : JSON.stringify(body),
      });
      if (body === undefined) {
        return handleApi(new Request(`https://lab.example${pathname}`, { headers }), {
          hub,
          limiter: { reserve: (day: string) => reserveSignup(signupSql, day) },
          now: date,
        });
      }
      return handleApi(request, {
        hub,
        limiter: { reserve: (day: string) => reserveSignup(signupSql, day) },
        now: date,
      });
    },
  };
}

describe("SignupLimiter", () => {
  it("accepts 100 new names on a UTC day, rejects the 101st, and resets the next day", () => {
    const sql = openMemorySql();
    migrateSignups(sql);
    for (let i = 1; i <= DAILY_SIGNUP_CAP; i += 1) {
      const reserved = reserveSignup(sql, "2026-10-30");
      expect(reserved.ok).toBe(true);
      expect(reserved.count).toBe(i);
    }
    const blocked = reserveSignup(sql, "2026-10-30");
    expect(blocked.ok).toBe(false);
    expect(blocked.count).toBe(DAILY_SIGNUP_CAP);
    expect(blocked.message).toBe(SIGNUP_LIMIT_MESSAGE);
    const nextDay = reserveSignup(sql, "2026-10-31");
    expect(nextDay.ok).toBe(true);
    expect(nextDay.count).toBe(1);
    expect(utcDay(new Date("2026-10-30T23:30:00.000Z"))).toBe("2026-10-30");
    sql.close();
  });

  it("returns 429 from the username route and does not spend a slot on a taken name", async () => {
    const lab = harness();
    for (let i = 1; i <= DAILY_SIGNUP_CAP; i += 1) {
      const response = await lab.post("/api/users", { username: `dino${i}` });
      expect(response.status).toBe(201);
    }
    const limited = await lab.post("/api/users", { username: "one-more" });
    expect(limited.status).toBe(429);
    expect(await limited.json()).toEqual({ error: SIGNUP_LIMIT_MESSAGE });
    const taken = await lab.post("/api/users", { username: "dino1" });
    expect(taken.status).toBe(409);
    const stillFull = reserveSignup(lab.signupSql, "2026-10-30");
    expect(stillFull.ok).toBe(false);
    expect(stillFull.count).toBe(DAILY_SIGNUP_CAP);

    const tomorrow = await lab.post("/api/users", { username: "one-more" }, undefined, new Date("2026-10-31T00:05:00.000Z"));
    expect(tomorrow.status).toBe(201);
    const cookie = tomorrow.headers.get("set-cookie") ?? "";
    expect(cookie).toContain("HttpOnly");
    expect(cookie).toContain("Secure");
    const token = cookie.split(";")[0]?.split("=")[1] ?? "";
    expect(token).not.toBe("one-more");
    const stored = lab.hubSql.exec(`SELECT token_hash FROM users WHERE username = ?`, "one-more");
    expect(stored[0]?.token_hash).toBe(await hashToken(token));
    expect(stored[0]?.token_hash).not.toBe(token);

    const me = await handleApi(new Request("https://lab.example/api/me", { headers: { cookie } }), {
      hub: lab.hub,
      limiter: { reserve: (day: string) => reserveSignup(lab.signupSql, day) },
      now: new Date("2026-10-31T00:06:00.000Z"),
    });
    expect(me.status).toBe(200);
    expect(await me.json()).toEqual({ username: "one-more" });
    lab.signupSql.close();
    lab.hubSql.close();
  });
});

describe("PersonaHub", () => {
  it("rejects a stale persona version and a non-owner edit", () => {
    const sql = openMemorySql();
    migrateHub(sql);
    const hub = new HubStore(sql);
    const dino = blankDino(new Set());
    const now = "2026-10-30T15:00:00.000Z";
    const created = hub.savePersona("ada", dino.persona.id, dino, 0, now);
    expect(created.ok).toBe(true);
    expect(created.persona?.version).toBe(1);
    const stale = hub.savePersona("ada", dino.persona.id, dino, 0, now);
    expect(stale.ok).toBe(false);
    expect(stale.status).toBe(409);
    expect(stale.error).toBe(STALE_VERSION);
    const saved = hub.savePersona("ada", dino.persona.id, dino, 1, now);
    expect(saved.persona?.version).toBe(2);
    const stolen = hub.savePersona("bob", dino.persona.id, dino, 2, now);
    expect(stolen.status).toBe(403);
    sql.close();
  });

  it("adds a kill to the family row and the totals, and applies the core rep delta", () => {
    const sql = openMemorySql();
    migrateHub(sql);
    const hub = new HubStore(sql);
    const now = "2026-10-30T15:00:00.000Z";
    const bumped = hub.bumpStats("ada", {
      at: now,
      playerKey: "Pip",
      familyId: "red",
      dinoId: "big-grumble",
      kind: "kill",
      amount: 1,
      note: "lab",
    }, now);
    expect(bumped.ok).toBe(true);
    if (!bumped.ok) return;
    expect(bumped.stats.kills).toBe(1);
    expect(bumped.stats.rep).toBe(-8);
    expect(bumped.event.kind).toBe("kill");
    expect(hub.getStats("Pip", "*")?.kills).toBe(1);
    expect(hub.getStats("Pip", "*")?.rep).toBe(-8);
    sql.close();
  });

  it("credits wages on the work table and leaves persona rows alone", () => {
    const sql = openMemorySql();
    migrateHub(sql);
    const hub = new HubStore(sql);
    expect(hub.creditWages("big-grumble", 10).lifetimeEarnings).toBe(10);
    expect(hub.creditWages("big-grumble", 5).lifetimeEarnings).toBe(15);
    expect(sql.exec(`SELECT COUNT(*) AS n FROM personas`)[0]?.n ?? 0).toBe(0);
    expect(() => hub.creditWages("big-grumble", -1)).toThrow(/lifetime earnings/);
    sql.close();
  });
});
