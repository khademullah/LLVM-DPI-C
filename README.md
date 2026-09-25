# Portwalk

Compiler-scheduled access ports for skyrmion racetrack memory, with the
controller checked through DPI-C.

Racetrack memory does not offer random access. A domain becomes visible only
after the nanowire shifts it under an access port, and a long run of zeros
(a stretch with no skyrmion) is a reliability problem. Both costs are visible
to a compiler: strides and gathers determine how far the port travels, and the
bit pattern of a stored word determines whether a write is legal.

This repository is a small, runnable cut of that co-design. It is a prototype
cost model and a single-port controller, not a device-physics simulator and
not a replacement for polyhedral loop optimization.

The tape diagram, a short account of how LLVM is used here, and how that maps onto NIC DMA, firmware, PCIe, and the networking stack are in [docs/guide.md](docs/guide.md). A narrated walkthrough from the tape itself is [docs/portwalk-demo.mp4](docs/portwalk-demo.mp4).

## Pipeline

```
C kernel
  |  clang -O2 -emit-llvm
  v
LLVM IR  --->  rtm-cost (ScalarEvolution stride, or "indirect")
  |                    |
  |                    v
  |             port scheduler (dependence-aware nearest port)
  |                    |
  v                    v
RLL bit-stuff     command stream
                       |
                       v
                  rtm_ctrl  <---DPI-C--->  C port model
                  (SystemVerilog FSM)      (scoreboard + trace)
```

| Piece | What it shows |
| --- | --- |
| `llvm/CostPass.cpp` | An LLVM pass over ScalarEvolution. Affine loops get a steady-state shift per iteration. Indirect addresses are refused by the static model and handed to the scheduler. |
| `src/schedule.c` | Ready-list scheduling of a memory-op bundle. Independent reads reorder to shorten head travel. A write still orders later accesses to the same domain. |
| `src/rll.c` | Bit-stuffing that caps zero-runs, the data-representation half of the reliability problem. |
| `rtl/rtm_ctrl.sv` | A one-command-in-flight shift controller. Same shape as firmware driving a memory-mapped engine. |
| `src/dpi_bridge.c` | DPI-C scoreboard. Every RTL command is replayed on the C model; head and read data must match. |
| `build/la_trace.csv` | Cycle, event, head, domain. The same columns a logic analyzer would capture off the controller. |

## Build and run

Requires LLVM/Clang 21 and Verilator (the versions this tree was developed against).

```bash
make check   # model, scheduler, RLL
make demo    # check + runtime numbers + LLVM report + DPI-C co-sim
```

`make demo` is the walkthrough. The three stages agree on the quantities below.

**Stride.** For 64 accesses with the head already on the first element, a unit-stride stream costs 63 shifts and a stride-64 stream costs 4032. On `row_sum` and `col_sum` the LLVM pass reports `steady-shift/iter` of 1 versus 64, and `carry-shifts` of 63 versus 4032. Those carry figures are the same numbers the C model counts.

**Ports.** Seeking domain 32 costs 32 shifts with one port and 0 shifts with a second port at offset 32. Port count belongs in the compiler's target description.

**Gathers.** Addresses `0 10 1 11 2 12` cost 48 shifts in program order and 12 after nearest-port scheduling. `gather_sum` is the IR case the pass marks `indirect`, because `vals[idx[i]]` has no affine stride.

**Zero-runs.** A sparse bit stream is bit-stuffed so no zero-run exceeds K. A raw all-zero word is counted as a write fault; the encoded word is not.

**Co-sim.** `rtm_ctrl` runs the same writes and readbacks. The DPI-C bridge fails the simulation if the RTL head or the returned data disagrees with the C model. The scoreboard trace is `build/cosim_trace.csv`.

`make demo` also loads the same pass into `opt`:

```bash
opt-21 -load-pass-plugin=build/rtm_cost.so -passes=rtm-cost -disable-output build/ir_kernels.ll
```

Two lines the checks lock onto:

```text
load A elem=4B step=256B (64 domains) steady-shift/iter=64 carry-shifts=4032
load vals indirect: no affine stride, hand the bundle to the port scheduler
```

## What is deliberately out of scope

Affine loop and layout rewriting for racetrack memory is already published as polyhedral compilation on Polly ([Polyhedral Compilation for Racetrack Memories](https://fis.tu-dresden.de/portal/en/publications/polyhedral-compilation-for-racetrack-memories(2efdc04f-8948-468f-8ad8-ea3f35b88880).html)). Portwalk sits beside that result. It schedules indirect gathers, the accesses outside the affine fragment, the way a DMA engine reorders descriptors. It bit-stuffs stored words so zero-runs stay bounded, and it checks that the controller which consumes a schedule matches the model the compiler used.

Timing is one cycle per domain of travel and one cycle per access. A domain is a 32-bit word. Shift-pulse energy, skyrmion diameter, and misalignment probability are not modeled. The RTL controller has one port; the C model is what evaluates extra ports.

## Layout

```
include/rtm.h          port model, RLL, scheduler
src/rtm_model.c        shift, access, trace
src/schedule.c         dependence-aware reorder
src/rll.c              zero-run limited encoding
src/demo.c             the numeric walkthrough
src/dpi_bridge.c       DPI-C scoreboard
llvm/CostPass.cpp      LLVM IR shift-cost pass
kernels/ir_kernels.c   row_sum, col_sum, gather_sum
rtl/rtm_ctrl.sv        shift controller
rtl/tb_rtm.sv          co-sim stimulus
docs/guide.md          tape diagram, LLVM, and the NIC/firmware mapping
docs/portwalk-demo.mp4 narrated walkthrough of the same counts
tests/test_rtm.c
```
