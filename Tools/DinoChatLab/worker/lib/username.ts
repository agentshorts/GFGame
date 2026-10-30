import { filterOutput } from "../../src/logic/filter";

export const USERNAME_PATTERN = /^[A-Za-z0-9_-]{3,20}$/;
export const USER_COOKIE = "dcl_user";
export const USERNAME_STORAGE_KEY = "dcl:username";

export function validateUsername(name: string): { ok: true; username: string } | { ok: false; error: string } {
  const username = name.trim();
  if (!USERNAME_PATTERN.test(username)) {
    return { ok: false, error: "Use 3–20 letters, numbers, underscores, or hyphens." };
  }
  if (filterOutput(username).filtered) {
    return { ok: false, error: "That name is not allowed." };
  }
  return { ok: true, username };
}

export function readCookie(header: string | null, name: string): string | null {
  if (!header) return null;
  for (const part of header.split(";")) {
    const eq = part.indexOf("=");
    if (eq < 0) continue;
    const key = part.slice(0, eq).trim();
    if (key !== name) continue;
    const raw = part.slice(eq + 1).trim();
    try {
      return decodeURIComponent(raw);
    } catch {
      return raw;
    }
  }
  return null;
}

export function sessionCookie(token: string, secure: boolean): string {
  const parts = [
    `${USER_COOKIE}=${token}`,
    "HttpOnly",
    "SameSite=Lax",
    "Path=/",
    `Max-Age=${60 * 60 * 24 * 400}`,
  ];
  if (secure) parts.push("Secure");
  return parts.join("; ");
}

export function newToken(): string {
  const bytes = new Uint8Array(32);
  crypto.getRandomValues(bytes);
  return [...bytes].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

export async function hashToken(token: string): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(token));
  return [...new Uint8Array(digest)].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}
