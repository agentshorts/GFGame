import { describe, expect, it } from "vitest";
import { DEFAULT_REP_DELTAS, estimateTokens } from "../src/core/types";
import { POST_TOKEN_CAP } from "../src/dinobook/types";
import {
  DEFAULT_RENTAL_TERMS,
  activeRentals,
  createLedger,
  downRental,
  elderOf,
  harvestMineral,
  normalizeTerms,
  openRental,
  owedMinerals,
  quoteRental,
  returnRental,
  withTerms,
  type OpenRentalInput,
  type RentalLedger,
  type RentalResult,
  type RentalSettlement,
} from "../src/dinobook/rentals";

const at = "2026-10-30T00:00:00.000Z";
const due = "2026-10-30T00:15:00.000Z";

function openInput(partial: Partial<OpenRentalInput> = {}): OpenRentalInput {
  return {
    id: "rent-1",
    playerId: "pip",
    dinoId: "ripwing",
    elderId: "lanternmaw",
    familyId: "swamp",
    at,
    blocks: 20,
    ...partial,
  };
}

function opened(ledger: RentalLedger, partial: Partial<OpenRentalInput> = {}) {
  const result = openRental(ledger, openInput(partial));
  if (!result.ok) throw new Error(`${result.reason}: ${result.detail}`);
  return result;
}

function settled(result: RentalResult<RentalSettlement>): RentalSettlement {
  if (!result.ok) throw new Error(`${result.reason}: ${result.detail}`);
  return result.value;
}

describe("rental terms", () => {
  it("starts from decision 18", () => {
    expect(DEFAULT_RENTAL_TERMS).toEqual({
      upfrontBlocks: 20,
      mineralShare: 0.1,
      durationMs: 15 * 60 * 1000,
      downedPriceBump: 0.1,
      mineralReturnRep: 1,
      plainReturnRep: 0,
      lateRep: 0,
      downedRep: 0,
    });
    const ledger = createLedger();
    expect(ledger.terms).toEqual(DEFAULT_RENTAL_TERMS);
    expect(quoteRental(ledger, "lanternmaw")).toMatchObject({
      listBlocks: 20,
      upfrontBlocks: 20,
      priceScaleBps: 10_000,
      mineralShareBps: 1_000,
      durationMs: 15 * 60 * 1000,
    });
  });

  it("clamps tuned numbers and keeps the decision when a value is not a number", () => {
    expect(normalizeTerms({ mineralShare: 5, downedPriceBump: -1, upfrontBlocks: 20.9 }).upfrontBlocks).toBe(20);
    expect(normalizeTerms({ mineralShare: 5 }).mineralShare).toBe(1);
    expect(normalizeTerms({ downedPriceBump: -1 }).downedPriceBump).toBe(0);
    expect(normalizeTerms({ durationMs: 0 }).durationMs).toBe(1);
    expect(normalizeTerms({ upfrontBlocks: Number.NaN }).upfrontBlocks).toBe(20);
    expect(normalizeTerms({ mineralReturnRep: 400 }).mineralReturnRep).toBe(100);
  });

  it("floors each mineral's cut on its own", () => {
    expect(owedMinerals([
      { id: "Amber", count: 10 },
      { id: "Fossil", count: 9 },
      { id: "Bone", count: 25 },
      { id: "Scales", count: 0 },
    ], 0.1)).toEqual([
      { id: "Amber", count: 1 },
      { id: "Bone", count: 2 },
    ]);
    expect(owedMinerals([{ id: "Amber", count: 10 }], 0)).toEqual([]);
    expect(owedMinerals([{ id: "Amber", count: 4 }], 1)).toEqual([{ id: "Amber", count: 4 }]);
  });
});

describe("opening a rental", () => {
  it("charges 20 blocks and is due in 15 minutes", () => {
    const ledger = createLedger();
    const before = structuredClone(ledger);
    const result = opened(ledger);
    expect(ledger).toEqual(before);
    expect(result.value.blocksCharged).toBe(20);
    expect(result.value.blocksLeft).toBe(0);
    expect(result.value.rental.startedAt).toBe(at);
    expect(result.value.rental.dueAt).toBe(due);
    expect(result.value.rental.status).toBe("active");
    expect(result.value.rental.ranOff).toBe(false);
    expect(activeRentals(result.ledger, "pip").map((rental) => rental.id)).toEqual(["rent-1"]);
  });

  it("refuses a short purse, a busy dino, a duplicate slip, and a mixed-up elder", () => {
    const ledger = createLedger();
    const short = openRental(ledger, openInput({ blocks: 19 }));
    expect(short.ok).toBe(false);
    if (!short.ok) expect(short.reason).toBe("short-blocks");
    expect(ledger.rentals).toEqual([]);

    const first = opened(ledger, { blocks: 30 });
    expect(first.value.blocksLeft).toBe(10);
    const busy = openRental(first.ledger, openInput({ id: "rent-2", blocks: 30 }));
    expect(busy.ok).toBe(false);
    if (!busy.ok) expect(busy.reason).toBe("dino-busy");

    const other = opened(first.ledger, { id: "rent-2", dinoId: "tusksaw", blocks: 30 });
    const duplicate = openRental(other.ledger, openInput({ id: "rent-2", dinoId: "scythepaw", blocks: 30 }));
    expect(duplicate.ok).toBe(false);
    if (!duplicate.ok) expect(duplicate.reason).toBe("duplicate-id");

    const mismatch = openRental(other.ledger, openInput({
      id: "rent-3",
      dinoId: "scythepaw",
      familyId: "sickle",
      blocks: 30,
    }));
    expect(mismatch.ok).toBe(false);
    if (!mismatch.ok) expect(mismatch.reason).toBe("elder-family");
  });

  it("refuses blocks and ids that are not whole shop values", () => {
    const ledger = createLedger();
    expect(openRental(ledger, openInput({ blocks: 20.5 })).ok).toBe(false);
    expect(openRental(ledger, openInput({ blocks: -1 })).ok).toBe(false);
    expect(openRental(ledger, openInput({ id: " __proto__ " })).ok).toBe(false);
    expect(openRental(ledger, openInput({ at: "lunch" })).ok).toBe(false);
    expect(openRental(ledger, openInput({ dinoId: " " })).ok).toBe(false);
  });
});

describe("harvest and return", () => {
  it("adds a second scoop of the same mineral and pays rep only when some came home on time", () => {
    const first = opened(createLedger(), { blocks: 40 });
    const scooped = harvestMineral(first.ledger, "rent-1", "Amber", 4);
    if (!scooped.ok) throw new Error(scooped.detail);
    const hauled = harvestMineral(scooped.ledger, "rent-1", "Amber", 6);
    if (!hauled.ok) throw new Error(hauled.detail);
    const fossil = harvestMineral(hauled.ledger, "rent-1", "Fossil", 9);
    if (!fossil.ok) throw new Error(fossil.detail);
    expect(fossil.value.minerals).toEqual([
      { id: "Amber", count: 10 },
      { id: "Fossil", count: 9 },
    ]);

    const back = returnRental(fossil.ledger, "rent-1", due);
    const settlement = settled(back);
    expect(settlement.outcome).toBe("returned");
    expect(settlement.statKind).toBe("rental_returned");
    expect(settlement.statAmount).toBe(1);
    expect(settlement.repDelta).toBe(1);
    expect(settlement.rep).toBe(1);
    expect(settlement.shareDue).toEqual([{ id: "Amber", count: 1 }]);
    expect(settlement.post).toBeNull();
    expect(settlement.quit).toBe(false);
    expect(settlement.died).toBe(false);
    expect(settlement.priceBumpBps).toBe(0);
    expect(elderOf(back.ok ? back.ledger : first.ledger, "lanternmaw")?.repByPlayer.pip).toBe(1);

    const again = opened(back.ok ? back.ledger : first.ledger, { id: "rent-2", blocks: 40 });
    const second = settled(returnRental(again.ledger, "rent-2", due));
    expect(second.outcome).toBe("plain");
    expect(second.statKind).toBe("rental_returned");
    expect(second.repDelta).toBe(0);
    expect(second.rep).toBe(1);
    expect(second.shareDue).toEqual([]);
    expect(second.post).toBeNull();
    expect(activeRentals(again.ledger)).toHaveLength(1);
  });

  it("treats the due instant as on time and the next millisecond as late", () => {
    const haul = harvestMineral(opened(createLedger()).ledger, "rent-1", "Bone", 1);
    if (!haul.ok) throw new Error(haul.detail);
    expect(settled(returnRental(haul.ledger, "rent-1", due)).outcome).toBe("returned");

    const lateLedger = harvestMineral(opened(createLedger()).ledger, "rent-1", "Bone", 10);
    if (!lateLedger.ok) throw new Error(lateLedger.detail);
    const late = settled(returnRental(lateLedger.ledger, "rent-1", "2026-10-30T00:15:00.001Z"));
    expect(late.outcome).toBe("late");
    expect(late.statKind).toBe("rental_late");
    expect(late.repDelta).toBe(0);
    expect(late.shareDue).toEqual([{ id: "Bone", count: 1 }]);
    expect(late.post).toBeNull();
    expect(late.rep).toBe(0);
  });

  it("keeps the cut that was agreed at open", () => {
    const openedOnce = opened(createLedger());
    const hauled = harvestMineral(openedOnce.ledger, "rent-1", "Amber", 10);
    if (!hauled.ok) throw new Error(hauled.detail);
    const tuned = withTerms(hauled.ledger, { mineralShare: 1, upfrontBlocks: 5 });
    const back = settled(returnRental(tuned, "rent-1", due));
    expect(back.shareDue).toEqual([{ id: "Amber", count: 1 }]);
    expect(quoteRental(tuned, "lanternmaw").upfrontBlocks).toBe(5);
  });

  it("refuses a haul or a return after the slip is closed", () => {
    const done = returnRental(opened(createLedger()).ledger, "rent-1", at);
    if (!done.ok) throw new Error(done.detail);
    const harvest = harvestMineral(done.ledger, "rent-1", "Amber", 1);
    expect(harvest.ok).toBe(false);
    if (!harvest.ok) expect(harvest.reason).toBe("not-active");
    const again = returnRental(done.ledger, "rent-1", due);
    expect(again.ok).toBe(false);
    if (!again.ok) expect(again.reason).toBe("not-active");
    expect(harvestMineral(done.ledger, "missing", "Amber", 1).ok).toBe(false);
    expect(harvestMineral(done.ledger, "rent-1", " ", 1).ok).toBe(false);
    const zero = harvestMineral(opened(createLedger()).ledger, "rent-1", "Amber", 0);
    expect(zero.ok).toBe(false);
    if (!zero.ok) expect(zero.reason).toBe("bad-mineral");
    expect(returnRental(createLedger(), "rent-1", "yesterday").ok).toBe(false);
  });
});

describe("downed rentals", () => {
  it("quits, runs off, posts, and raises the elder's price by 10 percent", () => {
    const hauled = harvestMineral(opened(createLedger(), { blocks: 40 }).ledger, "rent-1", "Amber", 10);
    if (!hauled.ok) throw new Error(hauled.detail);
    const down = downRental(hauled.ledger, "rent-1", {
      at: "2026-10-30T00:20:00.000Z",
      playerName: " Pip ",
      dinoName: "Ripwing",
      elderName: "Lanternmaw",
    });
    const settlement = settled(down);
    expect(settlement.outcome).toBe("downed");
    expect(settlement.statKind).toBe("rental_downed");
    expect(settlement.quit).toBe(true);
    expect(settlement.ranOff).toBe(true);
    expect(settlement.died).toBe(false);
    expect(settlement.rental.ranOff).toBe(true);
    expect(settlement.repDelta).toBe(0);
    expect(settlement.rep).toBe(0);
    expect(settlement.shareDue).toEqual([{ id: "Amber", count: 1 }]);
    expect(settlement.priceBumpBps).toBe(1_000);
    expect(settlement.priceScaleBps).toBe(11_000);
    expect(settlement.post).toBe(
      "Ripwing quit. Pip downed the rental, so I ran off. Lanternmaw's prices go up. I am not dead.",
    );
    expect(estimateTokens(settlement.post ?? "")).toBeLessThanOrEqual(POST_TOKEN_CAP);
    expect(settlement.post).not.toMatch(/\bdied\b/i);

    const next = opened(down.ok ? down.ledger : hauled.ledger, { id: "rent-2", dinoId: "tusksaw", blocks: 40 });
    expect(next.value.blocksCharged).toBe(22);
    expect(next.value.blocksLeft).toBe(18);

    const second = settled(downRental(next.ledger, "rent-2", at));
    expect(second.priceScaleBps).toBe(12_000);
    expect(quoteRental(next.ledger, "lanternmaw").upfrontBlocks).toBe(22);
    const afterSecond = downRental(next.ledger, "rent-2", at);
    if (!afterSecond.ok) throw new Error(afterSecond.detail);
    expect(quoteRental(afterSecond.ledger, "lanternmaw").upfrontBlocks).toBe(24);
  });

  it("uses a short post when the names would blow the token cap", () => {
    const name = "N".repeat(120);
    const down = settled(downRental(opened(createLedger()).ledger, "rent-1", {
      at,
      playerName: name,
      dinoName: name,
      elderName: name,
    }));
    expect(down.post).toBe("Quit. I ran off. I am not dead. The elder's prices go up.");
    expect(estimateTokens(down.post ?? "")).toBeLessThanOrEqual(POST_TOKEN_CAP);
  });

  it("does not let a later price knob rewrite an open rental, and it stops at 10x", () => {
    const ledger = withTerms(createLedger(), { downedPriceBump: 0.1, downedRep: -6 });
    const down = settled(downRental(opened(ledger).ledger, "rent-1", at));
    expect(down.rep).toBe(-6);
    expect(down.priceBumpBps).toBe(1_000);

    const snapped = opened(createLedger());
    const retuned = withTerms(snapped.ledger, { downedPriceBump: 0.5 });
    expect(settled(downRental(retuned, "rent-1", at)).priceBumpBps).toBe(1_000);

    let current = withTerms(createLedger(), { downedPriceBump: 1, upfrontBlocks: 1 });
    for (let i = 0; i < 9; i += 1) {
      const slip = opened(current, { id: `rent-${i}`, blocks: 100 });
      const closed = downRental(slip.ledger, `rent-${i}`, at);
      if (!closed.ok) throw new Error(closed.detail);
      current = closed.ledger;
    }
    expect(quoteRental(current, "lanternmaw").priceScaleBps).toBe(100_000);
    expect(quoteRental(current, "lanternmaw").upfrontBlocks).toBe(10);
    const capped = opened(current, { id: "rent-cap", blocks: 100 });
    const stuck = settled(downRental(capped.ledger, "rent-cap", at));
    expect(stuck.priceBumpBps).toBe(0);
    expect(stuck.priceScaleBps).toBe(100_000);
    expect(stuck.quit).toBe(true);
  });

  it("still counts as downed when the clock is already past due", () => {
    const down = settled(downRental(opened(createLedger()).ledger, "rent-1", "2026-10-30T00:16:00.000Z"));
    expect(down.outcome).toBe("downed");
    expect(downRental(createLedger(), "rent-1", "2026-10-29T23:00:00.000Z").ok).toBe(false);
    const backwards = returnRental(opened(createLedger()).ledger, "rent-1", "2026-10-29T23:59:00.000Z");
    expect(backwards.ok).toBe(false);
    if (!backwards.ok) expect(backwards.reason).toBe("time-backwards");
  });
});

describe("rep stays inside the core's clamp", () => {
  it("stops at 100 and does not borrow the core's rental_returned delta", () => {
    expect(DEFAULT_RENTAL_TERMS.mineralReturnRep).toBe(1);
    expect(DEFAULT_REP_DELTAS.rental_returned).toBe(2);
    let ledger = withTerms(createLedger(), { mineralReturnRep: 80 });
    for (let i = 0; i < 2; i += 1) {
      const slip = opened(ledger, { id: `rent-${i}`, blocks: 20 });
      const hauled = harvestMineral(slip.ledger, `rent-${i}`, "Amber", 1);
      if (!hauled.ok) throw new Error(hauled.detail);
      const back = returnRental(hauled.ledger, `rent-${i}`, due);
      if (!back.ok) throw new Error(back.detail);
      ledger = back.ledger;
    }
    expect(elderOf(ledger, "lanternmaw")?.repByPlayer.pip).toBe(100);
  });
});
