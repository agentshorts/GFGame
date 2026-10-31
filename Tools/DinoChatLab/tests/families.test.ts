import { describe, expect, it } from "vitest";
import { estimateTokens } from "../src/logic/context";
import {
  HATCHLING_NAMES,
  askForSkin,
  emptySkinPester,
  familyRules,
  familySize,
  finishBook,
  grumbleTameAllowed,
  hitForSkin,
  lineageOf,
  matedPair,
  nextSpawnMinute,
  normalizeSeed,
  noteDeath,
  pickHeir,
  respondToKill,
  stepSeed,
  successionEvent,
  tickSpawns,
} from "../src/dinobook/families";
import { pickHeir as barrelHeir, successionEvent as barrelEvent } from "../src/dinobook";
import { BIG_GRUMBLE_ID } from "../src/dinobook/tree";
import {
  FAMILY_CAP,
  GANK_FAMILY_SIZE,
  POST_TOKEN_CAP,
  REVENGE_HITS,
  SKIN_ASKS,
  SPAWN_EVERY_MINUTES,
  type FamilyBook,
  type KinSex,
  type LineageMember,
} from "../src/dinobook/types";

function dino(partial: Partial<LineageMember> & { id: string }): LineageMember {
  return {
    id: partial.id,
    name: partial.name ?? partial.id,
    familyId: partial.familyId ?? "swamp",
    sex: partial.sex ?? "female",
    age: partial.age ?? 1,
    alive: partial.alive ?? true,
    mateId: partial.mateId ?? null,
    parentIds: partial.parentIds ? [...partial.parentIds] : [],
    bornAtMinute: partial.bornAtMinute ?? 0,
  };
}

function book(members: LineageMember[], extras: Partial<FamilyBook> = {}): FamilyBook {
  const families = extras.families ?? [{ familyId: "swamp", name: "Swamp", elderId: "elder" }];
  return {
    families,
    dinos: members,
    lastSpawnMinute: extras.lastSpawnMinute ?? {},
  };
}

function pair(older = 40, younger = 30): LineageMember[] {
  return [
    dino({ id: "elder", name: "Moss", sex: "female", age: older, mateId: "mate" }),
    dino({ id: "mate", name: "Nib", sex: "male", age: younger, mateId: "elder" }),
  ];
}

function hatchFrom(seed: number): { sex: KinSex; name: string; seed: number } {
  const sexRoll = stepSeed(seed);
  const nameRoll = stepSeed(sexRoll.seed);
  const index = Math.min(HATCHLING_NAMES.length - 1, Math.floor(nameRoll.unit * HATCHLING_NAMES.length));
  return {
    sex: sexRoll.unit < 0.5 ? "female" : "male",
    name: HATCHLING_NAMES[index] ?? HATCHLING_NAMES[0],
    seed: nameRoll.seed,
  };
}

function coinKind(seed: number): "barter" | "bounty" {
  return stepSeed(seed).unit < 0.5 ? "barter" : "bounty";
}

describe("seed", () => {
  it("repeats a mulberry32 step and keeps the unit in range", () => {
    expect(normalizeSeed(Number.NaN)).toBe(1);
    expect(stepSeed(1)).toEqual({ unit: 0.6270739405881613, seed: 1831565814 });
    expect(stepSeed(1).seed).not.toBe(stepSeed(2).seed);
  });
});

describe("lineage", () => {
  it("keeps parents, drops a broken bond, and walks a diamond", () => {
    const raw = book([
      dino({ id: "gran", name: "Gran", sex: "female", age: 20 }),
      dino({ id: "elder", name: "Moss", sex: "female", age: 10, mateId: "mate", parentIds: ["gran"] }),
      dino({ id: "mate", name: "Nib", sex: "male", age: 9, mateId: "elder", parentIds: ["gran"] }),
      dino({ id: "cub", name: "Dot", sex: "female", age: 1, parentIds: ["missing", "mate", "elder"] }),
      dino({ id: "elder", name: "Duplicate", age: 99 }),
      dino({ id: "stray", familyId: "other", mateId: "elder" }),
    ]);
    const clean = finishBook(raw);
    expect(clean.dinos.map((row) => row.id)).toEqual(["cub", "elder", "gran", "mate", "stray"]);
    expect(clean.dinos.find((row) => row.id === "elder")?.name).toBe("Moss");
    const cub = lineageOf(raw, "cub");
    expect(cub?.parentIds).toEqual(["elder", "mate"]);
    expect(cub?.generation).toBe(2);
    expect(cub?.ancestorIds).toEqual(["gran", "elder", "mate"]);
    expect(lineageOf(raw, "gran")?.descendantIds).toEqual(["elder", "mate", "cub"]);
    expect(lineageOf(raw, "gran")?.childIds).toEqual(["elder", "mate"]);
    expect(lineageOf(raw, "stray")?.mateId).toBeNull();
    expect(matedPair(raw, "swamp")?.map((row) => row.id)).toEqual(["elder", "mate"]);
  });

  it("stops on a parent cycle", () => {
    const cycled = book([
      dino({ id: "elder", parentIds: ["mate"] }),
      dino({ id: "mate", parentIds: ["elder"] }),
    ]);
    expect(lineageOf(cycled, "elder")?.ancestorIds).toEqual(["mate"]);
    expect(lineageOf(cycled, "mate")?.generation).toBeGreaterThan(0);
  });

  it("chooses the older mated pair when two bonds are live", () => {
    const crowded = book([
      dino({ id: "a", sex: "female", age: 10, mateId: "b" }),
      dino({ id: "b", sex: "male", age: 10, mateId: "a" }),
      dino({ id: "m", sex: "female", age: 9, mateId: "z" }),
      dino({ id: "z", sex: "male", age: 9, mateId: "m" }),
    ]);
    expect(matedPair(crowded, "swamp")?.map((row) => row.id)).toEqual(["a", "b"]);
    const tied = book([
      dino({ id: "a", sex: "female", age: 5, mateId: "b" }),
      dino({ id: "b", sex: "male", age: 5, mateId: "a" }),
      dino({ id: "m", sex: "female", age: 6, mateId: "z" }),
      dino({ id: "z", sex: "male", age: 4, mateId: "m" }),
    ]);
    expect(matedPair(tied, "swamp")?.map((row) => row.id)).toEqual(["a", "b"]);
  });
});

describe("heir pick", () => {
  it("seats the oldest living member, and breaks an age tie by id", () => {
    const kin = book([
      dino({ id: "elder", age: 3 }),
      dino({ id: "bea", age: 8 }),
      dino({ id: "ada", age: 8 }),
      dino({ id: "gone", age: 40, alive: false }),
    ]);
    expect(pickHeir(kin, "swamp", "elder")).toBe("ada");
    expect(barrelHeir(kin, "swamp")).toBe("ada");
    expect(pickHeir(kin, "swamp", "ada")).toBe("bea");
    const babies = book([
      dino({ id: "elder", age: -4 }),
      dino({ id: "zed", age: 1.2 }),
      dino({ id: "ann", age: 1.6 }),
    ]);
    expect(pickHeir(babies, "swamp", "elder")).toBe("ann");
    expect(pickHeir(book([dino({ id: "elder" })]), "swamp", "elder")).toBeNull();
  });
});

describe("succession", () => {
  const kin = () => book([
    ...pair(),
    dino({ id: "cub", name: "Dot", sex: "female", age: 2, parentIds: ["elder", "mate"] }),
  ]);

  it("moves the seat to the oldest survivor and clears the bond", () => {
    const before = kin();
    const snapshot = structuredClone(before);
    const result = successionEvent(before, { familyId: "swamp", slainId: "elder", atMinute: 12 });
    expect(before).toEqual(snapshot);
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(result.event.kind).toBe("elder-succession");
    expect(result.event.heirId).toBe("mate");
    expect(result.event.heirName).toBe("Nib");
    expect(result.event.hunt).toBeNull();
    expect(result.event.line).toContain("Nib is oldest and takes the seat");
    expect(result.event.line).not.toContain("Big Grumble");
    expect(result.event.tokens).toBe(estimateTokens(result.event.line));
    expect(result.event.tokens).toBeLessThanOrEqual(POST_TOKEN_CAP);
    expect(result.book.families[0]?.elderId).toBe("mate");
    expect(result.book.dinos.find((row) => row.id === "elder")).toMatchObject({ alive: false, mateId: null });
    expect(result.book.dinos.find((row) => row.id === "mate")?.mateId).toBeNull();
    expect(lineageOf(result.book, "cub")?.parentIds).toEqual(["elder", "mate"]);
    const again = successionEvent(result.book, { familyId: "swamp", slainId: "elder", atMinute: 13 });
    expect(again.ok).toBe(false);
    if (again.ok) return;
    expect(again.reason).toBe("already-dead");
    expect(again.book.families[0]?.elderId).toBe("mate");
  });

  it("sends Big Grumble after the player who killed the elder", () => {
    const result = barrelEvent(kin(), {
      familyId: "swamp",
      slainId: "elder",
      atMinute: 4,
      killedByPlayerKey: "Pip",
    });
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(result.event.hunt).toEqual({ grumbleId: BIG_GRUMBLE_ID, playerKey: "Pip", untameable: true });
    expect(result.event.line).toContain("Big Grumble hunts Pip and will not be tamed");
    const hunt = result.event.hunt;
    const both = { grumbleAlive: true, playerAlive: true };
    expect(grumbleTameAllowed(hunt, "Pip", both)).toBe(false);
    expect(grumbleTameAllowed(hunt, "Ada", both)).toBe(true);
    expect(grumbleTameAllowed(hunt, "Pip", { grumbleAlive: false, playerAlive: true })).toBe(true);
    expect(grumbleTameAllowed(hunt, "Pip", { grumbleAlive: true, playerAlive: false })).toBe(true);
    expect(grumbleTameAllowed(null, "Pip", both)).toBe(true);
  });

  it("leaves an empty seat when the elder was the last living member", () => {
    const alone = book([dino({ id: "elder", name: "Moss" })]);
    const result = successionEvent(alone, {
      familyId: "swamp",
      slainId: "elder",
      atMinute: 1,
      killedByPlayerKey: "Pip",
    });
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(result.event.heirId).toBeNull();
    expect(result.book.families[0]?.elderId).toBeNull();
    expect(result.event.line).toContain("The seat is empty");
    expect(result.event.hunt?.playerKey).toBe("Pip");
  });

  it("refuses a kill that is not the sitting elder and does not copy the book", () => {
    const before = kin();
    expect(successionEvent(before, { familyId: "swamp", slainId: "mate", atMinute: 1 })).toEqual({
      ok: false,
      reason: "not-elder",
      book: before,
    });
    expect(successionEvent(before, { familyId: "swamp", slainId: "nope", atMinute: 1 }).ok).toBe(false);
    expect(successionEvent(before, { familyId: "roost", slainId: "elder", atMinute: 1 })).toMatchObject({
      ok: false,
      reason: "wrong-family",
    });
    expect(noteDeath(before, "elder")).toBe(before);
    expect(before.dinos.find((row) => row.id === "elder")?.alive).toBe(true);
  });

  it("keeps a huge line inside the token cap", () => {
    const named = book([
      dino({ id: "elder", name: "Moss".repeat(80), mateId: "mate", age: 5 }),
      dino({ id: "mate", name: "Nib", sex: "male", age: 4, mateId: "elder" }),
    ], {
      families: [{ familyId: "swamp", name: "Swamp".repeat(40), elderId: "elder" }],
    });
    const result = successionEvent(named, {
      familyId: "swamp",
      slainId: "elder",
      atMinute: 2,
      killedByPlayerKey: "Pip".repeat(30),
    });
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(estimateTokens(result.event.line)).toBeLessThanOrEqual(POST_TOKEN_CAP);
    expect(result.event.hunt?.playerKey.length).toBeLessThanOrEqual(64);
  });
});

describe("spawns", () => {
  it("lays one seeded egg per due family and ignores a long pause", () => {
    const before = book(pair());
    const snapshot = structuredClone(before);
    const early = tickSpawns(before, SPAWN_EVERY_MINUTES - 1, 7);
    expect(early.births).toEqual([]);
    expect(early.held).toEqual([{ familyId: "swamp", reason: "early" }]);
    expect(early.seed).toBe(7);
    expect(before).toEqual(snapshot);

    const first = tickSpawns(before, 90, 7);
    const again = tickSpawns(before, 90, 7);
    expect(again).toEqual(first);
    expect(first.births).toHaveLength(1);
    expect(first.births[0]?.childId).toBe("kin-swamp-90");
    const expected = hatchFrom(7);
    const child = first.book.dinos.find((row) => row.id === "kin-swamp-90");
    expect(child).toMatchObject({
      name: expected.name,
      sex: expected.sex,
      age: 0,
      alive: true,
      mateId: null,
      parentIds: ["elder", "mate"],
      bornAtMinute: 90,
      familyId: "swamp",
    });
    expect(first.seed).toBe(expected.seed);
    expect(first.book.families[0]?.elderId).toBe("elder");
    expect(first.book.lastSpawnMinute.swamp).toBe(90);
    expect(nextSpawnMinute(first.book, "swamp")).toBe(90 + SPAWN_EVERY_MINUTES);
    expect(familySize(first.book, "swamp")).toBe(3);

    const followed = tickSpawns(first.book, 90 + SPAWN_EVERY_MINUTES, first.seed);
    expect(followed.births.map((birth) => birth.childId)).toEqual(["kin-swamp-120"]);
  });

  it("visits families in id order and spends the seed only on a birth", () => {
    const mixed = book(
      [
        dino({ id: "elder", familyId: "swamp", sex: "female", age: 8, mateId: "mate" }),
        dino({ id: "mate", familyId: "swamp", sex: "male", age: 7, mateId: "elder" }),
        dino({ id: "roost-elder", familyId: "roost", sex: "female", age: 6 }),
      ],
      {
        families: [
          { familyId: "swamp", name: "Swamp", elderId: "elder" },
          { familyId: "roost", name: "Roost", elderId: "roost-elder" },
        ],
      },
    );
    const ticked = tickSpawns(mixed, 30, 3);
    expect(ticked.births.map((birth) => birth.familyId)).toEqual(["swamp"]);
    expect(ticked.held).toEqual([{ familyId: "roost", reason: "no-pair" }]);
    expect(ticked.book.dinos.find((row) => row.id === "kin-swamp-30")?.sex).toBe(hatchFrom(3).sex);

    const both = book(
      [
        dino({ id: "swamp-a", familyId: "swamp", sex: "female", age: 4, mateId: "swamp-b" }),
        dino({ id: "swamp-b", familyId: "swamp", sex: "male", age: 4, mateId: "swamp-a" }),
        dino({ id: "roost-a", familyId: "roost", sex: "female", age: 3, mateId: "roost-b" }),
        dino({ id: "roost-b", familyId: "roost", sex: "male", age: 3, mateId: "roost-a" }),
      ],
      {
        families: [
          { familyId: "swamp", name: "Swamp", elderId: "swamp-a" },
          { familyId: "roost", name: "Roost", elderId: "roost-a" },
        ],
      },
    );
    const litter = tickSpawns(both, 30, 11);
    const roostHatch = hatchFrom(11);
    const swampHatch = hatchFrom(roostHatch.seed);
    expect(litter.births.map((birth) => birth.childId)).toEqual(["kin-roost-30", "kin-swamp-30"]);
    expect(litter.book.dinos.find((row) => row.id === "kin-roost-30")).toMatchObject({
      name: roostHatch.name,
      sex: roostHatch.sex,
      parentIds: ["roost-a", "roost-b"],
    });
    expect(litter.book.dinos.find((row) => row.id === "kin-swamp-30")).toMatchObject({
      name: swampHatch.name,
      sex: swampHatch.sex,
    });
    expect(litter.seed).toBe(swampHatch.seed);
  });

  it("holds a family with no pair or a full nest, then recovers after a death", () => {
    const lonely = book([dino({ id: "elder" })]);
    expect(tickSpawns(lonely, 30, 1).held).toEqual([{ familyId: "swamp", reason: "no-pair" }]);
    expect(nextSpawnMinute(lonely, "swamp")).toBeNull();
    const oneSided = book([
      dino({ id: "elder", sex: "female", mateId: "mate" }),
      dino({ id: "mate", sex: "male", mateId: null }),
    ]);
    expect(tickSpawns(oneSided, 40, 1).held[0]?.reason).toBe("no-pair");
    const sameSex = book([
      dino({ id: "elder", sex: "female", mateId: "mate" }),
      dino({ id: "mate", sex: "female", mateId: "elder" }),
    ]);
    expect(matedPair(sameSex, "swamp")).toBeNull();

    const full = book([
      ...pair(),
      ...Array.from({ length: FAMILY_CAP - 2 }, (_, index) => dino({
        id: `kin-${index}`,
        sex: index % 2 === 0 ? "female" : "male",
        age: index + 1,
      })),
    ]);
    expect(familySize(full, "swamp")).toBe(FAMILY_CAP);
    const blocked = tickSpawns(full, 30, 5);
    expect(blocked.births).toEqual([]);
    expect(blocked.held).toEqual([{ familyId: "swamp", reason: "cap" }]);
    expect(blocked.seed).toBe(5);
    expect(nextSpawnMinute(full, "swamp")).toBeNull();

    const opened = noteDeath(full, "kin-0");
    expect(familySize(opened, "swamp")).toBe(FAMILY_CAP - 1);
    const recovered = tickSpawns(opened, 30, 5);
    expect(recovered.births).toHaveLength(1);
    expect(familySize(recovered.book, "swamp")).toBe(FAMILY_CAP);
  });

  it("accepts a tuned interval and cap", () => {
    const rules = familyRules({ spawnEveryMinutes: 0, familyCap: 0, gankAt: Number.NaN });
    expect(rules.spawnEveryMinutes).toBe(1);
    expect(rules.familyCap).toBe(1);
    expect(rules.gankAt).toBe(GANK_FAMILY_SIZE);
    const laid = tickSpawns(book(pair()), 1, 2, { spawnEveryMinutes: 10 });
    expect(laid.births).toEqual([]);
    expect(tickSpawns(book(pair()), 10, 2, { spawnEveryMinutes: 10 }).births).toHaveLength(1);
    expect(tickSpawns(book(pair()), 50, 2, { familyCap: 2 }).held[0]?.reason).toBe("cap");
  });
});

describe("payback", () => {
  function crowd(count: number): FamilyBook {
    return book(Array.from({ length: count }, (_, index) => dino({
      id: index === 0 ? "elder" : `m-${index}`,
      age: count - index,
      alive: true,
    })));
  }

  it("ganks at five living members and flips a seeded coin under that", () => {
    const big = crowd(GANK_FAMILY_SIZE);
    const small = crowd(GANK_FAMILY_SIZE - 1);
    const gank = respondToKill(big, "swamp", 4);
    expect(gank.response?.kind).toBe("gank");
    expect(gank.response?.size).toBe(5);
    expect(gank.response?.memberIds).toEqual(["elder", "m-1", "m-2", "m-3", "m-4"]);
    expect(gank.seed).toBe(4);

    const seeds = Array.from({ length: 32 }, (_, index) => index + 1);
    const barterSeed = seeds.find((seed) => coinKind(seed) === "barter");
    const bountySeed = seeds.find((seed) => coinKind(seed) === "bounty");
    expect(barterSeed).toEqual(expect.any(Number));
    expect(bountySeed).toEqual(expect.any(Number));
    expect(respondToKill(small, "swamp", barterSeed ?? 1).response?.kind).toBe("barter");
    expect(respondToKill(small, "swamp", bountySeed ?? 1).response?.kind).toBe("bounty");
    expect(respondToKill(small, "swamp", barterSeed ?? 1).seed).toBe(stepSeed(barterSeed ?? 1).seed);

    const thinned = noteDeath(big, "m-1");
    expect(respondToKill(thinned, "swamp", barterSeed ?? 1).response?.kind).toBe("barter");
    expect(respondToKill(book([dino({ id: "elder", alive: false })]), "swamp", 1).response).toBeNull();
    expect(respondToKill(small, "swamp", 3, { gankAt: 4.2 }).response?.kind).toBe("gank");
    expect(respondToKill(big, "swamp", 3, { gankAt: 9 }).response?.kind).toBe(coinKind(3));
  });
});

describe("dead skin", () => {
  it("gives the skin on the fifth ask and starts payback on the first hit", () => {
    let pester = emptySkinPester();
    for (let ask = 1; ask < SKIN_ASKS; ask += 1) {
      const step = askForSkin(pester);
      expect(step.verdict.kind).toBe("wait");
      if (step.verdict.kind === "wait") expect(step.verdict.asksLeft).toBe(SKIN_ASKS - ask);
      pester = step.pester;
    }
    const gift = askForSkin(pester);
    expect(gift.verdict.kind).toBe("give-skin");
    expect(askForSkin(gift.pester).verdict.kind).toBe("give-skin");
    expect(askForSkin(gift.pester).pester.asks).toBe(SKIN_ASKS);

    const small = book([
      dino({ id: "elder", age: 3 }),
      dino({ id: "mate", sex: "male", age: 2 }),
    ]);
    const hit = hitForSkin(small, "swamp", emptySkinPester(), 8);
    expect(hit.verdict.kind).toBe("revenge");
    if (hit.verdict.kind !== "revenge") return;
    expect(hit.verdict.response?.kind).toBe(coinKind(8));
    expect(hit.pester.hits).toBe(REVENGE_HITS);
    expect(askForSkin(hit.pester).verdict).toMatchObject({ kind: "revenge", response: null });
    expect(hitForSkin(small, "swamp", hit.pester, 8).verdict).toMatchObject({ kind: "revenge", response: null });
    expect(hitForSkin(small, "swamp", hit.pester, 8).seed).toBe(8);

    const big = book(Array.from({ length: 5 }, (_, index) => dino({ id: index === 0 ? "elder" : `m-${index}` })));
    const ganked = hitForSkin(big, "swamp", gift.pester, 6);
    expect(ganked.verdict).toMatchObject({ kind: "revenge", response: { kind: "gank", size: 5 } });
    expect(ganked.seed).toBe(6);
  });

  it("waits out a higher hit threshold before it rolls", () => {
    const kin = book(pair());
    const first = hitForSkin(kin, "swamp", emptySkinPester(), 9, { revengeHits: 2 });
    expect(first.verdict.kind).toBe("wait");
    expect(first.seed).toBe(9);
    const second = hitForSkin(kin, "swamp", first.pester, 9, { revengeHits: 2 });
    expect(second.verdict.kind).toBe("revenge");
    expect(second.seed).toBe(stepSeed(9).seed);
    expect(askForSkin(emptySkinPester(), { skinAsks: 1 }).verdict.kind).toBe("give-skin");
  });
});

describe("a thinned nest still knows its dead", () => {
  it("inherits past the baby, and a broken bond does not keep laying", () => {
    const hatched = tickSpawns(book(pair(40, 30)), 30, 1);
    const childId = hatched.births[0]?.childId ?? "";
    const passed = successionEvent(hatched.book, {
      familyId: "swamp",
      slainId: "elder",
      atMinute: 30,
      killedByPlayerKey: "Pip",
    });
    expect(passed.ok).toBe(true);
    if (!passed.ok) return;
    expect(passed.event.heirId).toBe("mate");
    expect(passed.event.heirId).not.toBe(childId);
    expect(lineageOf(passed.book, childId)?.parentIds).toEqual(["elder", "mate"]);
    expect(matedPair(passed.book, "swamp")).toBeNull();
    expect(nextSpawnMinute(passed.book, "swamp")).toBeNull();
    expect(tickSpawns(passed.book, 60, 2).births).toEqual([]);
    expect(grumbleTameAllowed(passed.event.hunt, "Pip", { grumbleAlive: true, playerAlive: true })).toBe(false);
  });
});
