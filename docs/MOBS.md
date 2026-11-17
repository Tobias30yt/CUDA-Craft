# Mob support in 0.11

All **89 non-player mob types** in the pinned 1.21.11 entity registry can be
created through `/summon <name> [x y z]`. Available matching spawn eggs use the
same catalog. Armor stands and mannequins are excluded from the mob count.

IDs, body dimensions and the health metadata slot come from the pinned entity
reference. `tools/mob-data-manifest.json` records hashes. Base health, hostility,
flight/swimming categories and simplified drops are explicit approximate engine
profiles in `tools/generate-mob-data.py`, not extracted complete vanilla behavior.

Shared behavior includes spawn/render packets, saving/loading, damage, death,
knockback and basic movement. Hostile profiles approach and melee survival players;
neutral profiles become aggressive briefly when hit. Other profiles wander.
Flying profiles hover and aquatic profiles can move through water. Natural
spawning selects a small dimension/day-dependent subset. Selected mobs have one
simplified drop entry. Every type is limited by the existing 128 living-mob cap.

Species-specific behavior is **not complete**. Missing areas include navigation,
accurate attributes and equipment, ranged attacks/projectiles, explosions,
breeding/taming, trading, riding, variants, full loot tables, raids, transformations,
boss bars/phases and the dragon fight. A skeleton currently uses basic melee;
a creeper does not explode. Summoning a dragon or wither does not create a vanilla
boss encounter. Full mob mechanics cannot be claimed from catalog coverage.
