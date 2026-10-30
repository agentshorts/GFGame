import { readCookie, USER_COOKIE, USERNAME_STORAGE_KEY, validateUsername } from "../../worker/lib/username";
import type { KeyValueStore } from "../core";

export function browserKv(): KeyValueStore {
  return localStorage;
}

export function readLocalUsername(kv: KeyValueStore, cookieHeader: string): string | null {
  const stored = kv.getItem(USERNAME_STORAGE_KEY)?.trim() ?? "";
  const storedCheck = validateUsername(stored);
  if (storedCheck.ok) return storedCheck.username;
  const fromCookie = readCookie(cookieHeader, USER_COOKIE) ?? "";
  const cookieCheck = validateUsername(fromCookie);
  return cookieCheck.ok ? cookieCheck.username : null;
}

export function writeLocalUsername(kv: KeyValueStore, username: string): void {
  kv.setItem(USERNAME_STORAGE_KEY, username);
  if (typeof document === "undefined") return;
  const maxAge = 60 * 60 * 24 * 400;
  document.cookie = `${USER_COOKIE}=${encodeURIComponent(username)}; Path=/; Max-Age=${maxAge}; SameSite=Lax`;
}

export async function claimUsername(
  name: string,
  apiBase: string | null,
  kv: KeyValueStore,
  fetchImpl: typeof fetch = fetch,
): Promise<{ ok: true; username: string } | { ok: false; error: string }> {
  const check = validateUsername(name);
  if (!check.ok) return check;
  if (!apiBase) {
    writeLocalUsername(kv, check.username);
    return { ok: true, username: check.username };
  }
  const response = await fetchImpl(`${apiBase.replace(/\/$/, "")}/users`, {
    method: "POST",
    credentials: "include",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ username: check.username }),
  });
  const body = await response.json().catch(() => ({})) as { error?: string; username?: string };
  if (!response.ok) return { ok: false, error: body.error || "Could not save that name." };
  const username = body.username || check.username;
  kv.setItem(USERNAME_STORAGE_KEY, username);
  return { ok: true, username };
}

export async function remoteMe(apiBase: string, fetchImpl: typeof fetch = fetch): Promise<string | null> {
  const response = await fetchImpl(`${apiBase.replace(/\/$/, "")}/me`, { credentials: "include" });
  if (!response.ok) return null;
  const body = await response.json().catch(() => ({})) as { username?: string };
  return typeof body.username === "string" ? body.username : null;
}
