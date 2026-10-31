import { describe, expect, it } from "vitest";
import { LocalStorageAdapter, memoryKeyValueStore } from "../src/core/storage";
import { DEFAULT_REP_DELTAS } from "../src/core/types";
import {
  applyEventDelta,
  finishBrief,
  finishWage,
  loadCorePort,
  portFromCore,
  type WageCard,
} from "../src/dinobook";

function adapter(): LocalStorageAdapter {
  return new LocalStorageAdapter(memoryKeyValueStore());
}

function card(partial: Partial<WageCard> & { dinoId: string }): WageCard {
  return finishWage({
    factoryId: "amber-kiln",
    factoryName: "Amber kiln",
    shiftPay: 10,
    lifetimeEarnings: 10,
    hiredAt: "2026-01-01T00:00:00.000Z",
    mineralsFound: 0,
    mateRank: 0,
    employment: "tamed",
    ...partial,
  });
}

function family(store: LocalStorageAdapter, familyId: string) {
  return store.getStats("Pip").find((row) => row.familyId === familyId);
}

describe("core port wage guards", () => {
  it("does not let setWage raise a lifetime the core already stored", () => {
    const store = adapter();
    const port = portFromCore({ storage: store });
    expect(port).not.toBeNull();
    port!.seedLabCrew("Pip");
    expect(store.getWork("scythepaw")?.lifetimeEarnings).toBe(0);

    port!.setWage("Pip", card({
      dinoId: "scythepaw",
      factoryId: "amber-kiln",
      factoryName: "Amber kiln",
      shiftPay: 500,
      lifetimeEarnings: 9_000,
      employment: "tamed",
    }));

    expect(store.getWork("scythepaw")).toMatchObject({
      lifetimeEarnings: 0,
      employerId: "amber-kiln",
      employment: "tamed",
    });
  });

  it("keeps a rented Ripwing and the same employer when Seed runs again", () => {
    const store = adapter();
    const port = portFromCore({ storage: store })!;
    port.seedLabCrew("Pip");
    port.setWage("Pip", card({
      dinoId: "ripwing",
      factoryId: "amber-kiln",
      factoryName: "Amber kiln",
      shiftPay: 10,
      lifetimeEarnings: 10,
      employment: "rented",
    }));
    port.creditShift("Pip", "ripwing", 25);
    expect(store.getWork("ripwing")?.lifetimeEarnings).toBe(35);

    port.seedLabCrew("Pip");

    expect(store.getWork("ripwing")).toMatchObject({
      employment: "rented",
      employerId: "amber-kiln",
      lifetimeEarnings: 35,
    });
  });

  it("fills a jobless credit on the next seed and leaves the earnings alone", () => {
    const store = adapter();
    const port = portFromCore({ storage: store })!;
    port.creditShift("Pip", "tusksaw", 5);
    port.noteMineral("Pip", "tusksaw");
    expect(store.getWork("tusksaw")).toMatchObject({
      lifetimeEarnings: 5,
      mineralsFound: 1,
      employerId: null,
      employment: null,
    });

    port.seedLabCrew("Pip");

    expect(store.getWork("tusksaw")).toMatchObject({
      lifetimeEarnings: 5,
      mineralsFound: 1,
      employerId: "fossil-pit",
      employment: "tamed",
    });
    expect(store.getWork("chatty-bird")).toMatchObject({
      lifetimeEarnings: 0,
      employerId: null,
      employment: null,
    });
  });

  it("keeps a job seeker's credited earnings unemployed", () => {
    const store = adapter();
    const port = portFromCore({ storage: store })!;
    port.creditShift("Pip", "chatty-bird", 7);
    port.seedLabCrew("Pip");
    expect(store.getWork("chatty-bird")).toMatchObject({
      lifetimeEarnings: 7,
      employerId: null,
      employment: null,
    });
  });
});

describe("core port rep deltas", () => {
  it("reads event rep from DEFAULT_REP_DELTAS, then from knob overrides", () => {
    const plain = adapter();
    const plainPort = portFromCore({ storage: plain })!;
    plainPort.apply("Pip", "swamp", "attacked");
    plainPort.apply("Pip", "swamp", "refused-mango");
    plainPort.apply("Pip", "sickle", "rental-downed");
    expect(family(plain, "swamp")).toMatchObject({
      kills: 1,
      mangoRefused: 1,
      rep: DEFAULT_REP_DELTAS.kill + DEFAULT_REP_DELTAS.mango_refused,
    });
    expect(family(plain, "sickle")).toMatchObject({
      rentalsDowned: 1,
      rep: DEFAULT_REP_DELTAS.rental_downed,
    });

    const tuned = adapter();
    const knobs = tuned.getKnobs();
    tuned.putKnobs({
      ...knobs,
      repDeltas: { ...knobs.repDeltas, kill: -15, mango_refused: -11, rental_downed: -1 },
    });
    const tunedPort = portFromCore({ storage: tuned })!;
    tunedPort.apply("Pip", "swamp", "attacked");
    tunedPort.apply("Pip", "roost", "refused-mango");
    tunedPort.apply("Pip", "sickle", "rental-downed");
    expect(family(tuned, "swamp")?.rep).toBe(-15);
    expect(family(tuned, "roost")?.rep).toBe(-11);
    expect(family(tuned, "sickle")?.rep).toBe(-1);

    const seen: unknown[] = [];
    const fallback = portFromCore({
      storage: {
        getStats: () => [],
        bumpStats: (_event: unknown, deltas?: unknown) => {
          seen.push(deltas);
        },
        getKnobs: () => ({ repDeltas: { kill: Number.NaN, mango_refused: -11 } }),
      },
    })!;
    fallback.apply("Pip", "swamp", "attacked");
    fallback.apply("Pip", "swamp", "refused-mango");
    expect(seen[0]).toEqual({ kill: DEFAULT_REP_DELTAS.kill });
    expect(seen[1]).toEqual({ mango_refused: -11 });
  });

  it("keeps the local event table on the same rep numbers", () => {
    const blank = finishBrief({ familyId: "swamp" });
    expect(applyEventDelta(blank, "attacked").rep).toBe(DEFAULT_REP_DELTAS.kill);
    expect(applyEventDelta(blank, "refused-mango").rep).toBe(DEFAULT_REP_DELTAS.mango_refused);
    expect(applyEventDelta(blank, "rental-downed").rep).toBe(DEFAULT_REP_DELTAS.rental_downed);
  });

  it("seeds family rep from the lab brief, not from knob overrides", () => {
    const store = adapter();
    const knobs = store.getKnobs();
    store.putKnobs({
      ...knobs,
      repDeltas: {
        ...knobs.repDeltas,
        kill: -1,
        mango_refused: -9,
        mango_given: 1,
        hit_taken: 40,
      },
    });
    const port = portFromCore({ storage: store })!;
    port.seedLabCrew("Pip");
    expect(family(store, "swamp")).toMatchObject({ rep: -50, kills: 1, mangoRefused: 1 });
    expect(family(store, "sickle")).toMatchObject({ rep: 4, hitsTaken: 1 });
    expect(family(store, "roost")).toMatchObject({ rep: 36, mangoGiven: 2 });

    port.seedLabCrew("Pip");
    expect(family(store, "swamp")?.kills).toBe(1);
    expect(family(store, "sickle")?.hitsTaken).toBe(1);
  });
});

describe("core port bind", () => {
  it("binds src/core when the modules are in the tree", async () => {
    const port = await loadCorePort();
    expect(port.source).toBe("src/core");
  });
});
