# GFGame: stated game goals (v1)

> *The one page every mini-game gets measured against. When the code and this page disagree, one of them is wrong, and we'll find out which in a goal-alignment issue rather than by vibes.*

- **Written:** Oct 30, 2026 (America/Chicago), from the design chat, saved notes, the GitHub issues, `PLAN.md`, `README.md` and the planning notes.
- **Rule for this page:** it records only what Captain said or approved. Team defaults that were logged as `Decision:` issues are marked **(team default)**. Anything not settled is under **Open questions**.
- **Decisions rule:** per Captain's standing rule, bots pick a sensible default and log it rather than asking him. Each open question below says which default applies until he overrules it.
- **Inspirations:** every "Inspired by" note is a mood reference for this page only. No other game's names, art, UI, sounds or logos go into the game, its file names or its art prompts.
- **Tracking:** each game has a `Goal alignment: <game>` issue, and the `Goal alignment: index` issue links them all.

### Source key
| Tag | Meaning |
|---|---|
| **C-1029** | Captain, design chat, Oct 29 2026 |
| **C-1030** | Captain, design chat, Oct 30 2026 |
| **X-1030** | Captain's idea posts, early Oct 30 2026 (queued **last**, after everything already planned) |
| **#N** | GitHub issue N (`Decision:` issues are team defaults unless they say otherwise) |
| **PLAN** | `PLAN.md` on `staging` (team-written Oct 29; where Captain resolved a question it says so) |

---

## 0. Right-now priority (applies to every game)
- **Get into the game playable, even if it's broken.** Polish comes later. (C-1030)
- Core game before the paint-and-effects editor. Existing editor work stays, but core-game tasks win. (C-1030)
- Code flow: feature branches merge into `staging` when good enough, failures get fixed there, then `staging` goes to `main`. Tests run on merges to `main`, and a merge is fine if most pass. (C-1030)
- Cloud agents write code and tests only (no Unreal build or run). Local bots review, build and test on the dev PC. (C-1030)
- Playable-build work: #78 world, #79 visuals, #80 wild dinos, #81 Big Grumble, #82 HUD, #85 packaging.

## 1. The hub and the world
**Stated goals**
- A multiplayer co-op "friendslop" game from day one, with networking built in from the start. Friends drop into the hub together. (C-1029)
- Chunky voxel art style, like a block-building game. No deadline. (C-1029)
- Max **10 players**. (C-1030)
- **Premise:** players are trapped in a hell world where the house-and-garden circle is the only safe zone. They risk the outside mini-games to gather minerals. The escape-device idea is **dropped**. The endgame gives the material used to tame Big Grumble. (C-1030)
- **The Unbinding** frees nobody. It exists only to get Big Grumble's taming material. Settings: how many players it needs and who gets the material. Defaults: all 10 players, host gets it. (C-1030)
- The safe circle grows with the player count up to a limit (small when solo), and the safe land expands for houses, gardens and buildings. (C-1030)
- **Rime is the official starter zone.** Most players start in Verdant or Rime. (C-1030; spawn defaults #32, #65)
- Factions own zones. The faction leader decides which members can turn off visitor protection. In another faction's zone, members can turn off a visitor's safety or drag them so they can't run while Big Grumble chases. "Trick your friends into getting murdered" is the spirit. (C-1030)
- **Materials are the only currency.** No coins or money, so all bets and rewards are materials. (C-1029)
- Mini-game materials are only rewarded for **winning**, so playing is high-risk. (C-1030)
- The economy is **generous**, with high drop rates except in the auto-miner area, and Big Grumble tamable in about **a week**. (C-1030)
- **Death rules:** deaths annoy friends a little without losing hard work. Dying while farming dinos drops a little material and respawns you at the circle's edge. (C-1030)
- Players can paint their own blocks and materials and add effects (glow, sparkle), synced in co-op and baked into data when applied, not computed live. The plugin should be drivable by AI too. **After** the core game. (C-1030)

**Inspired by:** Minecraft (art style only).

**Open questions (default in force)**
- `PLAN.md` and `README.md` still pitch a **cozy builder with no fail-states**, but the premise is now a **hell world with one safe circle**. Default: the hub stays cozy and safe, everything outside it is dangerous, and the docs get updated to say so.
- "Winning only pays" vs "generous economy": the generous-economy plan (losers get 50%) is queued, not approved. Default: wins-only stays until Captain picks.

## 2. Beat Hop (rhythm bunny-hop)
**Stated goals**
- A rhythm bunny-hop ("bhop") mini-game, named **Beat Hop**. (C-1030)
- Live mode syncs to whatever the player is already playing on their PC (output-device loopback, never the mic), so the player's own music is the soundtrack and there's nothing to license. Built-in songs mode keeps a few licence-free tracks. (PLAN Q12, resolved Oct 29)
- Mini-game materials pay on a win. (C-1030)

**Inspired by:** lane-based rhythm games and obby/bhop courses (PLAN).

**Open questions (default in force)**
- Whether a Beat Hop "win" is a grade threshold or finishing the song. Default: grade B or better counts as a win.

## 3. Fashion show
**Stated goals**
- A fashion show mini-game. (C-1029)
- Works like a themed dress-up game: a theme, a dress-up timer, a runway walk, then a vote. Original art and names only. (C-1030)
- Item and brand names are funny parodies of real fashion houses, spun to fit the game's own world (e.g. "Dirtachi"), cleared as low-risk in-world flavour. Each final name gets a trademark check, and the README says all brand names are fictional. "Run with it, we'll change it later." (C-1030; #86, #89)
- No logos, colour schemes, fonts or slogans that echo real brands. Names stay in-world flavour, never the game title or merch. (C-1030, legal read)
- The **dead-skin purse:** one elder lets players take dead skin from its corpses only after being pestered repeatedly, and the skin makes a purse in the fashion show. The alternative is attacking dinos for it, which triggers family revenge or a bounty. (C-1030; #20 team default: 5 asks, 1 hit triggers revenge)
- Item mockups (#87) and material recipes (#88) follow the brand list.

**Inspired by:** Dress to Impress (Roblox).

**Open questions (default in force)**
- `PLAN.md` M3 scores outfits with **AI judges**. The stated goal is a **player vote**. Default: players vote, and AI judges only fill in when someone plays solo.

## 4. 5v5 MOBA
**Stated goals**
- A MOBA. (C-1029)
- Max 10 players, so it can be a **true 5v5**. (C-1030)
- **Crafting instead of gold:** minions drop basic crafting items, jungle monsters drop jungle-focused items, big monsters drop rare powerful items. Only **2 big monsters** (a minor and a major, both count), and fully equipping a team takes **5 big-monster kills**. (C-1030)
- Dying in the MOBA loses nothing. (C-1030)

**Inspired by:** classic lane MOBAs (genre only).

**Open questions (default in force)**
- `PLAN.md` M4 is still **1 lane, 3v3 vs bots, no jungle, no items**. That conflicts with all of the above. Default: rescope M4 to 5v5 with a jungle and the crafting drops, with bots filling empty slots.
- How many lanes? Default: 1 lane plus a jungle, for scope.

## 5. Pebble Mine (zombie auto-idler)
**Stated goals**
- The mining mini-game area is the **Pebble Mine**. (C-1030)
- It's the **auto-idler**, a separate game from the factory game. (C-1030)
- Cute, original bunny-like helper creatures (captured mid-early) gather in the mine rooms the player points them to. No infinite digging. They're called home to deposit materials. (C-1030)
- It's the one area that is **not** generous: normal drop rates. (C-1030)

**Inspired by:** Zombie Miner (the idle loop), with helpers in a Chiikawa/Hachiware-like cute style (original designs).

**Open questions**
- None new. Dinos don't work here. Factory wages and mate rank stay in the factory game.

## 6. Voxel lab co-op
**Stated goals**
- A co-op voxel lab where players craft gear from office junk, fight anomalies and scavenge materials together. Original names and art. (C-1030)

**Inspired by:** Abiotic Factor.

**Open questions (default in force)**
- The repo's lab mode (`GFLab`, "Overtime lab") has junk shifts and junk-to-gear recipes, but no **anomalies** yet. Default: anomalies are the lab's hostile threat and get their own work item.

## 7. Hallow Night (platformer)
**Stated goals**
- A co-op platformer area called **Hallow Night**: WASD to move and jump, Z/X/C move platforms, and anyone can trigger any platform. It's the **hardest area** and pays the **best minerals**. Name and assets stay original. (C-1030)
- Tight controls: quick run, variable jump, dash and wall-jump, with the co-op platform buttons layered on top. (C-1030)
- Voxel art, but played as a **2.5D side-scroller**: side camera, players locked to a flat lane. (C-1030)
- Dying loses nothing, and there are many lives so friends can knock each other off platforms. (C-1030)
- **Water areas where rented dinos help**, e.g. a water dino catches you and swims you across. (C-1030; helper tools catch, float and fly-over are the #19 team default)

**Inspired by:** Hollow Knight: Silksong (the latest Hollow Knight). The name "Hallow Night" is a nod, so keep its art and level names fully original.

**Open questions**
- None new.

## 8. Factory game (Factorio-style)
**Stated goals**
- A separate factory-building mini-game that uses dinos as workers: **rented**, or **tamed and placed** in factories. (C-1030)
- Working gives dinos a way to communicate, complain and form relationships. A dino in its own territory may walk up to a player with a good factory and ask for a job. (C-1030)
- **Wages mean mate potential** (male and female). Dinos brag on Dinobook about being paid for finding minerals (ties back to renting). High earners get first choice of mates, first choice always goes to high stats and HP, those dinos become elders, and their kids have better odds, so dinos grow bigger and stronger over time. (C-1030)
- Separate from Pebble Mine. (C-1030)

**Inspired by:** Factorio.

**Open questions (default in force)**
- No factory machines, belts or production chains are designed yet. Default per #98: after the playable push, and a design issue comes first.

## 9. Social zone and Theatre (queued)
**Stated goals**
- A small social safe zone with a **buildable theatre** for watching movies together, with VR for watching. (X-1030, queued last)

**Inspired by:** VRChat.

**Open questions (default in force)**
- Everything past the headline (movie source, VR scope, cost) is a planning note, not approved. Default: nothing gets built until it comes up in the queue.

## 10. Sword duels (queued)
**Stated goals**
- Sword duels in the safe zone. (X-1030, queued last)

**Open questions (default in force)**
- Moves, wagers and rules are planning notes only. Default: opt-in friendly duels where nothing is lost.

## 11. Old West gun game (queued)
**Stated goals**
- An Old West gun game. (X-1030, queued last. The original note is cut off, so this is everything we have.)

**Open questions (default in force)**
- What the game actually is. The planning note guesses a one-on-one quick-draw duel. Default: hold until Captain describes it.

## 12. Online rewards and proximity voice (queued)
**Stated goals**
- Login rewards and rewards for staying online. (X-1030, queued last)
- Proximity voice chat through Unreal. (X-1030, queued last)

**Open questions (default in force)**
- Reward amounts and cycle are planning notes only. Default: no streak loss and no real money, ever.
- Voice joining method. Default: Unreal's built-in voice, and jail keeps muting.

## 13. Dino taming and the dino arena
**Stated goals**
- Each dino kind is tamed with food it accepts (several accepted foods plus several favourites) that the player has to discover. The wrong food makes it attack. (C-1030)
- Taming is a separate area from the arena. (C-1030)
- **Arena:** watch from a balcony, place up to 3 dinos that auto-battle, "slow time" once every 30 s to order abilities and cast spells, and take control of any dino in real time and switch between them, all with a simple menu. (C-1030)
- Arena rewards: every match gives a free reward roll with terrible odds on the good stuff, and before a fight you can bet materials for a better payout. AI opponents bet something similar in type and value to what's in your inventory. (C-1030)
- Friends fight in the arena with or against each other. (C-1029)
- Dinos have mates and tunable spawn rates, so family sizes change over time. (C-1030; #16 team default)
- Murderous dinos (T. rex, raptors) are the heart of it. (C-1030)

**Inspired by:** Palworld and Pokémon (taming), Teamfight Tactics (auto-battle).

**Open questions (default in force)**
- The repo's arena (`GFArena`) runs staked bouts with a seeded fight sim. The balcony view, slow-time and take-control aren't confirmed in code. Default: the audit lists them as gaps.

## 14. Dinobook, elders and Big Grumble
**Stated goals**
- **Dinobook** is the dinos' social network. It's a tree through the elders: every dino of a type links to its elder, and friendly dinos and animals have extra side links for ganging up. Dinos only have their own family plus their type's elder. (C-1030; #13)
- Dinos **coordinate on Dinobook first** and use all chat sparingly, e.g. only when the leader demands the mango. How often they speak in all chat is tunable. (C-1030)
- When a player shows up, dinos coordinate, then move into the player's path. One may take a deliberate hit, then they shake the player down for mango in each dino's own voice. No spam or repeated lines: only a few dinos talk per event, they take turns, and later lines build on earlier ones. (C-1030)
- Killing a member of a big family brings the family to gank you. A small family might barter or post a bounty with its elder that any dino can take. (C-1030; #15)
- If a player kills an elder, a new one is picked and Big Grumble instantly hunts that player, staying untamable for them until one of them dies. (C-1030; #17)
- Internal per-player stats (kills by family, hits taken, mango given or refused, rental outcomes, bounties, rep per family), which elders check before answering. (C-1030)
- Dino chat: classic chatbots fronted by a small model with about 4k of context per dino. Each dino's alignment drives what it does when it smells a material within 30 ft (ambush, barter, beg, protection deals, extortion). (C-1030; #12, #14)
- Dino speech noises are short, pitched, original gibberish bursts while text shows. (C-1030)
- The **dino gang** roams the whole map (other dinos stay home), and follows players into any map. (C-1030)
- **Dino Chat Lab:** a browser tool that uses existing chatbot modules first. It gets a Dinobook simulator panel with event triggers, and a lite single-screen persona manager where each visitor picks a username (saved for them), data lives in SQLite, and new usernames are capped at **100 a day** in a separate database. (C-1030; #75, #76, #77)
- **Big Grumble:** a giant (about 38 m) taunting dino from a scaled-up T. rex rig. He herds players toward the safe circle, goes after new players first, eats anyone who attacks him, slows down if nobody attacks, and eats anyone who stops moving. He taunts in all chat ("Where you going, huh?"), leaves after about 5 minutes or right after eating someone, and telling newbies to "go fight it" is the prank. When summoned, he first asks all the elders on Dinobook about the player. (C-1030)
- Queued for later: trash-talking dinos in all chat, a chatty PG bird, and gossip-driven revenge ambushes. (X-1030)

**Inspired by:** Star Fox radio chatter (gibberish voice bursts only, no Nintendo audio).

**Open questions (default in force)**
- The early posts call the feed "Clawbook" and later ones say "Dinobook". Default: **Dinobook**.

## 15. Dino rentals and the ferry
**Stated goals**
- **Elders are shopkeepers.** Players trade materials or rent dinos from that faction. Rented dinos act as if tamed. A rented dino that would die quits instead, runs off and posts on Dinobook, which moves prices. Returning it on time with minerals raises your rep. (C-1030)
- Rental price is X blocks plus 10% of each harvested mineral. (C-1030; #18 team default: 20 blocks plus 10% for 15 min)
- You tell rented dinos what to do in chat, and the chat model picks an obvious tool call that loads a scripted behaviour. Dinos can also offer help: "want me to catch you?", "want me to float the water area?", "want me to just fly you over?" (C-1030; #19)
- **Ferry:** a flying dino (Ripwing, #64) carries players between the two safe zones. The land route between them has constantly spawning high-level mobs. The ferry dino is always there and charges early blocks per trip. Later, players can buy a lifetime pass with its taming material, but it never follows the player. It can't be attacked because of its protection deal with the factions. (C-1030)

**Open questions (default in force)**
- The ferry decision issues disagree with each other (fare, pass cost, mob table). Default: the newest consolidated decision (#65) wins, and the older issues get a superseded note.

---

## Changelog
- v1 (Oct 30 2026): first version.
