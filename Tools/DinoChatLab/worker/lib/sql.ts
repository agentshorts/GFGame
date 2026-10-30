export type SqlValue = string | number | null;

export interface Row {
  [column: string]: SqlValue;
}

export interface Sql {
  exec(query: string, ...params: SqlValue[]): Row[];
}

export function normalizeRow(row: Record<string, unknown>): Row {
  const out: Row = {};
  for (const [key, value] of Object.entries(row)) {
    if (typeof value === "bigint") out[key] = Number(value);
    else if (value === null || typeof value === "number" || typeof value === "string") out[key] = value;
    else if (typeof value === "boolean") out[key] = value ? 1 : 0;
    else out[key] = null;
  }
  return out;
}

export interface DoSql {
  exec(query: string, ...bindings: unknown[]): { toArray(): Record<string, unknown>[] };
}

export function sqlFromDo(storage: DoSql): Sql {
  return {
    exec(query, ...params) {
      return storage.exec(query, ...params).toArray().map(normalizeRow);
    },
  };
}

export function num(row: Row | undefined, column: string): number {
  const value = row?.[column];
  return typeof value === "number" ? value : 0;
}

export function text(row: Row | undefined, column: string): string {
  const value = row?.[column];
  return typeof value === "string" ? value : "";
}
