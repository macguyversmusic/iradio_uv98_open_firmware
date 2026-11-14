import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class SeedVectors extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address base = currentProgram.getMinAddress();
        int made = 0;
        for (int i = 1; i < 100; i++) {
            long v = getInt(base.add(i * 4L)) & 0xffffffffL;
            if ((v & 1) == 1 && v >= 0x08002800L && v < 0x08040000L) {
                Address a = toAddr(v & ~1L);
                disassemble(a);
                if (getFunctionAt(a) == null && createFunction(a, null) != null) {
                    made++;
                }
            }
        }
        setAnalysisOption(currentProgram, "ARM Aggressive Instruction Finder", "true");
        println("SeedVectors: created " + made + " functions from the vector table");
    }
}
