import { num, type Sql } from "./sql";

export const DAILY_SIGNUP_CAP = 100;

export const SIGNUP_LIMIT_MESSAGE =
  "Daily new-name limit reached (100 per UTC day). Try again after 00:00 UTC.";

export const SIGNUPS_SCHEMA = `
CREATE TABLE IF NOT EXISTS signups (
  day TEXT PRIMARY KEY,
  count INTEGER NOT NULL
)`;

export interface ReserveResult {
  ok: boolean;
  count: number;
  message?: string;
}

export function migrateSignups(sql: Sql): void {
  sql.exec(SIGNUPS_SCHEMA);
}

/** UTC calendar day, YYYY-MM-DD. The cap resets when this string changes. */
export function utcDay(now: Date): string {
  return now.toISOString().slice(0, 10);
}

/**
 * Atomically increments the UTC day's counter when it is still under the cap.
 * The WHERE clause on the upsert makes the 101st call a no-op that returns no row.
 */
export function reserveSignup(sql: Sql, day: string, cap = DAILY_SIGNUP_CAP): ReserveResult {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(day)) {
    throw new Error("Signup day must be YYYY-MM-DD.");
  }
  const inserted = sql.exec(
    `INSERT INTO signups (day, count) VALUES (?, 1)
     ON CONFLICT(day) DO UPDATE SET count = count + 1
     WHERE signups.count < ?
     RETURNING count`,
    day,
    cap,
  );
  const count = inserted[0] ? num(inserted[0], "count") : 0;
  if (inserted.length > 0) return { ok: true, count };
  const current = sql.exec(`SELECT count FROM signups WHERE day = ?`, day);
  return {
    ok: false,
    count: num(current[0], "count") || cap,
    message: SIGNUP_LIMIT_MESSAGE,
  };
}
