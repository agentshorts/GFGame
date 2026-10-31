import { creatureEventLine, speciesForPersona, type CreatureEvent, type CreatureMood } from "../engines/creatureEliza";
import type { DinobookEventId } from "./types";

const EVENT_MOOD: Partial<Record<DinobookEventId, { event: CreatureEvent; mood: CreatureMood }>> = {
  attacked: { event: "hurt_by_player", mood: "Angry" },
  "refused-mango": { event: "pestered", mood: "Wary" },
  "rental-downed": { event: "bounty_posted", mood: "Bountied" },
  "summon-big-grumble": { event: "called_for_help", mood: "Wary" },
};

/** Dinobook events that already have a creature template. Payday stays on the wage lines. */
export function dinobookCreatureBark(eventId: DinobookEventId, dinoId: string, player: string): string | null {
  const mapped = EVENT_MOOD[eventId];
  if (!mapped) return null;
  return creatureEventLine({
    species: speciesForPersona(dinoId),
    mood: mapped.mood,
    event: mapped.event,
    player,
    rng: () => 0,
  }).text;
}

export function familyLostBark(dinoId: string, player: string): string {
  return creatureEventLine({
    species: speciesForPersona(dinoId),
    mood: "Grieving",
    event: "family_lost",
    player,
    rng: () => 0,
  }).text;
}
