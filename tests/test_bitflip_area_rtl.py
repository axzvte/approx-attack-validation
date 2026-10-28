"""Synthetic functional checks only; never invoke synthesis or application workloads.

Run with Python where gcc, iverilog and vvp are installed (including WSL).
All compiler outputs and generated stimuli live in TemporaryDirectory.
"""
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RTL = ROOT / "hardware/rtl"


class BitflipAreaRTLTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        missing = [tool for tool in ("gcc", "iverilog", "vvp") if not shutil.which(tool)]
        if missing:
            raise unittest.SkipTest("Functional RTL tools missing: " + ", ".join(missing))

    def run_tool(self, args, cwd, stdin=None):
        result = subprocess.run(args, cwd=cwd, input=stdin, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def simulate(self, folder, bench, sources):
        (folder / "tb.sv").write_text(bench, encoding="utf-8")
        for source in sources:
            self.assertTrue((RTL / source).is_file(), f"Missing RTL implementation: {source}")
            shutil.copyfile(RTL / source, folder / source)
        self.run_tool(["iverilog", "-g2012", "-s", "tb", "-o", "sim", "tb.sv", *sources], folder)
        return self.run_tool(["vvp", "sim"], folder)

    def test_5rp_matches_c_oracle_with_preserved_asymmetric_operand_order(self):
        # C signature is (B,A); + or swapped RTL operands must fail these vectors.
        edges = [0, 1, 2, 3, 4, 7, 8, 15, 16, 1023, 1024, 2047, 2048, 2049, 4094, 4095]
        vectors = [(a, b) for a in edges for b in edges]
        vectors += [(a, 0) for a in range(4096)] + [(0, b) for b in range(4096)]
        rng = random.Random(0x5A12)
        vectors += [(rng.randrange(4096), rng.randrange(4096)) for _ in range(8192)]
        with tempfile.TemporaryDirectory(prefix="bitflip-rtl-") as temp:
            folder = Path(temp)
            shutil.copyfile(ROOT / "third_party/evoapproxlib/add12se_5RP.c", folder / "oracle.c")
            (folder / "main.c").write_text('''#include <stdio.h>
#include <stdint.h>
uint64_t add12se_5RP(uint64_t B, uint64_t A);
int main(void) { unsigned a,b;
  while (scanf("%u %u", &a, &b) == 2)
    printf("%llu\\n", (unsigned long long)add12se_5RP(b,a));
  return 0;
}
''', encoding="utf-8")
            self.run_tool(["gcc", "-std=c99", "-O2", "oracle.c", "main.c", "-o", "oracle"], folder)
            expected = [int(v) for v in self.run_tool([str(folder / "oracle")], folder,
                        "".join(f"{a} {b}\n" for a, b in vectors)).splitlines()]
            by_vector = dict(zip(vectors, expected))
            self.assertNotEqual(by_vector[(0, 4)], by_vector[(4, 0)])
            (folder / "vectors.txt").write_text("".join(f"{a:x} {b:x}\n" for a, b in vectors))
            bench = '''module tb;
reg [11:0] a,b; wire [12:0] y;
integer fd, status;
add12se_5RP dut(.a(a),.b(b),.y(y));
initial begin
 fd=$fopen("vectors.txt","r");
 while (!$feof(fd)) begin
  status=$fscanf(fd,"%h %h",a,b);
  if (status==2) begin #1; $display("RESULT %0d",y); end
 end
 $fclose(fd); $finish;
end
endmodule
'''
            observed = [int(v.removeprefix("RESULT ")) for v in
                        self.simulate(folder, bench, ["add12se_5RP.sv"]).splitlines()
                        if v.startswith("RESULT ")]
            self.assertEqual(len(observed), len(expected))
            for (a, b), got, want in zip(vectors, observed, expected):
                self.assertEqual(got, want, f"A={a}, B={b}")

    def test_signed_inclusive_trigger_all_fixed_payload_bits_and_preflip_output(self):
        # Unsigned comparison, exclusive endpoints, output feedback or bit12 flips fail.
        rng = random.Random(12)
        for width in (12, 13):
            minimum, maximum = -(1 << (width - 1)), (1 << (width - 1)) - 1
            samples = [(minimum, minimum, minimum), (maximum, maximum, maximum),
                       (-1, minimum, -1), (0, minimum, -1), (-1, 0, maximum),
                       (0, 0, maximum), (0, 1, -1), (-1, -1, 0), (0, -1, 0)]
            samples += [(rng.randint(minimum, maximum), rng.randint(minimum, maximum),
                         rng.randint(minimum, maximum)) for _ in range(200)]
            declarations = "\n".join(f"wire [12:0] y{bit}; bitflip_trigger #(.MONITOR_WIDTH({width}), .FLIP_BIT({bit})) t{bit}(monitor_value, lower, upper, baseline_value, y{bit});" for bit in range(12))
            statements = []
            for monitor, lower, upper in samples:
                baseline = rng.randrange(8192)
                statements.append(f"monitor_value={width}'h{monitor & ((1 << width)-1):x}; lower={width}'h{lower & ((1 << width)-1):x}; upper={width}'h{upper & ((1 << width)-1):x}; baseline_value=13'h{baseline:x}; #1;")
                for bit in range(12):
                    expected = baseline ^ ((1 << bit) if lower <= monitor <= upper else 0)
                    statements.append(f'if (y{bit} !== 13\'h{expected:x}) $fatal(1,"signed trigger width={width} bit={bit}");')
            # monitor is wired to the baseline output, before the payload flips it.
            if width == 13:
                declarations += "\nwire [12:0] preflip; bitflip_trigger #(.MONITOR_WIDTH(13), .FLIP_BIT(11)) outmon(baseline_value, lower, upper, baseline_value, preflip);"
                statements += ["baseline_value=13'h1fff; lower=-1; upper=-1; #1;",
                               'if (preflip !== 13\'h17ff) $fatal(1,"preflip output monitor");']
            bench = f"module tb; reg signed [{width-1}:0] monitor_value,lower,upper; reg [12:0] baseline_value;\n{declarations}\ninitial begin\n" + "\n".join(statements) + '\n$display("PASS"); $finish; end endmodule\n'
            with tempfile.TemporaryDirectory(prefix="bitflip-rtl-") as temp:
                self.assertIn("PASS", self.simulate(Path(temp), bench, ["bitflip_trigger.sv"]))

    def test_parallel_bounds_load_hold_update_and_signed12_exact_baseline(self):
        # Missing load enable, wrong signed width, or stale configuration fails.
        for width in (12, 13):
            bench = f'''module tb;
reg clk=0, cfg_load=0;
reg signed [{width-1}:0] cfg_lower,cfg_upper;
wire signed [{width-1}:0] lower,upper;
reg signed [11:0] a,b;
wire signed [12:0] baseline_value = {{a[11],a}} + {{b[11],b}};
wire [12:0] y;
bitflip_bounds #(.WIDTH({width})) bounds(clk,cfg_load,cfg_lower,cfg_upper,lower,upper);
bitflip_trigger #(.MONITOR_WIDTH({width}),.FLIP_BIT(0)) trigger(lower,lower,upper,baseline_value,y);
initial begin
 cfg_lower=-(1<<({width}-1)); cfg_upper=(1<<({width}-1))-1; cfg_load=1;
 #1; clk=1; #1;
 if (lower !== cfg_lower || upper !== cfg_upper) $fatal(1,"first parallel load");
 clk=0; cfg_load=0; cfg_lower=12; cfg_upper=34; #1; clk=1; #1;
 if (lower !== -(1<<({width}-1)) || upper !== (1<<({width}-1))-1) $fatal(1,"hold disabled");
 clk=0; cfg_load=1; cfg_lower=-1; cfg_upper=0; #1;
 if (upper !== (1<<({width}-1))-1) $fatal(1,"changed before clock");
 clk=1; #1;
 if (lower !== -1 || upper !== 0) $fatal(1,"second parallel load");
 a=-2048; b=-2048; #1;
 if (baseline_value !== 13'h1000 || y !== 13'h1001) $fatal(1,"negative exact baseline");
 a=2047; b=2047; #1;
 if (baseline_value !== 13'h0ffe || y !== 13'h0fff) $fatal(1,"positive exact baseline");
 a=-2048; b=2047; #1;
 if (baseline_value !== 13'h1fff || y !== 13'h1ffe) $fatal(1,"mixed exact baseline");
 $display("PASS"); $finish;
end endmodule
'''
            with tempfile.TemporaryDirectory(prefix="bitflip-rtl-") as temp:
                self.assertIn("PASS", self.simulate(Path(temp), bench, ["bitflip_trigger.sv"]))


if __name__ == "__main__":
    unittest.main()
