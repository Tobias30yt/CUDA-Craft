# Nether portal linking (0.11.2)

Portal travel now scales X/Z by 1/8 into the Nether and by 8 on return. Y is not
scaled. Negative coordinates use floor conversion. `/dimension` still moves to
the dimension spawn; End travel is unchanged.

The destination searches intact portal frames in a horizontal square of radius
16 in the Nether or 128 in the Overworld, using three-dimensional distance and
lower Y to break a distance tie. Frames must have a complete interior and required
obsidian edges. Relative player position within the source opening is mapped to
the destination opening; an axis change rotates yaw by 90 degrees. Inventory,
health and the existing arrival cooldown survive the transfer.

If no destination exists, a bounded search within 16 blocks finds a dry, supported
site with clearance on both sides. Creation stays below the Nether roof. A fallback
builds an obsidian landing platform and clears space when ordinary placement fails.
Fallback carving refuses solid player edits and bedrock. Block changes are atomic;
failed placement leaves the player in the source dimension with a retry cooldown.
The source frame is checked at travel time even if invalidation work is queued.

## Remaining differences

- The engine still has a fixed horizontal boundary of -1024..1023. Portal targets
  are clamped inside it with room for a frame. This is not Minecraft's full world border.
- Site ranking, clearance, fallback carving and final collision correction are not
  certified vanilla-identical. The implementation intentionally refuses destructive
  fallback carving through player builds. Mob travel and velocity transfer are absent.
- Only constructed/saved frames and the existing prototype-generated Nether frame
  are indexed by the search. Additional future generated portals must join this search.
- Nether/End terrain is still the independent prototype. Creating a portal can
  synchronously generate surrounding terrain chunks (up to 16 including clearance).

The version-specific API/constants were checked against the
[Fabric 1.21.11 portal documentation](https://maven.fabricmc.net/docs/yarn-1.21.11%2Bbuild.6/net/minecraft/world/dimension/PortalForcer.html)
and [constant table](https://maven.fabricmc.net/docs/yarn-1.21.11%2Bbuild.6/constant-values.html#net.minecraft.world.dimension.PortalForcer.field_52248).
Behavior checks and real-client results are recorded in [VALIDATION.md](VALIDATION.md).
