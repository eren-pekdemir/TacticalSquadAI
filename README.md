# Tactical Squad AI — Unreal Engine 5.7, C++

A squad of soldiers that **thinks like a team**: they lose track of you, guess where you went, pin you down while
one of them sneaks around your side, push when they hear you reload, and fall back when their friends drop.

Everything lives in a game-agnostic C++ plugin (`Plugins/SquadAI`) with zero Blueprint logic, plus a small
third-person shooter that hosts it. Press **F1** in game to see every decision as it happens.

---

## What makes it different from "chase, shoot, repeat"

| System | What the player sees | How it works |
|---|---|---|
| **Belief, not omniscience** | Break line of sight and they search where they *think* you are | Perception writes into a per-agent memory (`USquadKnowledgeComponent`). Confidence decays, an uncertainty radius grows, position is extrapolated from last velocity. The brain never reads your real position. |
| **Utility AI** | Decisions that look intentional, never twitchy | 10 actions each score themselves 0..1 from context (`USquadBrainComponent`). Momentum bonus prevents dithering; committed actions (peeking, reloading, flanking) can't be interrupted mid-way. |
| **Procedural cover** | They always find sensible cover, on any map | `USquadCoverSubsystem` samples the navmesh, probes for walls in 8 directions and records full/half height cover plus left/right lean-out spots. No hand-placed markup. |
| **Squad director** | One suppresses, one flanks, the rest trade fire | `USquadSubsystem` assigns roles each second, relays sightings with a short radio delay, and hands out **attack tokens** so only two soldiers shoot at once — pressure without unfair focus fire. |
| **Suppression & morale** | Near misses make them duck; losing friends makes them break | Bullets passing close add suppression (accuracy down, peeks shorter). Morale drops with damage, suppression and nearby deaths, scaled by a per-soldier bravery trait. |
| **Fair aim** | First shots miss on purpose, so you can react | Spread settles over ~2 s of continuous sight, widens with target speed, own movement and suppression. |
| **Sound** | Sprinting gives you away, reloading invites a rush | Footsteps and reloads are hearing stimuli. Hearing a reload marks the target vulnerable: the squad yells "He's reloading, push!" and advances. |
| **Learning your habits** | They search the places you like to hide | The director keeps a heatmap of where players spend time and seeds search routes with it. |
| **Barks** | You always know what they are doing | Context lines ("Flanking!", "Man down!", "Lost visual!") with squad-wide cooldowns. |

## Architecture

```
Plugins/SquadAI  (game-agnostic, Runtime module)
├── ISquadCombatant            the only contract with the game: fire, reload, health, move style
├── ASquadAIController         senses (sight/hearing/damage), emotions, body helpers, barks
│   ├── USquadKnowledgeComponent   beliefs about hostiles (confidence, uncertainty, prediction)
│   └── USquadBrainComponent       utility selection over instanced USquadAction objects
├── Actions/                   Idle, Search, TakeCover, AttackFromCover, Suppress, Flank,
│                              Advance, Reload, Retreat, EngageInOpen
├── USquadCoverSubsystem       navmesh-driven cover generation and scoring (engage/flank/retreat)
├── USquadSubsystem            squad director: roles, tokens, comms, suppression, heatmap
└── USquadAIStatics            ReportShot / ReportNoise for game code and Blueprints

Source/TacticalSquadAI  (demo game)
├── ATSCharacter / ATSPlayerCharacter / ATSEnemyCharacter
├── ATSWeapon                  hitscan rifle shared by player and AI
├── UTSAnimInstance            fully native animation: 8-way locomotion, upper-body slot, aim offset
└── ATSHUD                     crosshair, vitals and the "AI Insight" overlay
```

The plugin never includes game headers. Any pawn implementing `ISquadCombatant` and `IGenericTeamAgentInterface`
can be possessed by `ASquadAIController`.

## Controls

| Key | Action |
|---|---|
| WASD / Mouse | Move / look |
| LMB / RMB | Fire / aim |
| Shift | Sprint (loud) |
| R | Reload (enemies can hear it) |
| **F1** | Toggle AI Insight overlay (roles, decisions, morale, what the AI believes) |
| **F2** | Toggle cover point debug |
| `squad.Debug 2` | Console: per-agent utility scores in world |

## Building

1. Unreal Engine 5.7, Visual Studio 2022 with the C++ game workload.
2. Right-click `TacticalSquadAI.uproject` → *Generate Visual Studio project files*, build `TacticalSquadAIEditor`.
3. Open the project; the startup map is `Lvl_CombatArena`.
4. To rebuild the arena from scratch: `UnrealEditor.exe TacticalSquadAI.uproject -ExecutePythonScript=Scripts/build_combat_arena.py -quit_after_build`

### Showcase map

`Lvl_TacticalYard` is the same layout dressed as a construction yard at sunset, used for the demo video. It needs
third-party art (a Fab construction pack and asphalt textures) that is not in this repository, so it is excluded by
`.gitignore`. With that content in place, run `Scripts/build_showcase_materials.py` and then
`Scripts/build_tactical_yard.py` inside the editor, rebuild navigation (`RebuildNavigation`) and save the map.

## Content

Mannequin characters and animations come from Epic's UE5 templates. Rifle sounds and tracer effects are
taken from free packs and are not redistributed in this repository.
