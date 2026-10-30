import { DatabaseSync } from "node:sqlite";
import { normalizeRow, type Row, type Sql, type SqlValue } from "./sql";

/** Node's SQLite, used by tests. The Worker uses Durable Object SQL instead. */
export function openMemorySql(): Sql & { close(): void } {
  const db = new DatabaseSync(":memory:");
  return {
    exec(query: string, ...params: SqlValue[]): Row[] {
      const statement = query.trim().replace(/;+\s*$/, "");
      if (/^(select|insert|update|delete|with)\b/i.test(statement)) {
        const rows = db.prepare(statement).all(...params) as Record<string, unknown>[];
        return rows.map(normalizeRow);
      }
      db.exec(statement);
      return [];
    },
    close() {
      db.close();
    },
  };
}
