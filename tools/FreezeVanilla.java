import com.sun.jdi.Bootstrap;
import com.sun.jdi.ClassType;
import com.sun.jdi.ObjectReference;
import com.sun.jdi.ThreadReference;
import com.sun.jdi.Value;
import com.sun.jdi.VirtualMachine;
import com.sun.jdi.event.BreakpointEvent;
import com.sun.jdi.event.ClassPrepareEvent;
import com.sun.jdi.event.EventSet;
import com.sun.jdi.request.EventRequest;
import com.sun.jdi.request.EventRequestManager;
import com.sun.jdi.ReferenceType;
import java.util.Arrays;
import java.util.List;

/** Sets the official 1.21.11 server's normal tick-freeze flag before its first tick. */
class FreezeVanilla {
    private static final String SERVER = "net.minecraft.server.MinecraftServer";

    private static void scheduleAtTick(EventRequestManager manager, ReferenceType server) {
        // The wrapper JAR hash is pinned by the Python caller.
        var tick = server.methodsByName("a", "(Ljava/util/function/BooleanSupplier;)V").getFirst();
        var firstTick = manager.createBreakpointRequest(tick.location());
        firstTick.setSuspendPolicy(EventRequest.SUSPEND_EVENT_THREAD);
        firstTick.enable();
    }

    private static Value invoke(ThreadReference thread, ObjectReference receiver, String name,
                                String signature, Value... arguments) throws Exception {
        var method = ((ClassType) receiver.referenceType()).concreteMethodByName(name, signature);
        if (method == null) throw new IllegalStateException("Missing fixture hook: " + name + signature);
        return receiver.invokeMethod(thread, method, Arrays.asList(arguments),
            ObjectReference.INVOKE_SINGLE_THREADED);
    }

    private static void interact(VirtualMachine vm, ThreadReference thread, ObjectReference server,
                                    String[] args, int index) throws Exception {
        var world = (ObjectReference) invoke(thread, server, "N", "()Laxf;"); // MinecraftServer.overworld()
        var positions = (ClassType) vm.classesByName("is").getFirst(); // BlockPos
        var constructor = positions.methodsByName("<init>", "(III)V").getFirst();
        var pos = positions.newInstance(thread, constructor, List.of(vm.mirrorOf(Integer.parseInt(args[index])),
            vm.mirrorOf(Integer.parseInt(args[index+1])), vm.mirrorOf(Integer.parseInt(args[index+2]))),
            ObjectReference.INVOKE_SINGLE_THREADED);
        var state = (ObjectReference) invoke(thread, world, "a_", "(Lis;)Leoh;", pos); // Level.getBlockState()
        var block = (ObjectReference) invoke(thread, state, "b", "()Ldzq;"); // BlockState.getBlock()
        String method = switch (block.referenceType().name()) {
            case "eab" -> "a"; // ButtonBlock.press()
            case "eel" -> "b"; // LeverBlock.pull()
            default -> throw new IllegalStateException("Fixture control is absent: " + pos);
        };
        invoke(thread, block, method, "(Leoh;Ldwo;Lis;Lddm;)V", state, world, pos, null);
    }

    public static void main(String[] args) throws Exception {
        var connector = Bootstrap.virtualMachineManager().attachingConnectors().stream()
            .filter(candidate -> candidate.name().equals("com.sun.jdi.SocketAttach"))
            .findFirst().orElseThrow();
        var settings = connector.defaultArguments();
        settings.get("hostname").setValue("127.0.0.1");
        settings.get("port").setValue(args[0]);
        settings.get("timeout").setValue("5000");
        VirtualMachine vm = connector.attach(settings);
        try {
            var manager = vm.eventRequestManager();
            var loaded = vm.classesByName(SERVER);
            if (loaded.isEmpty()) {
                var prepare = manager.createClassPrepareRequest();
                prepare.addClassFilter(SERVER);
                prepare.setSuspendPolicy(EventRequest.SUSPEND_EVENT_THREAD);
                prepare.enable();
            } else {
                scheduleAtTick(manager, loaded.getFirst());
            }
            while (true) {
                EventSet events = vm.eventQueue().remove(90_000);
                if (events == null) throw new IllegalStateException("Vanilla did not enter its first server tick");
                for (var event : events) {
                    if (event instanceof ClassPrepareEvent prepared) {
                        scheduleAtTick(manager, prepared.referenceType());
                        prepared.request().disable();
                    } else if (event instanceof BreakpointEvent beforeTick) {
                        var server = beforeTick.thread().frame(0).thisObject();
                        var tickManagerField = beforeTick.location().declaringType().fieldByName("aF");
                        var tickManager = (ObjectReference) server.getValue(tickManagerField);
                        var setFrozen = ((ClassType) tickManager.referenceType()).concreteMethodByName("a", "(Z)V");
                        beforeTick.request().disable();
                        tickManager.invokeMethod(beforeTick.thread(), setFrozen,
                            List.of(vm.mirrorOf(true)), ObjectReference.INVOKE_SINGLE_THREADED);
                        for (int i=1; i<args.length; i+=3) interact(vm, beforeTick.thread(), server, args, i);
                        events.resume();
                        return;
                    }
                }
                events.resume();
            }
        } finally {
            vm.dispose();
        }
    }
}
