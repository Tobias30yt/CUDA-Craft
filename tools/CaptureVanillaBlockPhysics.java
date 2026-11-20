/**
 * Reads compact registry facts from the version-pinned official 1.21.11 server.
 * Run via generate-vanilla-block-physics.py, which checks the JAR hash.
 */
class CaptureVanillaBlockPhysics {
    public static void main(String[] args) throws Exception {
        var output = System.out;
        Class.forName("w").getMethod("a").invoke(null); // SharedConstants.tryDetectVersion()
        Class.forName("amv").getMethod("a").invoke(null); // Bootstrap.bootStrap()
        var blocks = Class.forName("dzq");
        var stateById = blocks.getMethod("a",int.class);
        var registry = blocks.getField("k").get(null);
        var contains = registry.getClass().getMethod("c",int.class);

        var state = Class.forName("eoh");
        var positionClass = Class.forName("is");
        var position = positionClass.getField("c").get(null); // BlockPos.ZERO
        var emptyWorld = Class.forName("dwf").getField("a").get(null); // EmptyBlockGetter.INSTANCE
        var isConductor = state.getMethod("d",Class.forName("dvt"),positionClass);
        var fullCollision = state.getMethod("m",Class.forName("dvt"),positionClass);
        var lightBlock = state.getMethod("g");
        var pushReaction = state.getMethod("r");
        var hasBlockEntity = state.getMethod("x");
        var isSignalSource = state.getMethod("p");
        var directionClass = Class.forName("iz");
        var directions = directionClass.getEnumConstants();
        var sturdyFace = state.getMethod("c",Class.forName("dvt"),positionClass,directionClass);
        var collisionShape = state.getMethod("g",Class.forName("dvt"),positionClass);
        var toBoxes = Class.forName("fug").getMethod("e");
        var bounds = new java.lang.reflect.Field[6];
        for (int i=0; i<bounds.length; ++i) bounds[i] = Class.forName("fth").getField(""+(char)('a'+i));
        for (int id=0; (boolean) contains.invoke(registry,id); ++id) {
            var blockState = stateById.invoke(null,id);
            int conductor = (boolean) isConductor.invoke(blockState,emptyWorld,position)?1:0;
            int opacity = (int) lightBlock.invoke(blockState);
            int reaction = ((Enum<?>) pushReaction.invoke(blockState)).ordinal();
            int blockEntity = (boolean) hasBlockEntity.invoke(blockState)?1:0;
            int solid = (boolean) fullCollision.invoke(blockState,emptyWorld,position)?1:0;
            int signalSource = (boolean) isSignalSource.invoke(blockState)?1:0;
            int faces = 0;
            for (int i=0; i<directions.length; ++i) {
                if ((boolean) sturdyFace.invoke(blockState,emptyWorld,position,directions[i])) faces |= 1<<i;
            }
            var row = new StringBuilder(id+" "+conductor+" "+opacity+" "+reaction+" "+blockEntity+" "+solid
                +" "+signalSource+" "+faces);
            var shape = collisionShape.invoke(blockState,emptyWorld,position);
            for (var box : (java.util.List<?>) toBoxes.invoke(shape)) {
                for (var coordinate : bounds) row.append(' ').append(coordinate.getDouble(box));
            }
            output.println(row);
        }
    }
}
