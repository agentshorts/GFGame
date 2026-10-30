# Jail leader handover

`UGFSocialComponent` already jails, mutes, and releases. This only changes who counts as the faction leader for that.

`FGFSocialRules::ChooseLeader` picks the earliest faction claim among the online members you pass in. A jailed member is skipped while anyone is free. If every remaining member is jailed, the earliest of them leads. `ClaimOrder <= 0` (no `OnFactionChanged` record) sorts last. Equal orders break ties by player key.

`AGFGameState::OnFactionChanged` fires after a successful `ServerClaimFaction`. The social component stores the first order per `PlayerKey|Faction` and `Describe` sets `bLeader` from `ChooseLeader`. `FactionLeaders` is still the first claimer for the whole session; other systems that call `IsFactionLeader` are unchanged.

Jail stays on `UGFSocialComponent` (session set, `PlayerKey`, no timer). There is no second jail flag on `AGFPlayerState`.
