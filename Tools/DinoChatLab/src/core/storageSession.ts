/**
 * Which notebook the lab is using. localStorage stays the default.
 * The SQLite file is kept when the toggle goes off, so turning it back on
 * does not copy the older local snapshot over the file.
 */

import {
  bindStorage,
  createDefaultStorage,
  LocalStorageAdapter,
  setDefaultStorageFactory,
  type DinoStorage,
  type KeyValueStore,
} from "./storage";
import {
  chooseBrowserPersist,
  migrateLocalStorageOnce,
  openSqliteStorage,
  readStorageBackend,
  SQLITE_MIGRATED_KEY,
  STORAGE_BACKEND_KEY,
  SqliteStorage,
  type SqlitePlace,
} from "./sqliteStorage";

let live: DinoStorage | null = null;
let heldSqlite: SqliteStorage | null = null;
let opening: Promise<SqliteOpen> | null = null;
const swapListeners = new Set<() => void>();

function pinSqlite(storage: SqliteStorage): SqliteStorage {
  heldSqlite = storage;
  live = storage;
  const pinned: DinoStorage = storage;
  setDefaultStorageFactory(() => pinned);
  bindStorage(storage);
  return storage;
}

export function onStorageSwap(listener: () => void): () => void {
  swapListeners.add(listener);
  return () => {
    swapListeners.delete(listener);
  };
}

export function notifyStorageSwap(): void {
  for (const listener of [...swapListeners]) listener();
}

/** Opens SQLite when the saved preference says so. Repeated calls share one open. */
export function openPreferred(kv: KeyValueStore): Promise<SqliteOpen | null> {
  if (readStorageBackend(kv) !== "sqlite") return Promise.resolve(null);
  if (live instanceof SqliteStorage) {
    pinSqlite(live);
    const place = live.place === "opfs" ? "opfs" : "indexeddb";
    return Promise.resolve({ storage: live, place, copied: false });
  }
  if (!opening) {
    opening = enableSqlite(kv).finally(() => {
      opening = null;
    });
  }
  return opening;
}

export function liveStorage(): DinoStorage {
  if (!live) live = createDefaultStorage();
  return live;
}

export interface SqliteOpen {
  storage: SqliteStorage;
  place: SqlitePlace;
  copied: boolean;
}

export async function enableSqlite(kv: KeyValueStore): Promise<SqliteOpen> {
  if (live instanceof SqliteStorage) {
    kv.setItem(STORAGE_BACKEND_KEY, "sqlite");
    pinSqlite(live);
    return { storage: live, place: live.place === "opfs" ? "opfs" : "indexeddb", copied: false };
  }
  if (heldSqlite) {
    const copied = migrateLocalStorageOnce(heldSqlite, new LocalStorageAdapter(kv), kv);
    await heldSqlite.flush();
    kv.setItem(STORAGE_BACKEND_KEY, "sqlite");
    pinSqlite(heldSqlite);
    return { storage: heldSqlite, place: heldSqlite.place === "opfs" ? "opfs" : "indexeddb", copied };
  }
  const chosen = await chooseBrowserPersist();
  const storage = await openSqliteStorage({ persist: chosen.persist, place: chosen.place });
  let copied = false;
  try {
    copied = migrateLocalStorageOnce(storage, new LocalStorageAdapter(kv), kv);
    await storage.flush();
  } catch (error) {
    if (copied) kv.removeItem(SQLITE_MIGRATED_KEY);
    throw error;
  }
  kv.setItem(STORAGE_BACKEND_KEY, "sqlite");
  pinSqlite(storage);
  return { storage, place: chosen.place, copied };
}

export function disableSqlite(kv: KeyValueStore): LocalStorageAdapter {
  kv.setItem(STORAGE_BACKEND_KEY, "local");
  setDefaultStorageFactory(null);
  const storage = new LocalStorageAdapter(kv);
  live = storage;
  bindStorage(storage);
  return storage;
}
