/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_DINO_API?: string;
}

declare module "*.vue" {
  import type { DefineComponent } from "vue";
  const component: DefineComponent<object, object, unknown>;
  export default component;
}

declare module "sql.js" {
  export type SqlValue = string | number | Uint8Array | null;

  export interface Statement {
    bind(values?: Array<string | number | null | Uint8Array>): boolean;
    step(): boolean;
    getAsObject(): Record<string, SqlValue | undefined>;
    free(): boolean;
  }

  export interface Database {
    run(sql: string, params?: Array<string | number | null | Uint8Array>): Database;
    exec(sql: string): Array<{ columns: string[]; values: SqlValue[][] }>;
    prepare(sql: string): Statement;
    export(): Uint8Array;
    close(): void;
  }

  export interface SqlJsStatic {
    Database: new (data?: ArrayLike<number> | null) => Database;
  }

  export interface InitSqlJsOptions {
    wasmBinary?: ArrayLike<number>;
    locateFile?: (file: string, prefix: string) => string;
  }

  export default function initSqlJs(options?: InitSqlJsOptions): Promise<SqlJsStatic>;
}

declare module "sql.js/dist/sql-wasm.js" {
  import initSqlJs from "sql.js";
  export default initSqlJs;
}

declare module "sql.js/dist/sql-wasm-browser.js" {
  import initSqlJs from "sql.js";
  export default initSqlJs;
}

declare module "rivescript" {
  export default class RiveScript {
    constructor(options?: { utf8?: boolean });
    stream(source: string): Promise<void>;
    sortReplies(): void;
    reply(user: string, message: string): Promise<string>;
  }
}
