#pragma once

// Turns a mining-spell cone into ApplyDamage on wild dinos. Bound from the game mode.
// Fist swings already call ApplyDamage on the pawn the trace hits. Both land in AGFDino::TakeDamage.

class UGFBlockIntegritySubsystem;

void GFBindDinoSpellHits(UGFBlockIntegritySubsystem* Magic);
